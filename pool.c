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

#define P_SIZE 100000

static volatile Scoreboard_t *scoreboard;
static int shmid;
static int sb_lockfd;
static FILE *scoreFD;

extern volatile sig_atomic_t GeneralStopRequested;
extern ChildInfo_t childinfo;

static child_state_t state_new(void); 
static int set_lock(int type);
static pid_t reap_child(void);

/*
 *
 *
 * Scoreboard
 *
 *
 */

child_state_t state_new(void)
{
	child_state_t s;
	s.pid = -1;
	s.ctime = time(0);
	s.status = STATE_NOOP;
	s.count = 0;
	s.client = "none";
	return s;
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
	if ((shmid = shmget(IPC_PRIVATE, P_SIZE, 0644 | IPC_CREAT)) == -1) {
		serr = errno;
		trace(TRACE_FATAL, "%s,%s: shmget failed [%s]",
				__FILE__,__func__, 
				strerror(serr));
	}
	scoreboard = shmat(shmid, (void *) 0, 0);
	serr=errno;
	if (scoreboard == (Scoreboard_t *) (-1)) {
		trace(TRACE_FATAL, "%s,%s: scoreboard init failed [%s]",
		      __FILE__,__func__,
		      strerror(serr));
		scoreboard_delete();
	}
	scoreboard_lock_new();
	scoreboard->conf = conf;
	scoreboard_setup();
	scoreboard_conf_check();
}

char * scoreboard_lock_getfilename(void)
{
	return g_strdup_printf("%s_%d.LCK", SCOREBOARD_LOCK_FILE, getpid());
}

void scoreboard_lock_new(void)
{
	gchar *statefile = scoreboard_lock_getfilename();
	if ( (sb_lockfd = open(statefile,O_EXCL|O_RDWR|O_CREAT|O_TRUNC,0600)) < 0) {
		trace(TRACE_FATAL, "%s,%s, opening lockfile [%s] failed", 
		      __FILE__, __func__, statefile);
	}
	g_free(statefile);
}

void scoreboard_setup(void) {	
	int i;
	scoreboard_wrlck();
	for (i = 0; i < HARD_MAX_CHILDREN; i++)
		scoreboard->child[i] = state_new();
	scoreboard_unlck();
}

