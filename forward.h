/* $Id$ */

#ifndef FORWARD_H_
#define FORWARD_H_
#include <stdio.h>
#include "dbmysql.h"
#include "debug.h"
#include "list.h"
#include "bounce.h"

#define FW_SENDMAIL SENDMAIL

int pipe_forward(FILE *instream, struct list *targets, char *header, unsigned long databasemessageid);

#endif
