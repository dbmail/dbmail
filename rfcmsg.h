/*
 * rfcmsg.h
 * 
 * function prototypes to enable message parsing
 */

#ifndef _RFCMSG_H
#define _RFCMSG_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmailtypes.h"

void db_free_msg(mime_message_t *msg);
void db_reverse_msg(mime_message_t *msg);

int db_fetch_headers(u64_t msguid, mime_message_t *msg);
int db_add_mime_children(struct list *brothers, char *splitbound, int *level, int maxlevel);
int db_start_msg(mime_message_t *msg, char *stopbound, int *level, int maxlevel);
int db_parse_as_text(mime_message_t *msg);
int db_msgdump(mime_message_t *msg, u64_t msguid, int level);

#endif
