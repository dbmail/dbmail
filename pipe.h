/* $Id$ 
 * (c) 2000-2001 IC&S, The Netherlands */

#ifndef PIPE_H_
#define PIPE_H_

void create_unique_id(char *target, unsigned long messageid);
char *read_header(unsigned long *blksize);
int insert_messages(char *header, unsigned long headersize,struct list *users, struct list *returnpath);

#endif
