/* $Id$
 * Functions for database communication */

#ifndef  _DBMYSQL_H
#define  _DBMYSQL_H

#include "/usr/include/mysql/mysql.h"
#include "debug.h"
#include "imap4.h"

struct session;
struct list;

int db_connect();
int db_query (char *query);
int db_check_user (char *username, struct list *userids);
unsigned long db_insert_message (unsigned long *useridnr);
unsigned long db_update_message (unsigned long *messageidnr, char *unique_id,
		unsigned long messagesize);
unsigned long db_insert_message_block (char *block, int nextblock);
int db_check_id (char *id);
int db_disconnect();
unsigned long db_insert_result ();
int db_send_message_lines (void *fstream, unsigned long messageidnr, unsigned long lines);
int db_send_message (void *fstream, unsigned long messageidnr);
unsigned long db_validate (char *user, char *password);
unsigned long db_md5_validate (char *username,unsigned char *md5_apop_he, char *apop_stamp);
int db_createsession (unsigned long useridnr, struct session *sessionptr);
int db_update_pop (struct session *sessionptr);

/* mailbox functionality */
unsigned long db_findmailbox(const char *name, unsigned long useridnr);
int db_getmailbox(mailbox_t *mb, unsigned long userid);
int db_createmailbox(const char *name, unsigned long ownerid);
int db_listmailboxchildren(unsigned long uid, unsigned long **children, int *nchildren,
			   const char *filter);
int db_removemailbox(unsigned long uid, unsigned long ownerid);
int db_isselectable(unsigned long uid);
int db_noinferiors(unsigned long uid);
int db_setselectable(unsigned long uid, int value);
int db_removemsg(unsigned long uid);
int db_movemsg(unsigned long to, unsigned long from);
int db_getmailboxname(unsigned long uid, char *name);
int db_setmailboxname(unsigned long uid, const char *name);
int db_expunge(unsigned long uid,unsigned long **msgids,int *nmsgs);
int db_build_msn_list(mailbox_t *mb);
unsigned long db_first_unseen(unsigned long uid);

#endif
