/*
 * 
 * Copyright (C) 2005-2006 NFG Net Facilities Group BV support@nfg.nl
 * Copyright (C) 2006 Aaron Stone aaron@serendipity.cx
 *
 *
 * pool.c Management of process pool
 * 
 * 
 * TODO:
 *
 * general: statistics bookkeeping
 * 
 * 
 */

#include "dbmail.h"
#define THIS_MODULE "server"

static volatile Scoreboard_t *scoreboard;
static int shmid;
static int sb_lockfd;
static FILE *scoreFD;

extern volatile sig_atomic_t GeneralStopRequested;
extern ChildInfo_t childinfo;

static void state_reset(child_state_t *child); 
static int set_lock(int type);
static pid_t reap_child(void);

/*
 *
 *
 * Scoreboard
 *
 *
 */

void state_reset(child_state_t *s)
{
	s->pid = -1;
	s->ctime = time(0);
	s->status = STATE_NOOP;
	s->count = 0;
	memset(s->client, '\0', 128);
	memset(s->user, '\0', 128);
}

int set_lock(int type)
{
	int result, serr;
	struct flock lock;
	lock.l_type = type; /* F_RDLCK, F_WRLCK, F_UNLOCK */
	lock.l_start = 0;
	lock.l_whence = 0;
	lock.l_len = 1;
	result = fcntl(sb_lockfd, F_SETLK, &lock);
	if (result == -1) {
		serr = errno;
		switch (serr) {
			case EACCES:
			case EAGAIN:
			case EDEADLK:
				TRACE(TRACE_ERROR, "Error setting lock. Trying again.");
				usleep(10);
				set_lock(type);
				break;
			default:
				// ignore the rest
				break;
		}
		errno = serr;
	}
	return result;
}

void scoreboard_new(serverConfig_t * conf)
{
	int serr;
	if ((shmid = shmget(IPC_PRIVATE,
			(sizeof(child_state_t) * HARD_MAX_CHILDREN),
			0644 | IPC_CREAT)) == -1) {
		serr = errno;
		TRACE(TRACE_FATAL, "shmget failed [%s]",
				strerror(serr));
	}
	scoreboard = shmat(shmid, (void *) 0, 0);
	serr=errno;
	if (scoreboard == (Scoreboard_t *) (-1)) {
		TRACE(TRACE_FATAL, "scoreboard init failed [%s]",
		      strerror(serr));
		scoreboard_delete();
	}
	scoreboard_lock_new();
	scoreboard->conf = conf;
	scoreboard_setup();
	scoreboard_conf_check();

	/* Make sure that we clean up our shared memory segments when we exit
	 * normally (i.e. not by kill -9, if you do that, you get to clean this
	 * up yourself!)
	 * */
	atexit(scoreboard_delete); }

char * scoreboard_lock_getfilename(void)
{
	return g_strdup_printf("%s_%d.LCK", SCOREBOARD_LOCK_FILE, getpid());
}

void scoreboard_lock_new(void)
{
	gchar *statefile = scoreboard_lock_getfilename();
	if ( (sb_lockfd = open(statefile,O_EXCL|O_RDWR|O_CREAT|O_TRUNC,0600)) < 0) {
		TRACE(TRACE_FATAL, "Could not open lockfile [%s]", statefile);
	}
	g_free(statefile);
}

void scoreboard_setup(void) {	
	int i;
	scoreboard_wrlck();
	for (i = 0; i < HARD_MAX_CHILDREN; i++)
		state_reset(&scoreboard->child[i]);
	scoreboard_unlck();
}

void scoreboard_conf_check(void)
{
	/* some sanity checks on boundaries */
	scoreboard_wrlck();
	if (scoreboard->conf->maxChildren > HARD_MAX_CHILDREN) {
		TRACE(TRACE_WARNING, "MAXCHILDREN too large. Decreasing to [%d]",
		      HARD_MAX_CHILDREN);
		scoreboard->conf->maxChildren = HARD_MAX_CHILDREN;
	} else if (scoreboard->conf->maxChildren < scoreboard->conf->startChildren) {
		TRACE(TRACE_WARNING, "MAXCHILDREN too small. Increasing to NCHILDREN [%d]",
		      scoreboard->conf->startChildren);
		scoreboard->conf->maxChildren = scoreboard->conf->startChildren;
	}

	if (scoreboard->conf->maxSpareChildren > scoreboard->conf->maxChildren) {
		TRACE(TRACE_WARNING, "MAXSPARECHILDREN too large. Decreasing to MAXCHILDREN [%d]",
		      scoreboard->conf->maxChildren);
		scoreboard->conf->maxSpareChildren = scoreboard->conf->maxChildren;
	} else if (scoreboard->conf->maxSpareChildren < scoreboard->conf->minSpareChildren) {
		TRACE(TRACE_WARNING, "MAXSPARECHILDREN too small. Increasing to MINSPARECHILDREN [%d]",
		      scoreboard->conf->minSpareChildren);
		scoreboard->conf->maxSpareChildren = scoreboard->conf->minSpareChildren;
	}
	scoreboard_unlck();
}

