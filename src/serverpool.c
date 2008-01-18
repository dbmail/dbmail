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

FILE *scoreFD;

extern volatile sig_atomic_t GeneralStopRequested;
extern volatile sig_atomic_t alarm_occured;
extern volatile sig_atomic_t get_sigchld;
extern volatile sig_atomic_t mainStatus;

extern ChildInfo_t childinfo;

static void state_reset(child_state_t *child); 
static int set_lock(int type);
static pid_t reap_child(void);

volatile sig_atomic_t childSig = 0;
volatile sig_atomic_t ChildStopRequested = 0;
volatile sig_atomic_t connected = 0;

static int selfPipe[2];

volatile clientinfo_t client;


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
	// FIXME: valgrind is complaining about s->user going 2 bytes past the structure.
	memset(s->client, '\0', 128);
	memset(s->user, '\0', 128);
}

int set_lock(int type)
{
	int result, serr;
	struct flock lock;
	static int retry = 0;
	lock.l_type = type; /* F_RDLCK, F_WRLCK, F_UNLOCK */
	lock.l_start = 0;
	lock.l_whence = 0;
	lock.l_len = 1;
	result = fcntl(sb_lockfd, F_SETLK, &lock);
	if (result != -1) {
		retry = 0;
		return result;
	}

	serr = errno;
	switch (serr) {
		case EACCES:
		case EAGAIN:
		case EDEADLK:
			if (retry++ > 2)
				TRACE(TRACE_WARNING, "Error setting lock. Still trying...");
			usleep(10);
			set_lock(type);
			break;
		default:
			// ignore the rest
			retry = 0;
			break;
	}
	errno = serr;

	return result;
}

static char * scoreboard_lock_getfilename(void)
{
	return g_strdup_printf("%s_%d.LCK", SCOREBOARD_LOCK_FILE, getpid());
}

static void scoreboard_lock_new(void)
{
	gchar *statefile = scoreboard_lock_getfilename();
	if ( (sb_lockfd = open(statefile,O_EXCL|O_RDWR|O_CREAT|O_TRUNC,0600)) < 0) {
		TRACE(TRACE_FATAL, "Could not open lockfile [%s]", statefile);
	}
	g_free(statefile);
}

#define scoreboard_setup() \
	scoreboard_wrlck(); \
	for (i = 0; i < HARD_MAX_CHILDREN; i++) \
		state_reset((child_state_t *)&scoreboard->child[i]); \
	scoreboard_unlck()

#define scoreboard_conf_check() \
	scoreboard_wrlck(); \
	if (scoreboard->conf->maxChildren > HARD_MAX_CHILDREN) { \
		TRACE(TRACE_WARNING, "MAXCHILDREN too large. Decreasing to [%d]", \
		      HARD_MAX_CHILDREN); \
		scoreboard->conf->maxChildren = HARD_MAX_CHILDREN; \
	} else if (scoreboard->conf->maxChildren < scoreboard->conf->startChildren) { \
		TRACE(TRACE_WARNING, "MAXCHILDREN too small. Increasing to NCHILDREN [%d]", \
		      scoreboard->conf->startChildren); \
		scoreboard->conf->maxChildren = scoreboard->conf->startChildren; \
	}  \
	if (scoreboard->conf->maxSpareChildren > scoreboard->conf->maxChildren) { \
		TRACE(TRACE_WARNING, "MAXSPARECHILDREN too large. Decreasing to MAXCHILDREN [%d]", \
		      scoreboard->conf->maxChildren); \
		scoreboard->conf->maxSpareChildren = scoreboard->conf->maxChildren; \
	} else if (scoreboard->conf->maxSpareChildren < scoreboard->conf->minSpareChildren) { \
		TRACE(TRACE_WARNING, "MAXSPARECHILDREN too small. Increasing to MINSPARECHILDREN [%d]", \
		      scoreboard->conf->minSpareChildren); \
		scoreboard->conf->maxSpareChildren = scoreboard->conf->minSpareChildren; \
	} \
	scoreboard_unlck()

static int getKey(pid_t pid)
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

