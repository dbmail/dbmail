/* 
 * pool.h
 *
 * definition of process pool function prototypes
 */

#ifndef POOL_H
#define POOL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include "dbmail.h"
#include <sys/mman.h>
#include <sys/types.h>
#include "server.h"
#include "serverchild.h"

#define HARD_MAX_CHILDREN 50

#define STATE_NOOP -1
#define STATE_IDLE 0
#define STATE_CONNECTED 1
#define STATE_WAIT 2

#define TRUE 1
#define FALSE 0

#define SCOREBOARD_LOCK_FILE "/tmp/dbmail_scoreboard.LCK"

#define scoreboard_rdlck() set_lock(F_RDLCK)
#define scoreboard_wrlck() set_lock(F_WRLCK)
#define scoreboard_unlck() set_lock(F_UNLCK)

typedef struct {
	pid_t pid;
	time_t ctime;
	unsigned char status;
	unsigned long count;
	char * client;
} State_t;

typedef struct {
	unsigned int lock;
	serverConfig_t *conf;
	State_t child[HARD_MAX_CHILDREN];
} Scoreboard_t;


void scoreboard_new(serverConfig_t * conf);
void scoreboard_lock_new(void);
void scoreboard_release(pid_t pid);
void scoreboard_delete(void);
int child_register(void);
void child_reg_connected(void);
void child_reg_disconnected(void);
void child_unregister(void);
int count_children(void);
int count_spare_children(void);
pid_t get_idle_spare(void);
int getKey(pid_t pid);

void manage_start_children(void);
void manage_restart_children(void);
void manage_spare_children(void);
void manage_stop_children(void);
void scoreboard_setup(void);
void scoreboard_conf_check(void);
#endif