static unsigned scoreboard_cleanup(void)
{
	/* return the number of children still registered as being alive */
	unsigned count = 0;
	int i, status = 0;
	pid_t chpid = 0;
	for (i = 0; i < scoreboard->conf->maxChildren; i++) {
		
		scoreboard_rdlck();
		chpid = scoreboard->child[i].pid;
		status = scoreboard->child[i].status;
		scoreboard_unlck();
		
		if (chpid <= 0)
			continue;
		
		count++;
		
		if (status == STATE_WAIT) {
			if (waitpid(chpid, NULL, WNOHANG | WUNTRACED) == chpid)
				scoreboard_release(chpid);			
		}
	}
	return count;
}

void scoreboard_release(pid_t pid)
{
	int key;
	key = getKey(pid);

	if (key == -1) 
		return;
	
	scoreboard_wrlck();
	state_reset(&scoreboard->child[key]);
	scoreboard_unlck();
}

void scoreboard_delete(void)
{
	gchar *statefile;
	extern int isGrandChildProcess;

	/* The middle process removes the scoreboards, so only bail out
	 * if we are a grandchild / connection handler process. */
	if (isGrandChildProcess)
		return;

	if (shmdt((const void *)scoreboard) == -1)
		TRACE(TRACE_ERROR, "detach shared mem failed");
	if (shmctl(shmid, IPC_RMID, NULL) == -1)
		TRACE(TRACE_ERROR, "delete shared mem segment failed");
	
	statefile = scoreboard_lock_getfilename();
	if (unlink(statefile) == -1) 
		TRACE(TRACE_ERROR, "error deleting scoreboard lock file [%s]", 
		      statefile);
	g_free(statefile);
	
	return;
}


int count_spare_children(void)
{
	int i, count;
	count = 0;
	
	scoreboard_rdlck();
	for (i = 0; i < scoreboard->conf->maxChildren; i++) {
		if (scoreboard->child[i].pid > 0
		    && scoreboard->child[i].status == STATE_IDLE)
			count++;
	}
	scoreboard_unlck();
	return count;
}

int count_children(void)
{
	int i, count;
	count = 0;
	
	scoreboard_rdlck();
	for (i = 0; i < scoreboard->conf->maxChildren; i++) {
		if (scoreboard->child[i].pid > 0)
			count++;
	}
	scoreboard_unlck();
	
	return count;
}

pid_t get_idle_spare(void)
{
	int i;
	pid_t idlepid = (pid_t) -1;
	/* get the last-in-first-out idle process */	
	scoreboard_rdlck();
	for (i = scoreboard->conf->maxChildren - 1; i >= 0; i--) {
		if ((scoreboard->child[i].pid > 0) && (scoreboard->child[i].status == STATE_IDLE)) {
			idlepid = scoreboard->child[i].pid;
			break;
		}
	}
	scoreboard_unlck();
	
	return idlepid;
}

int getKey(pid_t pid)
{
	int i;
	scoreboard_rdlck();
	for (i = 0; i < scoreboard->conf->maxChildren; i++) {
		if (scoreboard->child[i].pid == pid) {
			scoreboard_unlck();
			return i;
		}
	}
	scoreboard_unlck();
	TRACE(TRACE_ERROR, "pid NOT found on scoreboard [%d]", pid);
	return -1;
}

/*
 *
 *
 * Child
 *
 *
 */

int child_register(void)
{
	int i;
	pid_t chpid = getpid();
	
	TRACE(TRACE_MESSAGE, "register child [%d]", chpid);
	
	scoreboard_rdlck();
	for (i = 0; i < scoreboard->conf->maxChildren; i++) {
		if (scoreboard->child[i].pid == -1)
			break;
		if (scoreboard->child[i].pid == chpid) {
			TRACE(TRACE_ERROR, "child already registered.");
			scoreboard_unlck();
			return -1;
		}
	}
	scoreboard_unlck();
	
	if (i == scoreboard->conf->maxChildren) {
		TRACE(TRACE_WARNING, "no empty slot found");
		return -1;
	}
	
	scoreboard_wrlck();
	scoreboard->child[i].pid = chpid;
	scoreboard->child[i].status = STATE_IDLE;
	scoreboard_unlck();

	TRACE(TRACE_INFO, "initializing child_state [%d] using slot [%d]",
		chpid, i);
	return 0;
}

