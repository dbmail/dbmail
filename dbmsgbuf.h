/* 
 * dbmsgbuf.h
 *
 * datatypes & function prototypes for the msgbuf system
 * using a mysql database
 *
 * should connect to any dbXXX.c file where XXXX is the dbase to be used
 */

#ifndef _DBMSGBUF_H
#define _DBMSGBUF_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmailtypes.h"
#include "memblock.h"

#define MSGBUF_FORCE_UPDATE -1

int db_init_msgfetch(u64_t uid);
int db_update_msgbuf(int minlen);
void db_close_msgfetch();
void db_give_msgpos(db_pos_t *pos);
u64_t db_give_range_size(db_pos_t *start, db_pos_t *end);

long db_dump_range(MEM *outmem, db_pos_t start, db_pos_t end, u64_t msguid);

#endif

