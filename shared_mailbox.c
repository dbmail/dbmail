/*
 Copyright (C) 2003 IC & S  dbmail@ic-s.nl

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 $Id$
*/
/**
   \file shared_mailbox.c
   \brief implementation of shared folders 
   \author IC & S, the Netherlands (c) 2003
*/
#include "shared_mailbox.h"
#include "db.h"
#include <assert.h>
#include <limits.h>
#include <regex.h>
#include <stdlib.h>

#define SHARED_MAILBOX_OWNER 0

/* find the mailbox_idnr of a shared mailbox. If only_subscribed is 1, the
   mailbox_idnr is only filled out if the user is subscribed to this 
   mailbox */
static int shared_mailbox_find_general(const char *mailbox_name, 
				       u64_t user_idnr,
				       int only_subscribed, 
				       u64_t *mailbox_idnr);
/* find shared_mailbox_id for a mailbox */
static int find_shared_mailbox_id(u64_t mailbox_id, u64_t *shared_mailbox_id);

static int insert_shared_mailbox_record(u64_t mailbox_id, u64_t maxmail_size,
					u64_t *shared_mailbox_id);
static int insert_shared_mailbox_access_entries_client(const char *name,
						       u64_t shmbx_id,
						       u64_t client_id);
static int insert_shared_mailbox_access_entry(const char *name,
					      u64_t shmbx_id,
					      u64_t user_id);
static int delete_shared_mailbox_record(u64_t shmbx_id);

int shared_mailbox_is_shared(u64_t mailbox_id) 
{
     int result = 0;

     snprintf(query, DEF_QUERYSIZE,
	      "SELECT mbx.mailbox_idnr FROM mailboxes mbx "
	      "WHERE mbx.mailbox_idnr = '%llu' "
	      "AND mbx.owner_idnr = '%u'", 
	      mailbox_id, (unsigned) SHARED_MAILBOX_OWNER);
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: error executing query", 
		__FILE__, __FUNCTION__);
	  return -1;
     }

     if (db_num_rows() > 0) 
	  result = 1;
     else
	  result = 0;

     db_free_result();
     return result;
}

/* check if a message is in a shared mailbox */
int shared_mailbox_msg_in_shared(u64_t message_id)
{
     int result = 0;

     snprintf(query, DEF_QUERYSIZE,
	      "SELECT msg.message_idnr FROM messages msg, mailboxes mbx "
	      "WHERE mbx.mailbox_idnr = msg.mailbox_idnr "
	      "AND msg.message_idnr = '%llu' "
	      "AND mbx.owner_idnr = '%u'", message_id, 
	      (unsigned) SHARED_MAILBOX_OWNER);
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: error executing query", 
		__FILE__, __FUNCTION__);
	  return -1;
     }

     if (db_num_rows() > 0) 
	  result = 1;
     else
	  result = 0;

     db_free_result();
     return result;
}
     

int shared_mailbox_find(const char *mailbox_name, u64_t user_idnr, u64_t* mailbox_idnr)
{
     return shared_mailbox_find_general(mailbox_name, user_idnr, 0, mailbox_idnr);
}

int shared_mailbox_find_subscribed(const char *mailbox_name, u64_t user_idnr, 
				   u64_t* mailbox_idnr)
{
     return shared_mailbox_find_general(mailbox_name, user_idnr, 1, mailbox_idnr);
}

int shared_mailbox_find_general(const char *mailbox_name, u64_t user_idnr,
			int only_subscribed, u64_t *mailbox_idnr)
{
     char *result_string;

     assert (mailbox_name != NULL);
     assert (mailbox_idnr != NULL);

     *mailbox_idnr = 0;
     if (only_subscribed) 
	  snprintf(query, DEF_QUERYSIZE,
		   "SELECT mbx.mailbox_idnr FROM "
		   "mailboxes mbx, shared_mailbox shmbx, "
		   "shared_mailbox_access shmbx_acc "
		   "WHERE mbx.name = '%s' "
		   "AND shmbx.mailbox_id = mbx.mailbox_idnr "
		   "AND shmbx_acc.shared_mailbox_id = shmbx.id "
		   "AND shmbx_acc.user_id = '%llu' "
		   "AND shmbx_acc.is_subscribed = '1'", mailbox_name, user_idnr);
     else
	  snprintf(query, DEF_QUERYSIZE,
		   "SELECT mbx.mailbox_idnr FROM "
		   "mailboxes mbx, shared_mailbox shmbx, "
		   "shared_mailbox_access shmbx_acc "
		   "WHERE mbx.name = '%s' "
		   "AND shmbx.mailbox_id = mbx.mailbox_idnr "
		   "AND shmbx_acc.shared_mailbox_id = shmbx.id "
		   "AND shmbx_acc.user_id = '%llu' "
		   "AND shmbx_acc.lookup_flag = '1'", mailbox_name, user_idnr);
     
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: database error finding shared mailbox",
		__FILE__, __FUNCTION__);
	  return -1;
     }

     if (db_num_rows() < 1) {
	  trace(TRACE_ERROR, "%s,%s: no shared mailbox with name [%s] found for "
		"user [%llu]", __FILE__, __FUNCTION__, mailbox_name, user_idnr);
	  db_free_result();
	  return 1;
     }

     result_string = db_get_result(0, 0);
     *mailbox_idnr = result_string ? strtoull(result_string, NULL, 10) : 0;
     db_free_result();
     return 1;
     
}

