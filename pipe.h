/* $Id$ */

#ifndef PIPE_H_
#define PIPE_H_
#include <time.h>
#include "dbmysql.h"
#include "debug.h"
#include "list.h"
#include "bounce.h"

void create_unique_id(char *target, unsigned long messageid);
char *read_header(unsigned long *blksize);
int insert_messages(char *firstblock, unsigned long headersize);

#endif