static void scoreboard_release(pid_t pid)
{
	int key;
	key = getKey(pid);

	if (key == -1) 
		return;
	
	scoreboard_wrlck();
	state_reset((child_state_t *)&scoreboard->child[key]);
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

static void scoreboard_delete(void)
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

static void scoreboard_new(serverConfig_t * conf)
{
	int serr;
	int i;
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
	atexit(scoreboard_delete); 
}



static int count_spare_children(void)
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

static int count_children(void)
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

static pid_t get_idle_spare(void)
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

/*
 *
 *
 * Child
 *
 *
 */

static int child_register(void)
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

static void child_reg_connected(void)
{
	int key;
	
	if (! scoreboard) // cli server
		return;

	if ((key = getKey(getpid())) == -1)
		TRACE(TRACE_FATAL, "unable to find this pid on the scoreboard");
	
	scoreboard_wrlck();
	scoreboard->child[key].status = STATE_CONNECTED;
	scoreboard->child[key].count++;
	scoreboard_unlck();
}

static void child_reg_disconnected(void)
{
	int key;
	
	if ((key = getKey(getpid())) == -1)
		TRACE(TRACE_FATAL, "unable to find this pid on the scoreboard");
	
	scoreboard_wrlck();
	scoreboard->child[key].status = STATE_IDLE;
	memset(scoreboard->child[key].client, '\0', 128);
	memset(scoreboard->child[key].user, '\0', 128);
	scoreboard_unlck();
}

static void child_unregister(void)
{
	/*
	 *
	 * Set the state for this slot to WAIT
	 * so the parent process can do a waitpid()
	 *
	 */
	int key;
	
	if (! scoreboard) // cli server
		return;

	if ((key = getKey(getpid())) == -1)
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

static void sig_handler(int sig, siginfo_t * info UNUSED, void *data UNUSED)
{
	int saved_errno = errno;
	
	/* Perform reinit at SIGHUP otherwise exit, but do nothing on
	 * SIGCHLD. Make absolutely sure that everything downwind of
	 * this function is signal-safe! Symptoms of signal-unsafe
	 * calls are random errors like this:
	 * *** glibc detected *** corrupted double-linked list: 0x0805f028 ***
	 * Right, so keep that in mind! */

	if (selfPipe[1] > -1)
		write(selfPipe[1], "S", 1);

	switch (sig) {

	case SIGCHLD:
		break;
	case SIGALRM:
		alarm_occured = 1;
		break;
	case SIGPIPE:
		break;
	default:
	 	ChildStopRequested = 1;
		childSig = sig;
		break;
	}
	errno = saved_errno;
}


/*
 * set_sighandler()
 * 
 * sets the signal handler for a child proces
 */
static int set_sighandler(void)
{
	struct sigaction act;
	struct sigaction rstact;

	memset(&act, 0, sizeof(act));
	memset(&rstact, 0, sizeof(rstact));

	act.sa_sigaction = sig_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;

	rstact.sa_sigaction = sig_handler;
	sigemptyset(&rstact.sa_mask);
	rstact.sa_flags = SA_SIGINFO | SA_RESETHAND;

	sigaddset(&act.sa_mask, SIGINT);
	sigaddset(&act.sa_mask, SIGQUIT);
	sigaddset(&act.sa_mask, SIGILL);
	sigaddset(&act.sa_mask, SIGBUS);
	sigaddset(&act.sa_mask, SIGFPE);
	sigaddset(&act.sa_mask, SIGSEGV);
	sigaddset(&act.sa_mask, SIGTERM);
	sigaddset(&act.sa_mask, SIGHUP);

	sigaction(SIGINT,	&rstact, 0);
	sigaction(SIGQUIT,	&rstact, 0);
	sigaction(SIGILL,	&rstact, 0);
	sigaction(SIGBUS,	&rstact, 0);
	sigaction(SIGFPE,	&rstact, 0);
	sigaction(SIGSEGV,	&rstact, 0);
	sigaction(SIGTERM,	&rstact, 0);
	sigaction(SIGHUP,	&rstact, 0);
	sigaction(SIGPIPE,	&rstact, 0);
	sigaction(SIGALRM,	&act, 0);
	sigaction(SIGCHLD,	&act, 0);

	TRACE(TRACE_INFO, "signal handler placed");

	return 0;
}


/*
 * CreateChild()
 *
 * creates a new child, returning only to the parent process
 */

static int select_and_accept(ChildInfo_t * info, int * clientSocket, struct sockaddr * saClient)
{
	fd_set rfds;
	int ip, result, flags;
	int active = 0, maxfd = 0;
	socklen_t len;

	/* This is adapted from man 2 select */
	FD_ZERO(&rfds);
	for (ip = 0; ip < info->numSockets; ip++) {
		FD_SET(info->listenSockets[ip], &rfds);
		maxfd = MAX(maxfd, info->listenSockets[ip]);
	}

	FD_SET(selfPipe[0], &rfds);
	maxfd = MAX(maxfd, selfPipe[0]);

	/* A null timeval means block indefinitely until there's activity. */
	result = select(maxfd+1, &rfds, NULL, NULL, NULL);
	if (result < 1)
		return -1;

	// Clear the self-pipe and return; we received a signal
	// and we need to loop again upstream to handle it.
	// See http://cr.yp.to/docs/selfpipe.html
	if (FD_ISSET(selfPipe[0], &rfds)) {
		char buf[1];
		while (read(selfPipe[0], buf, 1) > 0)
			;
		return -1;
	}

	/* This is adapted from man 2 select */
	for (ip = 0; ip < info->numSockets; ip++) {
		if (FD_ISSET(info->listenSockets[ip], &rfds)) {
			active = ip;
			break;
		}
	}

	/* accept the active fd */
	len = sizeof(struct sockaddr_in);

	// the listenSockets are set non-blocking in server.c,create_inet_socket
	*clientSocket = accept(info->listenSockets[active], saClient, &len);
	if (*clientSocket < 0)
		return -1;

	// the clientSocket *must* be blocking 
	flags = fcntl(*clientSocket, F_GETFL);
	if (*clientSocket > 0)
		fcntl(*clientSocket, F_SETFL, flags & ~ O_NONBLOCK);

	TRACE(TRACE_INFO, "connection accepted");
	return 0;
}


static int PerformChildTask(ChildInfo_t * info)
{
	int i, clientSocket, result;
	struct sockaddr_in saClient;
	struct hostent *clientHost;

	if (!info) {
		TRACE(TRACE_ERROR, "NULL info supplied");
		return -1;
	}

	if (db_connect() != 0) {
		TRACE(TRACE_ERROR, "could not connect to database");
		return -1;
	}

	if (auth_connect() != 0) {
		TRACE(TRACE_ERROR, "could not connect to authentication");
		return -1;
	}

	srand((int) ((int) time(NULL) + (int) getpid()));
	connected = 1;

	
	for (i = 0; i < info->maxConnect && !ChildStopRequested; i++) {

		if (db_check_connection()) {
			TRACE(TRACE_ERROR, "database has gone away");
			ChildStopRequested=1;
			continue;
		}

		child_reg_disconnected();

		/* wait for connect */
		result = select_and_accept(info, &clientSocket, (struct sockaddr *) &saClient);
		if (result != 0) {
			i--;	/* don't count this as a connect */
			continue;	/* accept failed, refuse connection & continue */
		}

		child_reg_connected();
		
		memset((void *)&client, 0, sizeof(client));	/* zero-init */

		client.timeout = info->timeout;
		client.login_timeout = info->login_timeout;
		strncpy((char *)client.ip_src, inet_ntoa(saClient.sin_addr), IPNUM_LEN);
		client.clientname[0] = '\0';
			
		if (info->resolveIP) {
			clientHost = gethostbyaddr((char *) &saClient.sin_addr, 
					sizeof(saClient.sin_addr), saClient.sin_family);

			if (clientHost && clientHost->h_name)
				strncpy((char *)client.clientname, clientHost->h_name, FIELDSIZE);

			TRACE(TRACE_MESSAGE, "incoming connection from [%s (%s)] by pid [%d]",
			      client.ip_src,
			      client.clientname[0] ? client.clientname : "Lookup failed", getpid());
		} else {
			TRACE(TRACE_MESSAGE, "incoming connection from [%s] by pid [%d]",
			      client.ip_src, getpid());
		}

		child_reg_connected_client((const char *)client.ip_src, (const char *)client.clientname);
		
		/* make streams */
		if (!(client.rx = fdopen(dup(clientSocket), "r"))) {
			/* read-FILE opening failure */
			TRACE(TRACE_ERROR, "error opening read file stream");
			close(clientSocket);
			continue;
		}

		if (!(client.tx = fdopen(clientSocket, "w"))) {
			/* write-FILE opening failure */
			TRACE(TRACE_ERROR, "error opening write file stream");
			fclose(client.rx);
			close(clientSocket);
			memset((void *)&client, 0, sizeof(client));
			continue;
		}

		setvbuf(client.tx, (char *) NULL, _IOLBF, 0);
		setvbuf(client.rx, (char *) NULL, _IOLBF, 0);

		TRACE(TRACE_DEBUG, "client info init complete, calling client handler");

		/* streams are ready, perform handling */
		info->ClientHandler((clientinfo_t *)&client);

		TRACE(TRACE_DEBUG, "client handling complete, closing streams");
		client_close();
		TRACE(TRACE_INFO, "connection closed");
	}

	if (!ChildStopRequested)
		TRACE(TRACE_ERROR, "maximum number of connections reached, stopping now");
	else{
		switch(childSig){
		case SIGHUP:
		case SIGTERM:
		case SIGQUIT:
			client_close();
			disconnect_all();
			child_unregister();
			exit(1);
		default:
			child_unregister();
			_exit(1);
		}
		TRACE(TRACE_ERROR, "stop requested");
	}

	child_reg_disconnected();
	disconnect_all();
	
	return 0;
}

static pid_t CreateChild(ChildInfo_t * info)
{
	extern int isGrandChildProcess;
	pid_t pid;

 	pid = fork();

	if (! pid) {
		if ( child_register() == -1 ) {
			TRACE(TRACE_FATAL, "child_register failed");
			_exit(0);
		}
	
		isGrandChildProcess = 1;
 		ChildStopRequested = 0;
		alarm_occured = 0;
		childSig = 0;

		if (pipe(selfPipe))
			return -1;

		fcntl(selfPipe[0], F_SETFL, O_NONBLOCK);
		fcntl(selfPipe[1], F_SETFL, O_NONBLOCK);

 		set_sighandler();
 		
		if (PerformChildTask(info) == -1) {
			close(selfPipe[0]); selfPipe[0] = -1;
			close(selfPipe[1]); selfPipe[1] = -1;
			return -1;
		}
		
 		child_unregister();
 		exit(0);
	} else {
 		usleep(5000);
		/* check for failed forkes */
		if (waitpid(pid, NULL, WNOHANG|WUNTRACED) == pid) 
			return -1;
 		return pid;
	}
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
	scorelen += fprintf(scoreFD, "\n");
	fflush(scoreFD);
	ftruncate(fileno(scoreFD), scorelen);

	g_free(state);
}

static pid_t reap_child(void)
{
	pid_t chpid=0;

	if ((chpid = get_idle_spare()) < 0)
		return 0; // no idle children

	if (kill(chpid, SIGTERM)) {
		int serr = errno;
		TRACE(TRACE_ERROR, "Cannot send SIGTERM to child [%d], error [%s]",
			(int) chpid, strerror(serr));
		errno = serr;
		return -1;
	}
		
	return 0;
}


// 
// Public methods
//

void client_close(void)
{
	if (client.tx) {
		fflush(client.tx);
		fclose(client.tx);	/* closes clientSocket as well */
		client.tx = NULL;
	}
	if (client.rx) {
		shutdown(fileno(client.rx), SHUT_RDWR);
		fclose(client.rx);
		client.rx = NULL;
	}
}

void disconnect_all(void)
{
	if (! connected)
		return;
	
	db_disconnect();
	auth_disconnect();
	connected = 0;
}

void pool_init(serverConfig_t *conf)
{
 	scoreboard_new(conf);
}

void pool_start(void)
{
	int i;
	for (i = 0; i < scoreboard->conf->startChildren; i++) {
		if (CreateChild(&childinfo) > -1)
			continue;
		pool_stop();
		TRACE(TRACE_FATAL, "could not create children.");
		exit(0);
	}

	scoreboard_state();
}
	
void pool_mainloop(void)
{
	int stopped = 0;
	pid_t chpid;

 	TRACE(TRACE_DEBUG, "starting main service loop");
 	while (!GeneralStopRequested) {
		if(get_sigchld){
			get_sigchld = 0;
			while((chpid = waitpid(-1,(int*)NULL,WNOHANG)) > 0) 
				scoreboard_release(chpid);
		}

		if (mainStatus) {
			mainStatus = 0;
			scoreboard_state();
		}

		if (db_check_connection() != 0) {
			
			if (! stopped) 
				pool_stop();
		
			stopped=1;
			sleep(10);
			
		} else {
			if (stopped) {
				pool_start();
				stopped=0;
			}
			
			pool_adjust();
			sleep(1);
		}
	}
}
void pool_adjust(void)
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


void pool_stop(void)
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

void child_reg_connected_client(const char *ip, const char *name)
{
	int key;
	pid_t pid;
	
	if (! scoreboard) // cli server
		return;

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
	
	if (! scoreboard) // cli server
		return;

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