int shared_mailbox_list_by_regex(u64_t user_idnr, int only_subscribed, 
				 regex_t *preg,
				 u64_t **mailboxes, unsigned int *nr_mailboxes)
{
     unsigned int i;
     u64_t *tmp;
     char *result_string;

     assert(mailboxes != NULL);
     assert(nr_mailboxes != NULL);
     assert(preg != NULL);

     *mailboxes = NULL;
     *nr_mailboxes = 0;
     if (only_subscribed)
	  snprintf(query, DEF_QUERYSIZE,
		   "SELECT mbx.name, mbx.mailbox_idnr "
		   "FROM mailboxes mbx, shared_mailbox shmbx, "
		   "shared_mailbox_access shmbx_acc "
		   "WHERE mbx.mailbox_idnr = shmbx.mailbox_id "
		   "AND shmbx.id = shmbx_acc.shared_mailbox_id "
		   "AND shmbx_acc.user_id = '%llu' "
		   "AND shmbx_acc.is_subscribed = '1'", user_idnr);
     else
	  snprintf(query, DEF_QUERYSIZE,
		   "SELECT mbx.name, mbx.mailbox_idnr "
		   "FROM mailboxes mbx, shared_mailbox shmbx, "
		   "shared_mailbox_access shmbx_acc "
		   "WHERE mbx.mailbox_idnr = shmbx.mailbox_id "
		   "AND shmbx.id = shmbx_acc.shared_mailbox_id "
		   "AND shmbx_acc.user_id = '%llu'", user_idnr);
     
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: error during mailbox query",
		__FILE__, __FUNCTION__);
	  return (-1);
     }
     
     if (db_num_rows() == 0) {
	  /* none exist, none matched */
	  db_free_result();
	  return 0;
     }
     
     tmp = (u64_t*) my_malloc (db_num_rows() * sizeof(u64_t));
     if (!tmp) {
	  trace(TRACE_ERROR, "%s,%s: not enough memory\n",
		__FILE__, __FUNCTION__);
	  return (-2);
     }

     for (i = 0; i < (unsigned) db_num_rows(); i++) {
	  result_string = db_get_result(i, 0);
	  if (result_string) {
	       if (regexec(preg, result_string, 0, NULL, 0) == 0) {
		    tmp[*nr_mailboxes] = 
			 strtoull(db_get_result(i, 1), NULL, 10);
		    (*nr_mailboxes)++;
	       }
	  }
     }
     db_free_result();
     
     if (*nr_mailboxes == 0) {
	  /* none exist, none matched */
	  my_free(tmp);
	  return 0;
     }

     *mailboxes = (u64_t*) realloc (tmp, *nr_mailboxes * sizeof(u64_t));
     if (!*mailboxes) {
	  trace(TRACE_ERROR, "%s,%s: realloc failed",
		__FILE__, __FUNCTION__);
	  my_free(tmp);
	  return -2;
     }
     
     return 1;
}
     
int shared_mailbox_calculate_quotum_used(u64_t mailbox_id) 
{
     char *result_string;
     u64_t quotum;

     if (shared_mailbox_is_shared(mailbox_id) != 1) {
	  trace(TRACE_ERROR, "%s,%s: this function should only be called for "
		"shared mailboxes!", __FILE__, __FUNCTION__);
	  return -1;
     }
     
     snprintf(query, DEF_QUERYSIZE, 
	      "SELECT SUM(pm.messagesize) "
	      "FROM physmessage pm, messages msg "
	      "WHERE msg.mailbox_idnr = '%llu' "
	      "AND msg.physmessage_id = pm.id "
	      "AND msg.status < 2", mailbox_id);
     
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: could not execute query", 
		__FILE__, __FUNCTION__);
	  return -1;
     }

     if (db_num_rows() < 1) {
	  trace(TRACE_INFO, "%s,%s: no result from SUM. Assuming "
		"mailbox is empty", __FILE__, __FUNCTION__);
	  quotum = 0;
     } else {
	  result_string = db_get_result(0, 0);
	  quotum = result_string ? strtoull(result_string, NULL, 10) : 0;
     }
     db_free_result();
     
     snprintf(query, DEF_QUERYSIZE,
	      "UPDATE shared_mailbox SET curmail_size = '%llu' "
	      "WHERE mailbox_id = '%llu'", quotum, mailbox_id);
     
 
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: could not execute query", 
		__FILE__, __FUNCTION__);
	  return -1;
     }

     return 1;
}     
     