void child_reg_connected(void)
{
	int key;
	pid_t pid;
	
	pid = getpid();
	key = getKey(pid);
	
	if (key == -1) 
		TRACE(TRACE_FATAL, "unable to find this pid on the scoreboard");
	
	scoreboard_wrlck();
	scoreboard->child[key].status = STATE_CONNECTED;
	scoreboard->child[key].count++;
	scoreboard_unlck();
}

void child_reg_connected_client(char *ip, char *name)
{
	int key;
	pid_t pid;
	
	pid = getpid();
	key = getKey(pid);
	
	if (key == -1) 
		TRACE(TRACE_FATAL, "unable to find this pid on the scoreboard");
	
	scoreboard_wrlck();
	if (scoreboard->child[key].status == STATE_CONNECTED) {
		if (name && name[0])
			strncpy(scoreboard->child[key].client, name, 127);
		else
			strncpy(scoreboard->child[key].client, ip, 127);
	} else {
		TRACE(TRACE_MESSAGE, "client disconnected before status detail was logged");
	}
	scoreboard_unlck();
}

void child_reg_connected_user(char *user)
{
	int key;
	pid_t pid;
	
	pid = getpid();
	key = getKey(pid);
	
	if (key == -1) 
		TRACE(TRACE_FATAL, "unable to find this pid on the scoreboard");
	
	scoreboard_wrlck();
	if (scoreboard->child[key].status == STATE_CONNECTED) {
		strncpy(scoreboard->child[key].user, user, 127);
	} else {
		TRACE(TRACE_MESSAGE, "client disconnected before status detail was logged");
	}
	scoreboard_unlck();
}

void child_reg_disconnected(void)
{
	int key;
	pid_t pid;
	
	pid = getpid();
	key = getKey(pid);

	if (key == -1) 
		TRACE(TRACE_FATAL, "unable to find this pid on the scoreboard");
	
	scoreboard_wrlck();
	scoreboard->child[key].status = STATE_IDLE;
	memset(scoreboard->child[key].client, '\0', 128);
	memset(scoreboard->child[key].user, '\0', 128);
	scoreboard_unlck();
}

void child_unregister(void)
{
	/*
	 *
	 * Set the state for this slot to WAIT
	 * so the parent process can do a waitpid()
	 *
	 */
	int key;
	pid_t pid;
	
	pid = getpid();
	key = getKey(pid);
	
	if (key == -1)
		TRACE(TRACE_FATAL, "unable to find this pid on the scoreboard");
	
	scoreboard_wrlck();
	scoreboard->child[key].status = STATE_WAIT;
	scoreboard_unlck();
}


/*
 *
 *
 * Server
 *
 *
 */

/* Start the first batch of forked processes */
void manage_start_children(void)
{
	int i;
	for (i = 0; i < scoreboard->conf->startChildren; i++) {
		if (CreateChild(&childinfo) > -1)
			continue;
		manage_stop_children();
		TRACE(TRACE_FATAL, "could not create children.");
		exit(0);
	}

	scoreboard_state();
}

void manage_stop_children(void)
{
	/* 
	 *
	 * cleanup all remaining forked processes
	 *
	 */
	int alive = 0;
	int i, cnt = 0;
	pid_t chpid;
	
	TRACE(TRACE_MESSAGE, "General stop requested. Killing children...");

	for (i=0; i < scoreboard->conf->maxChildren; i++) {
		scoreboard_rdlck();
		chpid = scoreboard->child[i].pid;
		scoreboard_unlck();
		
		if (chpid < 0)
			continue;
		if (kill(chpid, SIGTERM))
			TRACE(TRACE_ERROR, "Cannot send SIGTERM to child [%d], error [%s]",
				(int)chpid, strerror(errno));
	}
	
	alive = scoreboard_cleanup();
	while (alive > 0 && cnt++ < 10) {
		alive = scoreboard_cleanup();
		sleep(1);
	}
	
	if (alive) {
		TRACE(TRACE_INFO, "[%d] children alive after SIGTERM, sending SIGKILL",
		      alive);

		for (i = 0; i < scoreboard->conf->maxChildren; i++) {
			scoreboard_rdlck();
			chpid = scoreboard->child[i].pid;
			scoreboard_unlck();
			
			if (chpid < 0)
				continue;
			kill(chpid, SIGKILL);;
			if (waitpid(chpid, NULL, WNOHANG | WUNTRACED) == chpid)
				scoreboard_release(chpid);
		}
	}
}

static pid_t reap_child(void)
{
	pid_t chpid=0;

	if ((chpid = get_idle_spare()) < 0)
		return 0; // no idle children

	if (kill(chpid, SIGTERM)) {
		TRACE(TRACE_ERROR, "Cannot send SIGTERM to child [%d], error [%s]",
			(int) chpid, strerror(errno));
		return -1;
	}
		
	return 0;
}

