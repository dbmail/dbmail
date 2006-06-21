/*
 * 
 * Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl
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

#define P_SIZE 100000

static volatile Scoreboard_t *scoreboard;
static int shmid;
static int sb_lockfd;

extern int GeneralStopRequested;
extern ChildInfo_t childinfo;


static State_t state_new(void); 
static int set_lock(int type);
static pid_t reap_child(void);
/*
 *
 *
 * Scoreboard
 *
 *
 */

State_t state_new(void)
{
	State_t s;
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
		trace(TRACE_DEBUG, "%s,%s: error: %s",
				__FILE__, __func__, strerror(serr));
		
		/* TODO: this needs fixing */
		switch (serr) {
			case EDEADLK:
				sleep(2);
				set_lock(type);
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
	char *filename;
	GString *s = g_string_new("");
	g_string_printf(s,"%s_%d.LCK", SCOREBOARD_LOCK_FILE, getpid());
	filename = s->str;
	g_string_free(s,FALSE);
	return filename;
}

void scoreboard_lock_new(void)
{
	if ( (sb_lockfd = open(scoreboard_lock_getfilename(),O_EXCL|O_RDWR|O_CREAT|O_TRUNC,0600)) < 0) {
		trace(TRACE_FATAL,
		      "%s,%s, opening lockfile [%s] failed", 
		      __FILE__, __func__,
		      scoreboard_lock_getfilename());
	}
}

void scoreboard_setup(void) {	
	int i;
	scoreboard_wrlck();
	for (i = 0; i < HARD_MAX_CHILDREN; i++) {
		scoreboard->child[i] = state_new();
	}
	scoreboard_unlck();
}

void scoreboard_conf_check(void)
{
	/* some sanity checks on boundaries */
	scoreboard_wrlck();
	if (scoreboard->conf->maxChildren > HARD_MAX_CHILDREN) {
		trace(TRACE_WARNING,
		      "%s,%s: MAXCHILDREN too large. Decreasing to [%d]",
		      __FILE__,__func__,
		      HARD_MAX_CHILDREN);
		scoreboard->conf->maxChildren = HARD_MAX_CHILDREN;
	} else if (scoreboard->conf->maxChildren < scoreboard->conf->startChildren) {
		trace(TRACE_WARNING,
		      "%s,%s: MAXCHILDREN too small. Increasing to NCHILDREN [%d]",
		      __FILE__,__func__,
		      scoreboard->conf->startChildren);
		scoreboard->conf->maxChildren = scoreboard->conf->startChildren;
	}

	if (scoreboard->conf->maxSpareChildren > scoreboard->conf->maxChildren) {
		trace(TRACE_WARNING,
		      "%s,%s: MAXSPARECHILDREN too large. Decreasing to MAXCHILDREN [%d]",
		      __FILE__,__func__,
		      scoreboard->conf->maxChildren);
		scoreboard->conf->maxSpareChildren = scoreboard->conf->maxChildren;
	} else if (scoreboard->conf->maxSpareChildren < scoreboard->conf->minSpareChildren) {
		trace(TRACE_WARNING,
		      "%s,%s: MAXSPARECHILDREN too small. Increasing to MINSPARECHILDREN [%d]",
		      __FILE__,__func__,
		      scoreboard->conf->minSpareChildren);
		scoreboard->conf->maxSpareChildren = scoreboard->conf->minSpareChildren;
	}
	scoreboard_unlck();
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
void scoreboard_delete()
{
	if (shmdt((const void *)scoreboard) == -1)
		trace(TRACE_FATAL,
		      "%s,%s: detach shared mem failed",
		      __FILE__, __func__);
	if (shmctl(shmid, IPC_RMID, NULL) == -1)
		trace(TRACE_FATAL,
		      "%s,%s: delete shared mem segment failed",
		      __FILE__, __func__);
	
	if (unlink(scoreboard_lock_getfilename()) == -1) 
		trace(TRACE_ERROR,
		      "%s,%s: error deleting scoreboard lock file %s", 
		      __FILE__, __func__, 
		      scoreboard_lock_getfilename());
	
	return;
}
int count_spare_children()
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

int count_children()
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

pid_t get_idle_spare()
{
	int i;
	pid_t idlepid = (pid_t) -1;
	/* get the last-in-first-out idle process */	
	scoreboard_rdlck();
	for (i = scoreboard->conf->maxChildren - 1; i >= 0; i--) {
		if ((scoreboard->child[i].pid > 0) 
		    && (scoreboard->child[i].status == STATE_IDLE)) {
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
	trace(TRACE_ERROR,
	      "%s,%s: pid NOT found on scoreboard [%d]", __FILE__, __func__, pid);
	return -1;
}

/*
 *
 *
 * Child
 *
 *
 */

int child_register()
{
	int i;
	trace(TRACE_MESSAGE, "%s,%s: register child [%d]",
	      __FILE__, __func__, 
	      getpid());
	
	scoreboard_wrlck();
	for (i = 0; i < scoreboard->conf->maxChildren; i++) {
		if (scoreboard->child[i].pid == -1)
			break;
		if (scoreboard->child[i].pid == getpid()) {
			trace(TRACE_ERROR,
			      "%s,%s: child already registered.",
			      __FILE__, __func__);
			scoreboard_unlck();
			return -1;
		}
	}
	if (i == scoreboard->conf->maxChildren) {
		trace(TRACE_WARNING,
		      "%s,%s: no empty slot found",
		      __FILE__, __func__);
		scoreboard_unlck();
		return -1;
	}
	
	scoreboard->child[i].pid = getpid();
	scoreboard->child[i].status = STATE_IDLE;
	scoreboard_unlck();

	trace(TRACE_INFO, "%s,%s: initializing child_state [%d] using slot [%d]",
		__FILE__, __func__, getpid(), i);
	return 0;
}

void child_reg_connected()
{
	int key;
	pid_t pid;
	
	pid = getpid();
	key = getKey(pid);
	
	if (key == -1) 
		trace(TRACE_FATAL, "%s:%s: fatal: unable to find this pid on the scoreboard", __FILE__, __func__);
	
	scoreboard_wrlck();
	scoreboard->child[key].status = STATE_CONNECTED;
	scoreboard_unlck();

	trace(TRACE_DEBUG, "%s,%s: [%d]", __FILE__, __func__,
			getpid());
}

void child_reg_disconnected()
{
	int key;
	pid_t pid;
	
	pid = getpid();
	key = getKey(pid);

	if (key == -1) 
		trace(TRACE_FATAL, "%s:%s: fatal: unable to find this pid on the scoreboard", __FILE__, __func__);
	
	scoreboard_wrlck();
	scoreboard->child[key].status = STATE_IDLE;
	scoreboard_unlck();

	trace(TRACE_DEBUG, "%s,%s: [%d]", __FILE__, __func__,
		getpid());
}

void child_unregister()
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
		trace(TRACE_FATAL, "%s:%s: fatal: unable to find this pid on the scoreboard", __FILE__, __func__);
	
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

void manage_start_children()
{
	/* 
	 *
	 * startup the first batch of forked processes
	 *
	 */
	int i;
	for (i = 0; i < scoreboard->conf->startChildren; i++) {
		if (CreateChild(&childinfo) == -1) {
			manage_stop_children();
			trace(TRACE_FATAL,
			      "%s,%s: could not create children. Fatal.",
			      __FILE__, __func__);
			exit(0);
		}
	}
}
void manage_restart_children() { 
	/* restart active children */
	int i;
	pid_t chpid;
	for (i=0; i< scoreboard->conf->maxChildren; i++) {
		chpid=scoreboard->child[i].pid;
		if (chpid == -1)
			continue;
		if (waitpid(chpid, NULL, WNOHANG|WUNTRACED) == chpid) {
			trace(TRACE_MESSAGE,"%s,%s: child [%d] exited. Restarting...",
				__FILE__, __func__, chpid);
			scoreboard_release(chpid);			
			if (CreateChild(&childinfo)== -1) {
				GeneralStopRequested=1;
				manage_stop_children();
				exit(1);
			}
		}
	}
	sleep(1);
}

void manage_stop_children()
{
	/* 
	 *
	 * cleanup all remaining forked processes
	 *
	 */
	int stillSomeAlive = 1;
	int i, cnt = 0;
	pid_t chpid;
	
	trace(TRACE_MESSAGE, "%s,%s: General stop requested. Killing children.. ",
			__FILE__,__func__);

	while (stillSomeAlive && cnt < 10) {
		stillSomeAlive = 0;
		cnt++;

		for (i = 0; i < scoreboard->conf->maxChildren; i++) {
			chpid = scoreboard->child[i].pid;
			if (chpid <= 0)
				continue;

			if (waitpid(chpid, NULL, WNOHANG | WUNTRACED) == chpid) {
				scoreboard_release(chpid);			
			} else {
				stillSomeAlive = 1;
				if (cnt==1) /* no use killing the dead */
					kill(chpid, SIGTERM);

				usleep(1000);
			}
		}
		sleep(cnt);
	}

	if (stillSomeAlive) {
		trace(TRACE_INFO,
		      "%s,%s: not all children terminated at SIGTERM, killing hard now",
		      __FILE__,__func__);

		for (i = 0; i < scoreboard->conf->maxChildren; i++) {
			chpid = scoreboard->child[i].pid;
			if (chpid > 0) {
				kill(chpid, SIGKILL);;
				if (waitpid(chpid, NULL, WNOHANG | WUNTRACED) == chpid)
					scoreboard_release(chpid);
			}
		}
	}
}
static pid_t reap_child()
{
	pid_t chpid=0;
	
	if ((chpid = get_idle_spare()) < 0)
		return chpid;

	kill(chpid, SIGTERM);
	
	if (waitpid(chpid, NULL, WNOHANG | WUNTRACED) == chpid) 
		scoreboard_release(chpid);

	return chpid;
	
}
void manage_spare_children()
{
	/* 
	 *
	 * manage spare children while running
	 *
	 */
	pid_t chpid = 0;
	int spares;
	int children;
	
	if (GeneralStopRequested)
		return;
	
	/* scale up */
	spares = count_spare_children();
	children = count_children();
	while (children < scoreboard->conf->startChildren || spares < scoreboard->conf->minSpareChildren) {
		
		if (children >= scoreboard->conf->maxChildren)
			break;
		if ((chpid = CreateChild(&childinfo)) < 0) 
			break;
		
		children++;
		spares++;
	}

	/* scale down */
	spares = count_spare_children();
	children = count_children();
	while (children > scoreboard->conf->startChildren && spares > scoreboard->conf->maxSpareChildren) {
		if ((chpid = reap_child()) > 0) {
			children--;
			spares--;
		}
		if (chpid < 0)
			break;
	}
	
	if ((! chpid) && children > scoreboard->conf->startChildren && spares > scoreboard->conf->minSpareChildren)
		chpid = reap_child();
	
	spares = count_spare_children();
	children = count_children();
	
	/* scoreboard */
	if (chpid > 0) {
		trace(TRACE_MESSAGE, "%s,%s: children [%d/%d], spares [%d (%d - %d)]",
		      __FILE__,__func__,
		      children, scoreboard->conf->maxChildren, spares,
		      scoreboard->conf->minSpareChildren,
		      scoreboard->conf->maxSpareChildren);
	}

	if (chpid < 0)
		trace(TRACE_WARNING, "%s,%s: error scaling up/down", __FILE__, __func__);
	
	if (!children) {
		trace(TRACE_WARNING, "%s,%s: no children left ?. Aborting.", __FILE__,__func__);
		GeneralStopRequested = 1;
	}
}

