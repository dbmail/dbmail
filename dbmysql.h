/* $Id$
 * Functions for database communication */

#ifndef  _DBMYSQL_H
#define  _DBMYSQL_H
#endif

#include "/usr/local/include/mysql/mysql.h"
#include "debug.h"

struct session;
struct list;

int db_connect();
int db_query (char *query);
int db_check_user (char *username, struct list *userids);
unsigned long db_insert_message_block (char *block, int nextblock);
int db_check_id (char *id);
int db_disconnect();
unsigned long db_insert_result ();
int db_send_header (void *fstream, unsigned long messageidnr);
int db_send_message (void *fstream, unsigned long messageidnr);
unsigned long db_validate (char *user, char *password);
int db_createsession (unsigned long useridnr, struct session *sessionptr);
int db_update_pop (struct session *sessionptr);