void scoreboard_conf_check(void)
{
	/* some sanity checks on boundaries */
	scoreboard_wrlck();
	if (scoreboard->conf->maxChildren > HARD_MAX_CHILDREN) {
		trace(TRACE_WARNING, "%s,%s: MAXCHILDREN too large. Decreasing to [%d]",
		      __FILE__,__func__,
		      HARD_MAX_CHILDREN);
		scoreboard->conf->maxChildren = HARD_MAX_CHILDREN;
	} else if (scoreboard->conf->maxChildren < scoreboard->conf->startChildren) {
		trace(TRACE_WARNING, "%s,%s: MAXCHILDREN too small. Increasing to NCHILDREN [%d]",
		      __FILE__,__func__,
		      scoreboard->conf->startChildren);
		scoreboard->conf->maxChildren = scoreboard->conf->startChildren;
	}

	if (scoreboard->conf->maxSpareChildren > scoreboard->conf->maxChildren) {
		trace(TRACE_WARNING, "%s,%s: MAXSPARECHILDREN too large. Decreasing to MAXCHILDREN [%d]",
		      __FILE__,__func__,
		      scoreboard->conf->maxChildren);
		scoreboard->conf->maxSpareChildren = scoreboard->conf->maxChildren;
	} else if (scoreboard->conf->maxSpareChildren < scoreboard->conf->minSpareChildren) {
		trace(TRACE_WARNING, "%s,%s: MAXSPARECHILDREN too small. Increasing to MINSPARECHILDREN [%d]",
		      __FILE__,__func__,
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
	scoreboard->child[key] = state_new();
	scoreboard_unlck();
}

void scoreboard_delete(void)
{
	gchar *statefile;
	if (shmdt((const void *)scoreboard) == -1)
		trace(TRACE_FATAL, "%s,%s: detach shared mem failed",
		      __FILE__, __func__);
	if (shmctl(shmid, IPC_RMID, NULL) == -1)
		trace(TRACE_FATAL, "%s,%s: delete shared mem segment failed",
		      __FILE__, __func__);
	
	statefile = scoreboard_lock_getfilename();
	if (unlink(statefile) == -1) 
		trace(TRACE_ERROR, "%s,%s: error deleting scoreboard lock file %s", 
		      __FILE__, __func__, 
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
	trace(TRACE_ERROR, "%s,%s: pid NOT found on scoreboard [%d]",
		       	__FILE__, __func__, pid);
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
	
	trace(TRACE_MESSAGE, "%s,%s: register child [%d]",
	      __FILE__, __func__, chpid);
	
	scoreboard_rdlck();
	for (i = 0; i < scoreboard->conf->maxChildren; i++) {
		if (scoreboard->child[i].pid == -1)
			break;
		if (scoreboard->child[i].pid == chpid) {
			trace(TRACE_ERROR, "%s,%s: child already registered.",
			      __FILE__, __func__);
			scoreboard_unlck();
			return -1;
		}
	}
	scoreboard_unlck();
	
	if (i == scoreboard->conf->maxChildren) {
		trace(TRACE_WARNING, "%s,%s: no empty slot found",
		      __FILE__, __func__);
		return -1;
	}
	
	scoreboard_wrlck();
	scoreboard->child[i].pid = chpid;
	scoreboard->child[i].status = STATE_IDLE;
	scoreboard_unlck();

	trace(TRACE_INFO, "%s,%s: initializing child_state [%d] using slot [%d]",
		__FILE__, __func__, chpid, i);
	return 0;
}

void child_reg_connected(void)
{
	int key;
	pid_t pid;
	
	pid = getpid();
	key = getKey(pid);
	
	if (key == -1) 
		trace(TRACE_FATAL, "%s:%s: fatal: unable to find this pid on the scoreboard", 
				__FILE__, __func__);
	
	scoreboard_wrlck();
	scoreboard->child[key].status = STATE_CONNECTED;
	scoreboard_unlck();
}

void child_reg_disconnected(void)
{
	int key;
	pid_t pid;
	
	pid = getpid();
	key = getKey(pid);

	if (key == -1) 
		trace(TRACE_FATAL, "%s:%s: fatal: unable to find this pid on the scoreboard", 
				__FILE__, __func__);
	
	scoreboard_wrlck();
	scoreboard->child[key].status = STATE_IDLE;
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
		trace(TRACE_FATAL, "%s:%s: fatal: unable to find this pid on the scoreboard", 
				__FILE__, __func__);
	
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

void manage_start_children(void)
{
	/* 
	 *
	 * startup the first batch of forked processes
	 *
	 */
	int i;
	for (i = 0; i < scoreboard->conf->startChildren; i++) {
		if (CreateChild(&childinfo) > -1)
			continue;
		manage_stop_children();
		trace(TRACE_FATAL, "%s,%s: could not create children.",
		      __FILE__, __func__);
		exit(0);
	}
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
	
	trace(TRACE_MESSAGE, "%s,%s: General stop requested. Killing children.. ",
			__FILE__,__func__);

	for (i=0; i < scoreboard->conf->maxChildren; i++) {
		scoreboard_rdlck();
		chpid = scoreboard->child[i].pid;
		scoreboard_unlck();
		
		if (chpid < 0)
			continue;
		if (kill(chpid, SIGTERM))
			trace(TRACE_ERROR, "%s,%s: %s", __FILE__, __func__, strerror(errno));
	}
	
	alive = scoreboard_cleanup();
	while (alive > 0 && cnt++ < 10) {
		alive = scoreboard_cleanup();
		sleep(1);
	}
	
	if (alive) {
		trace(TRACE_INFO, "%s,%s: [%d] children alive after SIGTERM, sending SIGKILL",
		      __FILE__,__func__, alive);

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
		trace(TRACE_ERROR, "%s,%s: %s", __FILE__, __func__, strerror(errno));
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
	if (fprintf(scoreFD, "%s\n", state) <= 0) {
		TRACE(TRACE_ERROR, "Couldn't write scoreboard state to top file [%s].",
			strerror(errno));
	}
	for (i = 0; i < scoreboard->conf->maxChildren; i++) {
		int chpid;
		int status;
		
		scoreboard_rdlck();
		chpid = scoreboard->child[i].pid;
		status = scoreboard->child[i].status;
		scoreboard_unlck();

		if (fprintf(scoreFD, "Child %d Pid %d Status %d\n", i, chpid, status) <= 0) {
			TRACE(TRACE_ERROR, "Couldn't write scoreboard state to top file [%s].",
				strerror(errno));
			break;
		}
	}
	fflush(scoreFD);

	g_free(state);
}

static FILE *statefile_to_close;
static char *statefile_to_remove;

static void statefile_remove(void)
{
	int res;

	if (statefile_to_close) {
		res = fclose(statefile_to_close);
		if (res) trace(TRACE_ERROR, "Error closing statefile: [%s].",
			strerror(errno));
		statefile_to_close = NULL;
	}

	if (statefile_to_remove) {
		res = unlink(statefile_to_remove);
		if (res) trace(TRACE_ERROR, "Error unlinking statefile [%s]: [%s].",
			statefile_to_remove, strerror(errno));
		g_free(statefile_to_remove);
		statefile_to_remove = NULL;
	}

}

void statefile_create(char *scoreFile)
{
	TRACE(TRACE_DEBUG, "Creating scoreboard at [%s].", scoreFile);
	// FIXME: Check ownership and permissions. Must be root and 0644 or better.
	scoreFD = fopen(scoreFile, "w");
	if (scoreFD == NULL) {
		TRACE(TRACE_ERROR, "Could not create scoreboard [%s].", scoreFile );
	}

	atexit(statefile_remove);

	statefile_to_close = scoreFD;
	statefile_to_remove = g_strdup(scoreFile);
}