int shared_mailbox_get_maxmail_size(u64_t mailbox_id, u64_t *maxmail_size)
{
     char *result_string;

     assert(maxmail_size != NULL);

     snprintf(query, DEF_QUERYSIZE,
	      "SELECT shmbx.maxmail_size FROM shared_mailbox shmbx "
	      "WHERE shmbx.mailbox_id = '%llu'", mailbox_id);

     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: error getting maxmail_size from shared mailbox",
		__FILE__, __FUNCTION__);
	  return -1;
     }

     result_string = db_get_result(0, 0);
     *maxmail_size = result_string ? strtoull(result_string, NULL, 10): 0;
     return 1;
}

int shared_mailbox_get_curmail_size(u64_t mailbox_id, u64_t *curmail_size)
{
     char *result_string;

     assert(curmail_size != NULL);

     snprintf(query, DEF_QUERYSIZE,
	      "SELECT shmbx.curmail_size FROM shared_mailbox shmbx "
	      "WHERE shmbx.mailbox_id = '%llu'", mailbox_id);

     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: error getting curmail_size from shared mailbox",
		__FILE__, __FUNCTION__);
	  return -1;
     }

     result_string = db_get_result(0, 0);
     *curmail_size = result_string ? strtoull(result_string, NULL, 10): 0;
     return 1;
}

/* subscribe or unsubscribe to/from a mailbox */
int shared_mailbox_subscribe(u64_t mailbox_id, u64_t user_id, int do_subscribe)
{
     u64_t shared_mailbox_id;

     assert (do_subscribe == 0 || do_subscribe == 1);

     /* get the shared mailbox id */
     if (find_shared_mailbox_id(mailbox_id, &shared_mailbox_id) != 1) {
	  trace(TRACE_ERROR, "%s,%s: error finding id of shared mailbox [%llu]",
		__FILE__, __FUNCTION__, mailbox_id);
	  return -1;
     }

     snprintf(query, DEF_QUERYSIZE,
	      "UPDATE shared_mailbox_access SET "
	      "is_subscribed = '%i' "
	      "WHERE shared_mailbox_id = '%llu' "
	      "AND user_id = '%llu'", 
	      do_subscribe, shared_mailbox_id, user_id);
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: error setting subscribe status on shared "
		"mailbox [%llu] for user [%llu] to [%d]", 
		__FILE__, __FUNCTION__, 
		shared_mailbox_id, user_id, do_subscribe);
	  return -1;
     }
     return 1;

}

int find_shared_mailbox_id(u64_t mailbox_id, u64_t *shared_mailbox_id)
{
     char *result_string;

     assert(shared_mailbox_id != NULL);
     *shared_mailbox_id = 0;

     snprintf(query, DEF_QUERYSIZE,
	      "SELECT id FROM shared_mailbox "
	      "WHERE mailbox_id = '%llu'", mailbox_id);
     
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: error getting shared mailbox id "
		"from database", __FILE__, __FUNCTION__);
	  return -1;
     }

     if (db_num_rows() < 1) {
	  trace(TRACE_INFO, 
		"%s,%s: no shared mailbox found normal mailbox [%llu]",
		__FILE__, __FUNCTION__, mailbox_id);
	  db_free_result();
	  return 0;
     }
     result_string = db_get_result(0, 0);
     *shared_mailbox_id = 
	  result_string ? strtoull(result_string, NULL, 10): 0;
     db_free_result();
     return 1;
}

