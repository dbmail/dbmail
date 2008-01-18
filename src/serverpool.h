/* 
 * Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl
 * pool.h
 *
 * definition of process pool function prototypes
 */

#ifndef POOL_H
#define POOL_H

#include "dbmail.h"

#define HARD_MAX_CHILDREN 300

#define STATE_NOOP -1
#define STATE_IDLE 0
#define STATE_CONNECTED 1
#define STATE_WAIT 2

#define SCOREBOARD_LOCK_FILE "/tmp/dbmail_scoreboard"

#define scoreboard_rdlck() set_lock(F_RDLCK)
#define scoreboard_wrlck() set_lock(F_WRLCK)
#define scoreboard_unlck() set_lock(F_UNLCK)

typedef struct {
	pid_t pid;
	time_t ctime;
	unsigned char status;
	unsigned long count;
	char client[128];
	char user[128];
} child_state_t;

typedef struct {
	unsigned int lock;
	serverConfig_t *conf;
	child_state_t child[HARD_MAX_CHILDREN];
} Scoreboard_t;

void pool_init(serverConfig_t *conf);
void pool_start(void);
void pool_adjust(void);
void pool_mainloop(void);
void pool_stop(void);

void child_reg_connected_client(const char *ip, const char *name);
void child_reg_connected_user(char *user);

void client_close(void);
void disconnect_all(void);

#endif