void manage_spare_children(void)
{
	/* 
	 *
	 * manage spare children while running. One child more/less for each run.
	 *
	 */
	pid_t chpid = 0;
	int spares, children;
	int changes = 0;
	int minchildren, maxchildren;
	int minspares, maxspares;
	
	if (GeneralStopRequested)
		return;

	// cleanup
	scoreboard_cleanup();

	children = count_children();
	spares = count_spare_children();
	
	/* scale up */
	minchildren = scoreboard->conf->startChildren;
	maxchildren = scoreboard->conf->maxChildren;
	minspares = scoreboard->conf->minSpareChildren;
	maxspares = scoreboard->conf->maxSpareChildren;

	if ((children < minchildren || spares < minspares) && children < maxchildren)  {
		if ((chpid = CreateChild(&childinfo)) < 0) 
			return;
		changes++;
	}
	/* scale down */
	else if (children > minchildren && spares > maxspares) {
		reap_child();
		changes++;
	}

	if (changes)
		scoreboard_state();

	children = count_children();
}

void scoreboard_state(void)
{
	char *state;
	int i;

	unsigned children = count_children();
	unsigned spares = count_spare_children();

	state = g_strdup_printf("Scoreboard state: children [%d/%d], spares [%d (%d - %d)]",
	      children,
	      scoreboard->conf->maxChildren,
	      spares,
	      scoreboard->conf->minSpareChildren,
	      scoreboard->conf->maxSpareChildren);

	/* Log it. */
	TRACE(TRACE_MESSAGE, "%s", state);

	/* Top it. */
	rewind(scoreFD);
	int printlen, scorelen = 0; // Tally up how much data has been written out.
	if ((printlen = fprintf(scoreFD, "%s\n", state)) <= 0
	  || !(scorelen += printlen)) {
		TRACE(TRACE_ERROR, "Couldn't write scoreboard state to top file [%s].",
			strerror(errno));
	}

	// Fixed 78 char output width.
	if ((printlen = fprintf(scoreFD, "%8s%8s%8s%8s%22s%22s\n\n",
		"Child", "PID", "Status", "Count", "Client", "User") <= 0)
	  || !(scorelen += printlen)) {
		TRACE(TRACE_ERROR, "Couldn't write scoreboard state to top file [%s].",
			strerror(errno));
	}

	for (i = 0; i < scoreboard->conf->maxChildren; i++) {
		int chpid, status;
		char *client, *user;
		unsigned count;
		
		scoreboard_rdlck();
		chpid = scoreboard->child[i].pid;
		status = scoreboard->child[i].status;
		count = scoreboard->child[i].count;
		client = scoreboard->child[i].client;
		user = scoreboard->child[i].user;
		scoreboard_unlck();

		// Matching 78 char fixed width as above.
		// Long hostnames may make the output a little messy.
		if ((printlen = fprintf(scoreFD, "%8d%8d%8d%8u%22s%22s\n",
			i, chpid, status, count, client, user)) <= 0
		  || !(scorelen += printlen)) {
			TRACE(TRACE_ERROR, "Couldn't write scoreboard state to top file [%s].",
				strerror(errno));
			break;
		}
	}
	fflush(scoreFD);
	ftruncate(fileno(scoreFD), scorelen);

	g_free(state);
}

static FILE *statefile_to_close;
static char *statefile_to_remove;

static void statefile_remove(void)
{
	int res;
	extern int isChildProcess;

	if (isChildProcess)
		return;

	if (statefile_to_close) {
		res = fclose(statefile_to_close);
		if (res) TRACE(TRACE_ERROR, "Error closing statefile: [%s].",
			strerror(errno));
		statefile_to_close = NULL;
	}

	if (statefile_to_remove) {
		res = unlink(statefile_to_remove);
		if (res) TRACE(TRACE_ERROR, "Error unlinking statefile [%s]: [%s].",
			statefile_to_remove, strerror(errno));
		g_free(statefile_to_remove);
		statefile_to_remove = NULL;
	}

}

void statefile_create(char *scoreFile)
{
	TRACE(TRACE_DEBUG, "Creating scoreboard at [%s].", scoreFile);
	if (!(scoreFD = fopen(scoreFile, "w"))) {
		TRACE(TRACE_ERROR, "Cannot open scorefile [%s], error was [%s]",
			scoreFile, strerror(errno));
	}
	chmod(scoreFile, 0644);
	if (scoreFD == NULL) {
		TRACE(TRACE_ERROR, "Could not create scoreboard [%s].", scoreFile );
	}

	atexit(statefile_remove);

	statefile_to_close = scoreFD;
	statefile_to_remove = g_strdup(scoreFile);
}