int shared_mailbox_create_mailbox_client(const char *name, 
					 u64_t client_id, 
					 u64_t maxmail_size, 
					 u64_t *mailbox_idnr) {
     u64_t tmp_mbx_id = 0;
     u64_t shmbx_id = 0;
     
     assert(name != NULL);
     assert(mailbox_idnr != NULL);

     /* create a new 'normal' mailbox */
     if (db_createmailbox(name, SHARED_MAILBOX_OWNER, &tmp_mbx_id) < 0) {
	  trace(TRACE_ERROR, "%s,%s: error creating normal mailbox",
		__FILE__, __FUNCTION__);
	  return -1;
     }

     /* create an entry in the shared mailbox table */
     if (insert_shared_mailbox_record(tmp_mbx_id, maxmail_size, 
				      &shmbx_id) == -1) {
	  trace(TRACE_ERROR, "%s,%s: error creating shared mailbox",
		__FILE__, __FUNCTION__);
	  db_delete_mailbox(tmp_mbx_id, 0);
	  return -1;
     }

     /* create the shared_mailbox_access entries */
     if (insert_shared_mailbox_access_entries_client(name,
						     shmbx_id, 
						     client_id) == -1) {
	  trace(TRACE_ERROR, "%s, %s: error creating shared mailbox access "
		"records", __FILE__, __FUNCTION__);
	  db_delete_mailbox(tmp_mbx_id, 0);
	  delete_shared_mailbox_record(shmbx_id);
	  return -1;
     }
     
     *mailbox_idnr = tmp_mbx_id;
     return 1;
}

int insert_shared_mailbox_record(u64_t mailbox_id, u64_t maxmail_size,
				 u64_t *shared_mailbox_id)
{
     assert(shared_mailbox_id != NULL);

     snprintf(query, DEF_QUERYSIZE, 
	     "INSERT INTO shared_mailbox "
	     "(mailbox_id, maxmail_size) "
	     "VALUES ('%llu', '%llu')", mailbox_id, maxmail_size);

     if (db_query(query) < -1) {
	  trace(TRACE_ERROR, "%s,%s: error creating shared mailbox entry",
		__FILE__, __FUNCTION__);
	  return -1;
     }
     
     *shared_mailbox_id = db_insert_result("shared_mailbox_id");
     return 1;
}

int insert_shared_mailbox_access_entries_client(const char *name,
						u64_t shmbx_id, 
						u64_t client_id)
{
     u64_t *user_ids;
     unsigned num_users;
     unsigned i;
     
     /* get all users for this client id */
     if (db_get_users_from_clientid(client_id, &user_ids, &num_users) == -1) {
	  trace(TRACE_ERROR, "%s,%s: error getting users for client id [%llu]",
		__FILE__, __FUNCTION__, client_id);
	  return -1;
     }
     /* add an entry for all users */
     for (i = 0; i < num_users; i++) {
	  if (insert_shared_mailbox_access_entry(name, shmbx_id, 
						 user_ids[i]) == -1) {
	       trace(TRACE_ERROR, "%s,%s: error inserting shared mailbox "
		     "access entry. Trying to clean up before returning",
		     __FILE__, __FUNCTION__);
	       /* try to clean up */
	       my_free(user_ids);
	       snprintf(query, DEF_QUERYSIZE,
		       "DELETE from shared_mailbox_access "
		       "WHERE shmbx_id = '%llu'", shmbx_id);
	       if (db_query(query) == -1) {
		    trace(TRACE_ERROR,"%s,%s: clean up failed. Please run "
			  "dbmail-maintenance", __FILE__, __FUNCTION__);
	       } else 
		    trace(TRACE_ERROR,"%s,%s: clean up succeeded",
			  __FILE__, __FUNCTION__);
	       return -1;
	  }
     }
     my_free(user_ids);
     return 1;
}

int insert_shared_mailbox_access_entry(const char *name,
				       u64_t shmbx_id, u64_t user_id) 
{
     u64_t mailbox_id;

     if (shared_mailbox_find(name, user_id, &mailbox_id) == -1) {
	  trace(TRACE_ERROR,"%s,%s: error finding shared mailbox access entry "
		"for shared mailbox [%llu] with name [%s] for user_id [%llu]",
		__FILE__, __FUNCTION__, shmbx_id, name, user_id);
	  return -1;
     }
     snprintf(query, DEF_QUERYSIZE, 
	      "INSERT into shared_mailbox_access "
	      "(shared_mailbox_id, user_id, lookup_flag, read_flag "
	      "insert_flag, write_flag, is_subscribed "
	      "VALUES ('%llu', '%llu', '1', '1', "
	      "'1', '1', '0')", shmbx_id, user_id);
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: error inserting shared mailbox "
		"entry for shared mailbox [%llu], user [%llu]",
		__FILE__, __FUNCTION__, shmbx_id, user_id);
	  return -1;
     }

     return 1;
}
	  
int delete_shared_mailbox_record(u64_t shmbx_id) 
{
     snprintf(query, DEF_QUERYSIZE,
	      "DELETE FROM shared_mailbox WHERE id = '%llu'",
	      shmbx_id);
     
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: error deleting shared mailbox",
		__FILE__, __FUNCTION__);
	  return -1;
     }
     return 1;
}
