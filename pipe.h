/* $Id$ 
 * (c) 2000-2002 IC&S, The Netherlands */

#ifndef _PIPE_H
#define _PIPE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "list.h"

void create_unique_id(char *target, u64_t messageid);
char *read_header(u64_t *blksize);
int insert_messages(char *header, u64_t headersize,struct list *users, 
		    struct list *returnpath, int users_are_usernames, 
		    char *deliver_to_mailbox, struct list *headerfields);

#endif
