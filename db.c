/* $Id$ */
/*
  Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

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
*/

/**
 * \file db.c
 *
 * implement database functionality. This used to split out
 * between MySQL and PostgreSQL, but this is now integrated. 
 * Only the actual calls to the database APIs are still in
 * place in the mysql/ and pgsql/ directories
 */

#include "db.h"
#include "dbmail.h"
#include "auth.h"
#include "misc.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>
#include <assert.h>

static const char *db_flag_desc[] = {
    "seen_flag",
    "answered_flag",
    "deleted_flag",
    "flagged_flag",
    "draft_flag",
    "recent_flag"
};

/** list of tables used in dbmail */
const char *DB_TABLENAMES[DB_NTABLES] = {
    "users", "aliases", "mailboxes", "messages", "physmessage",
	"messageblks"
};

/* size of buffer for writing messages to a client */
#define WRITE_BUFFER_SIZE 2048

/** static functions */
/** set quotum used for user user_idnr to curmail_size */
static int db_set_quotum_used(u64_t user_idnr, u64_t curmail_size);
/** list all mailboxes owned by user owner_idnr */
static int db_list_mailboxes_by_regex(u64_t owner_idnr, int only_subscribed,
			     regex_t *preg,
			     u64_t **mailboxes, unsigned int *nr_mailboxes);
/** get size of a message */
static int db_get_message_size(u64_t message_idnr, u64_t *message_size);
/** find a mailbox with a specific owner */
static int db_findmailbox_owner(const char *name, u64_t owner_idnr, 
				u64_t *mailbox_idnr);

int db_get_physmessage_id(u64_t message_idnr, u64_t *physmessage_id)
{
    char *query_result = (char *) NULL;
    
    assert(physmessage_id != NULL);
    *physmessage_id = 0;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT physmessage_id FROM messages "
	     "WHERE message_idnr = '%llu'", message_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: error getting physmessage_id",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    if (db_num_rows() < 1) {
	db_free_result();
	return 0;
    }

    query_result = db_get_result(0, 0);
    if (query_result)
	*physmessage_id = strtoull(query_result, NULL, 10);
   
    db_free_result();

    return 1;
}


int db_get_quotum_used(u64_t user_idnr, u64_t *curmail_size)
{
     assert(curmail_size != NULL);
     
     snprintf(query, DEF_QUERYSIZE,
	      "SELECT curmail_size FROM users "
	      "WHERE user_idnr = '%llu'", user_idnr);
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: error getting used quotum for "
		"user [%llu]", __FILE__, __FUNCTION__, user_idnr);
	  return -1;
     }
     
    *curmail_size = strtoull(db_get_result(0, 0), NULL, 10);
    db_free_result();
    return 1;
}

/* this is a local (static) function */
int db_set_quotum_used(u64_t user_idnr, u64_t curmail_size)
{
    snprintf(query, DEF_QUERYSIZE,
	     "UPDATE users SET curmail_size = '%llu' "
	     "WHERE user_idnr = '%llu'", curmail_size, user_idnr);
    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: error setting used quotum of "
	      "[%llu] for user [%llu]", curmail_size, user_idnr);
	return -1;
    }
    return 0;
}

int db_calculate_quotum_all()
{
    u64_t *user_idnrs;	/**< will hold all user_idnr for which the quotum
			   has to be set again */
    u64_t *curmail_sizes;  /**< will hold current mailsizes */
    int i;
    int n;  /**< number of records returned */
    int result;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT usr.user_idnr, sum(pm.messagesize), usr.curmail_size "
	     "FROM users usr, mailboxes mbx, messages msg, physmessage pm "
	     "WHERE pm.id = msg.physmessage_id "
	     "AND msg.mailbox_idnr = mbx.mailbox_idnr "
	     "AND mbx.owner_idnr = usr.user_idnr "
	     "AND msg.status < '2' "
	     "GROUP BY usr.user_idnr, usr.curmail_size "
	     "HAVING sum(pm.messagesize) <> usr.curmail_size");

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: error findng used quota",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    n = db_num_rows();
    result = n;
    if (n == 0) {
	trace(TRACE_DEBUG, "%s,%s: found no quota in need of update",
	      __FILE__, __FUNCTION__);
	return 0;
    }

    user_idnrs = (u64_t *) my_malloc(n * sizeof(u64_t));
    curmail_sizes = (u64_t *) my_malloc(n * sizeof(u64_t));
    if (user_idnrs == NULL || curmail_sizes == NULL) {
	trace(TRACE_ERROR,
	      "%s,%s: malloc failed. probably out of memory..", __FILE__,
	      __FUNCTION__);
	return -2;
    }
    for (i = 0; i < n; i++) {
	user_idnrs[i] = strtoull(db_get_result(i, 0), NULL, 10);
	curmail_sizes[i] = strtoull(db_get_result(i, 1), NULL, 10);
    }
    db_free_result();

    /* now update the used quotum for all users that need to be updated */
    for (i = 0; i < n; i++) {
	if (db_set_quotum_used(user_idnrs[i], curmail_sizes[i]) == -1) {
	    trace(TRACE_ERROR, "%s,%s: error setting quotum used, "
		  "trying to continue", __FILE__, __FUNCTION__);
	    result = -1;
	}
    }

    /* free allocated memory */
    my_free(user_idnrs);
    my_free(curmail_sizes);
    return result;
}


int db_calculate_quotum_used(u64_t user_idnr)
{
    u64_t quotum = 0;
    char *query_result = (char *) NULL;

    snprintf(query, DEF_QUERYSIZE, "SELECT SUM(pm.messagesize) "
	     "FROM physmessage pm, messages m, mailboxes mb "
	     "WHERE m.physmessage_id = pm.id "
	     "AND m.mailbox_idnr = mb.mailbox_idnr "
	     "AND mb.owner_idnr = '%llu' " "AND m.status < 2", user_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not execute query", 
	      __FILE__, __FUNCTION__);
	return -1;
    }
    if (db_num_rows() < 1)
	trace(TRACE_WARNING, "%s,%s: SUM did not give result, "
	      "assuming empty mailbox", __FILE__, __FUNCTION__);
    else {
	query_result = db_get_result(0, 0);
	if (query_result)
	    quotum = strtoull(query_result, NULL, 10);
    }
    db_free_result();
    trace(TRACE_DEBUG, "%s, found quotum usage of [%llu] bytes",
	  __FUNCTION__, quotum);
    /* now insert the used quotum into the users table */
    if (db_set_quotum_used(user_idnr, quotum) == -1) {
	if (db_query(query) == -1) {
	    trace(TRACE_ERROR,
		  "%s,%s: error setting quotum for user [%llu]",
		  __FILE__, __FUNCTION__, user_idnr);
	    return -1;
	}
    }
    return 0;
}

int db_get_users_from_clientid(u64_t client_id, u64_t **user_ids,
			       unsigned *num_users)
{
	unsigned i;
	char *result_string;
	
	assert (user_ids != NULL);
	assert (num_users != NULL);
	
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT user_idnr FROM users WHERE client_idnr = '%llu'",
		 client_id);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error gettings users for "
		      "client_id [%llu]", __FILE__, __FUNCTION__, client_id);
		return -1;
	}
	*num_users = db_num_rows();
	*user_ids = (u64_t*) my_malloc(*num_users * sizeof(u64_t));
	if (*user_ids == NULL) {
		trace(TRACE_ERROR, "%s,%s: error allocating memory, probably "
		      "out of memory", __FILE__, __FUNCTION__);
		db_free_result();
		return -2;
	}
	for (i = 0; i < *num_users; i++) {
		result_string = db_get_result(i, 0);
		(*user_ids)[i] = result_string ? 
			strtoull(result_string, NULL, 10) : 0;
	}
	db_free_result();
	return 1;
}

char *db_get_deliver_from_alias(const char *alias)
{
    char *deliver = NULL;
    const char *query_result = NULL;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT deliver_to FROM aliases WHERE alias = '%s'", alias);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not execute query",
	      __FILE__, __FUNCTION__);
	return NULL;
    }

    if (db_num_rows() == 0) {
	/* no such user */
	db_free_result();
	return "";
    }

    query_result = db_get_result(0, 0);
    if (!query_result) {
	db_free_result();
	return NULL;
    }

    if (!(deliver = (char *) my_malloc(strlen(query_result) + 1))) {
	trace(TRACE_ERROR, "%s,%s: out of mem", __FILE__, __FUNCTION__);
	db_free_result();
	return NULL;
    }

    strncpy(deliver, query_result, strlen(query_result) + 1);
    db_free_result();
    return deliver;
}

int db_addalias(u64_t user_idnr, const char *alias, u64_t clientid)
{
    /* check if this alias already exists */
    snprintf(query, DEF_QUERYSIZE,
	     "SELECT alias_idnr FROM aliases "
	     "WHERE lower(alias) = lower('%s') AND deliver_to = '%llu' "
	     "AND client_idnr = '%llu'", alias, user_idnr, clientid);

    if (db_query(query) == -1) {
	/* query failed */
	trace(TRACE_ERROR, "%s,%s: query for searching alias failed",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    if (db_num_rows() > 0) {
	trace(TRACE_INFO,
	      "%s,%s: alias [%s] for user [%llu] already exists", __FILE__,
	      __FUNCTION__, alias, user_idnr);

	db_free_result();
	return 1;
    }

    db_free_result();
    snprintf(query, DEF_QUERYSIZE,
	     "INSERT INTO aliases (alias,deliver_to,client_idnr) "
	     "VALUES ('%s','%llu','%llu')", alias, user_idnr, clientid);

    if (db_query(query) == -1) {
	/* query failed */
	trace(TRACE_ERROR, "%s,%s: query for adding alias failed",
	      __FILE__, __FUNCTION__);
	return -1;
    }
    return 0;
}

int db_addalias_ext(const char *alias,
		    const char *deliver_to, u64_t clientid)
{
    /* check if this alias already exists */
    if (clientid != 0) {
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT alias_idnr FROM aliases "
		 "WHERE lower(alias) = lower('%s') AND "
		 "lower(deliver_to) = lower('%s') "
		 "AND client_idnr = '%llu'", alias, deliver_to, clientid);
    } else {
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT alias_idnr FROM aliases "
		 "WHERE lower(alias) = lower('%s') "
		 "AND lower(deliver_to) = lower('%s') ",
		 alias, deliver_to);
    }

    if (db_query(query) == -1) {
	/* query failed */
	trace(TRACE_ERROR, "%s,%s: query for searching alias failed",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    if (db_num_rows() > 0) {
	trace(TRACE_INFO, "%s,%s: alias [%s] --> [%s] already exists",
	      __FILE__, __FUNCTION__, alias, deliver_to);

	db_free_result();
	return 1;
    }
    db_free_result();

    snprintf(query, DEF_QUERYSIZE,
	     "INSERT INTO aliases (alias,deliver_to,client_idnr) "
	     "VALUES ('%s','%s','%llu')", alias, deliver_to, clientid);
    if (db_query(query) == -1) {
	/* query failed */
	trace(TRACE_ERROR, "%s,%s: query for adding alias failed",
	      __FILE__, __FUNCTION__);
	return -1;
    }
    return 0;
}

int db_removealias(u64_t user_idnr, const char *alias)
{
    snprintf(query, DEF_QUERYSIZE,
	     "DELETE FROM aliases WHERE deliver_to='%llu' "
	     "AND lower(alias) = lower('%s')", user_idnr, alias);

    if (db_query(query) == -1) {
	/* query failed */
	trace(TRACE_ERROR, "%s,%s: query failed", __FILE__, __FUNCTION__);
	return -1;
    }
    return 0;
}

int db_removealias_ext(const char *alias, const char *deliver_to)
{
    snprintf(query, DEF_QUERYSIZE,
	     "DELETE FROM aliases WHERE lower(deliver_to) = lower('%s') "
	     "AND lower(alias) = lower('%s')", deliver_to, alias);
    if (db_query(query) == -1) {
	/* query failed */
	trace(TRACE_ERROR, "%s,%s: query failed", __FILE__, __FUNCTION__);
	return -1;
    }
    return 0;
}
int db_get_notify_address(u64_t user_idnr, char **notify_address)
{
    char *query_result = NULL;
    *notify_address = NULL;
    snprintf(query, DEF_QUERYSIZE, "SELECT notify_address "
	     "FROM auto_notifications WHERE user_idnr = %llu", user_idnr);

    if (db_query(query) == -1) {
	/* query failed */
	trace(TRACE_ERROR, "%s,%s: query failed", __FILE__, __FUNCTION__);
	return -1;
    }
    if (db_num_rows() > 0) {
	query_result = db_get_result(0, 0);
	if (query_result && strlen(query_result) > 0) {
	    if (!
		(*notify_address =
		 (char *) my_malloc(strlen(query_result) + 1))) {
		trace(TRACE_ERROR, "%s,%s: could not allocate", __FILE__,
		      __FUNCTION__);
		db_free_result();
		return -2;
	    }
	    snprintf(*notify_address, strlen(query_result) + 1, 
		     "%s", query_result);
	    trace(TRACE_DEBUG, "%s,%s: found address [%s]",
		  __FILE__, __FUNCTION__, *notify_address);
	}
    }

    db_free_result();
    return 0;
}

int db_get_reply_body(u64_t user_idnr, char **reply_body)
{
    char *query_result;
    *reply_body = NULL;

    snprintf(query, DEF_QUERYSIZE, "SELECT reply_body FROM auto_replies "
	     "WHERE user_idnr = %llu", user_idnr);
    if (db_query(query) == -1) {
	/* query failed */
	trace(TRACE_ERROR, "%s,%s: query failed", __FILE__, __FUNCTION__);
	return -1;
    }
    if (db_num_rows() > 0) {
	query_result = db_get_result(0, 0);
	if (query_result && strlen(query_result) > 0) {
	    if (!
		(*reply_body =
		 (char *) my_malloc(strlen(query_result) + 1))) {
		trace(TRACE_ERROR, "%s,%s: could not allocate", __FILE__,
		      __FUNCTION__);
		db_free_result();
		return -2;
	    }
	    snprintf(*reply_body, strlen(query_result) + 1,
		     "%s", query_result);
	    trace(TRACE_DEBUG, "%s,%s: found reply_body [%s]",
		  __FILE__, __FUNCTION__, *reply_body);
	}
    }
    db_free_result();
    return 0;
}

u64_t db_get_mailbox_from_message(u64_t message_idnr)
{
     char *result_string;
     u64_t mailbox_idnr;

     snprintf(query, DEF_QUERYSIZE,
	     "SELECT mailbox_idnr FROM messages "
	     "WHERE message_idnr = '%llu'", message_idnr);

     if (db_query(query) == -1) {
	  /* query failed */
	  trace(TRACE_ERROR, "%s,%s: query failed", __FILE__, __FUNCTION__);
	  return -1;
     }
     
     if (db_num_rows() < 1) {
	  trace(TRACE_DEBUG, "%s,%s: No mailbox found for message",
		__FILE__, __FUNCTION__);
	  db_free_result();
	  return 0;
     }
     result_string = db_get_result(0, 0);
     mailbox_idnr = (result_string) ? strtoull(result_string, NULL, 10) : 0;
     db_free_result();
     return mailbox_idnr;
}


u64_t db_get_useridnr(u64_t message_idnr)
{
    char *query_result;
    u64_t user_idnr;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT mailboxes.owner_idnr FROM mailboxes, messages "
	     "WHERE mailboxes.mailbox_idnr = messages.mailbox_idnr "
	     "AND messages.message_idnr = '%llu'", message_idnr);
    if (db_query(query) == -1) {
	/* query failed */
	trace(TRACE_ERROR, "%s,%s: query failed", __FILE__, __FUNCTION__);
	return -1;
    }

    if (db_num_rows() < 1) {
	trace(TRACE_DEBUG, "%s,%s: No owner found for message",
	      __FILE__, __FUNCTION__);
	db_free_result();
	return 0;
    }
    query_result = db_get_result(0, 0);
    user_idnr = (query_result) ? strtoull(query_result, NULL, 10) : 0;
    db_free_result();
    return user_idnr;
}

int db_insert_physmessage(u64_t *physmessage_id) 
{
	timestring_t timestring;

	*physmessage_id = 0;

	create_current_timestring(&timestring);

    
	snprintf(query, DEF_QUERYSIZE,
		 "INSERT INTO physmessage (messagesize, internal_date) "
		 "VALUES ('0', '%s')", timestring);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: query failed", __FILE__, __FUNCTION__);
		return -1;
	}
	
	*physmessage_id = db_insert_result("physmessage_id");
	
	return 1;
}

int db_update_physmessage(u64_t physmessage_id, u64_t message_size,
			  u64_t rfc_size) 
{
     snprintf(query, DEF_QUERYSIZE,
	      "UPDATE physmessage SET messagesize = '%llu', "
	      "rfcsize = '%llu' WHERE id = '%llu'",
	      message_size, rfc_size, physmessage_id);
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: error executing query",
		__FILE__, __FUNCTION__);
	  return -1;
     }
     return 1;
}
     
int db_insert_message_with_physmessage(u64_t physmessage_id,
				       u64_t user_idnr,
				       const char *deliver_to_mailbox,
				       const char *unique_id,
				       u64_t *message_idnr)
{
     u64_t deliver_to_mailbox_id;
     int result = 0;

     assert(message_idnr != NULL);

     if (deliver_to_mailbox)
	  result = db_findmailbox(deliver_to_mailbox, user_idnr,
				  &deliver_to_mailbox_id);
     else
	  result = db_findmailbox("INBOX", user_idnr, &deliver_to_mailbox_id);
     if (result < 0) {
	  trace(TRACE_ERROR, "%s,%s: error finding mailbox [%s] for user "
		"[%llu]", __FILE__, __FUNCTION__, deliver_to_mailbox, 
		user_idnr);
	  return -1;
     }
     if (deliver_to_mailbox_id == 0) {
	  trace(TRACE_ERROR, "%s,%s: mailbox [%s] for user [%llu] could not "
		"be found!", __FILE__, __FUNCTION__, 
		deliver_to_mailbox, user_idnr);
	  return -1;
     }

     snprintf(query, DEF_QUERYSIZE, "INSERT INTO "
	      "messages(mailbox_idnr, physmessage_id, unique_id,"
	      "recent_flag, status) "
	      "VALUES ('%llu', '%llu', '%s', '1', '000')",
	      deliver_to_mailbox_id,
	      physmessage_id, unique_id ? unique_id : "");

    if (db_query(query) == -1) {
	 trace(TRACE_ERROR, "%s,%s: query failed", __FILE__, __FUNCTION__);
	 return -1;
    }
    
    *message_idnr = db_insert_result("message_idnr");
    return 1;
}

int db_insert_message(u64_t user_idnr,
			const char *mailbox,
			int create_or_error_mailbox,
			const char *unique_id,
			u64_t *message_idnr)
{
    u64_t mailboxid;
    u64_t physmessage_id;
    int result;

    if (!mailbox)
	 mailbox = "INBOX";

    switch (create_or_error_mailbox) {
	 case CREATE_IF_MBOX_NOT_FOUND:
	     result = db_find_create_mailbox(mailbox, user_idnr, &mailboxid);
	     break;
	 case ERROR_IF_MBOX_NOT_FOUND:
	 default:
	     result = db_findmailbox(mailbox, user_idnr, &mailboxid);
	     break;
    }

    if (result == -1) {
	 trace(TRACE_ERROR, "%s,%s: error finding and/or creating mailbox [%s]",
	       __FILE__, __FUNCTION__, mailbox);
	 return -1;
    }
    if (mailboxid == 0) {
	 trace(TRACE_WARNING, "%s,%s: mailbox [%s] could not be found!",
	       __FILE__, __FUNCTION__, mailbox);
	 return -1;
    }

    /* insert a new physmessage entry */
    if (db_insert_physmessage(&physmessage_id) == -1) {
	 trace(TRACE_ERROR, "%s,%s: error inserting physmessage",
	       __FILE__, __FUNCTION__);
	 return -1;
    }

    /* now insert an entry into the messages table */
    snprintf(query, DEF_QUERYSIZE, "INSERT INTO "
	     "messages(mailbox_idnr, physmessage_id, unique_id,"
	     "recent_flag, status) "
	     "VALUES ('%llu', '%llu', '%s', '1', '005')",
	     mailboxid,
	     physmessage_id, unique_id ? unique_id : "");
    if (db_query(query) == -1) {
	 trace(TRACE_STOP, "%s,%s: query failed", __FILE__, __FUNCTION__);
    }

    *message_idnr = db_insert_result("message_idnr");
    return 1;
}

int db_update_message(u64_t message_idnr, const char *unique_id,
		      u64_t message_size, u64_t rfc_size)
{
    u64_t physmessage_id = 0;

    /* update the fields in the messages table */
    snprintf(query, DEF_QUERYSIZE,
	     "UPDATE messages SET unique_id='%s', status = '000' "
	     "WHERE message_idnr='%llu'", unique_id, message_idnr);
    if (db_query(query) == -1) {
	trace(TRACE_STOP, "%s,%s: dbquery failed", __FILE__, __FUNCTION__);
    }

    /* update the fields in the physmessage table */
    if (db_get_physmessage_id(message_idnr, &physmessage_id) == -1) {
	trace(TRACE_ERROR,
	      "%s,%s: could not find physmessage_id of message", __FILE__,
	      __FUNCTION__);
	return -1;
    }
    
    if (db_update_physmessage(physmessage_id, message_size, rfc_size) == -1) {
	 trace(TRACE_ERROR, "%s,%s: error updating physmessage [%llu]. "
	       "The database might be inconsistent. Run dbmail-maintenance",
	       __FILE__, __FUNCTION__, physmessage_id);
    } 
        
    if (db_calculate_quotum_used(db_get_useridnr(message_idnr)) == -1) {	 
	    trace (TRACE_ERROR, "%s,%s: error calculating quotum "
		   "used for user [%llu]. Database might be " 
		   "inconsistent. run dbmail-maintenance",
		   __FILE__, __FUNCTION__, db_get_useridnr(message_idnr));
	    return -1;
    }
    return 0;
}

int db_update_message_multiple(const char *unique_id, u64_t message_size,
			       u64_t rfc_size)
{
    u64_t *uids;
    int n, i;
    char new_unique[UID_SIZE];
    char *query_result;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT message_idnr FROM messages WHERE "
	     "unique_id = '%s'", unique_id);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: error executing query\n",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    n = db_num_rows();
    if (n <= 0) {
	trace(TRACE_INFO, "%s,%s: nothing to update (?)",
	      __FILE__, __FUNCTION__);
	db_free_result();
	return 0;
    }
    if ((uids = (u64_t *) my_malloc(n * sizeof(u64_t))) == 0) {
	trace(TRACE_ERROR, "%s,%s: out of memory", __FILE__, __FUNCTION__);
	db_free_result();
	return -1;
    }

    for (i = 0; i < n; i++) {
	query_result = db_get_result(i, 0);
	uids[i] = (query_result && strlen(query_result)) ?
	    strtoull(query_result, NULL, 10) : 0;
    }
    db_free_result();

    /* now update for each message */
    for (i = 0; i < n; i++) {
	 create_unique_id(new_unique, uids[i]);
	 if (db_update_message(uids[i], new_unique,
			       message_size, rfc_size) == -1) {
	      trace(TRACE_ERROR, "%s,%s error in db_update_message",
		    __FILE__, __FUNCTION__);
	      return -1;
	}
	 trace(TRACE_INFO, "%s,%s: message [%llu] updated, "
	       "[%llu] bytes", uids[i], message_size);

    }
    my_free(uids);
    return 0;
}

int db_insert_message_block_physmessage(const char *block, u64_t block_size,
					u64_t physmessage_id, 
					u64_t *messageblk_idnr)
{
     char *escaped_query = NULL;
     unsigned maxesclen = (READ_BLOCK_SIZE + 1) * 2 + DEF_QUERYSIZE;
     unsigned startlen = 0;
     unsigned esclen = 0;
     
     assert(messageblk_idnr != NULL);
     *messageblk_idnr = 0;
     
     if (block == NULL) {
	  trace(TRACE_ERROR,
		"%s,%s: got NULL as block. Insertion not possible",
		__FILE__, __FUNCTION__);
	  return -1;
     }

     if (block_size > READ_BLOCK_SIZE) {
	  trace(TRACE_ERROR, "%s,%s:blocksize [%llu], maximum is [%llu]",
		__FILE__, __FUNCTION__, block_size, READ_BLOCK_SIZE);
	  return -1;
     }
     
     escaped_query = (char *) my_malloc(sizeof(char) * maxesclen);
     if (!escaped_query) {
	  trace(TRACE_ERROR, "%s,%s: not enough memory", __FILE__,
		__FUNCTION__);
	  return -1;
     }
     
     startlen = snprintf(escaped_query, maxesclen, "INSERT INTO messageblks"
			 "(messageblk,blocksize, physmessage_id) VALUES ('");

     /* escape & add data */
     esclen = db_escape_string(&escaped_query[startlen], block, block_size);
     
     snprintf(&escaped_query[esclen + startlen],
	      maxesclen - esclen - startlen, "', '%llu', '%llu')",
	      block_size, physmessage_id);
     
     if (db_query(escaped_query) == -1) {
	  my_free(escaped_query);
	  
	  trace(TRACE_ERROR, "%s,%s: dbquery failed\n", __FILE__,
		__FUNCTION__);
	  return -1;
     }

    /* all done, clean up & exit */
    my_free(escaped_query);
    
    *messageblk_idnr = db_insert_result("messageblk_idnr");
    return 1;
}    
     
int db_insert_message_block(const char *block, u64_t block_size,
			    u64_t message_idnr, u64_t *messageblk_idnr)
{
    u64_t physmessage_id;

    assert(messageblk_idnr != NULL);
    *messageblk_idnr = 0;
    if (block == NULL) {
	trace(TRACE_ERROR,
	      "%s,%s: got NULL as block, insertion not possible\n",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    if (db_get_physmessage_id(message_idnr, &physmessage_id) < 0) {
	 trace(TRACE_ERROR, "%s,%s: error getting physmessage_id",
	       __FILE__, __FUNCTION__);
	 return -1;
    }
    
    if (db_insert_message_block_physmessage(block, block_size, physmessage_id, 
					    messageblk_idnr) < 0) {
	 trace(TRACE_ERROR, "%s,%s: error inserting messageblks for "
	       "physmessage [%llu]", __FILE__, __FUNCTION__, physmessage_id);
	 return -1;
    }
    return 1;
}

int db_insert_message_block_multiple(const char *unique_id,
				     const char *block, u64_t block_size)
{
    char *escaped_query = NULL;
    unsigned maxesclen = READ_BLOCK_SIZE * 2 + DEF_QUERYSIZE;
    unsigned startlen = 0;
    unsigned esclen = 0;

    if (block == NULL) {
	trace(TRACE_ERROR,
	      "%s,%s(): got NULL as block, insertion not possible\n",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    if (block_size > READ_BLOCK_SIZE) {
	trace(TRACE_ERROR,
	      "%s,%s: blocksize [%llu], maximum is [%llu]",
	      __FILE__, __FUNCTION__, block_size, READ_BLOCK_SIZE);
	return -1;
    }

    escaped_query = (char *) my_malloc(sizeof(char) * maxesclen);
    if (!escaped_query) {
	trace(TRACE_ERROR, "%s,%s: not enough memory",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    startlen = snprintf(escaped_query, maxesclen, "INSERT INTO messageblks"
			"(messageblk,blocksize,physmessage_id) SELECT '");
    /* escape & add data */
    esclen = db_escape_string(&escaped_query[startlen], block, block_size);

    snprintf(&escaped_query[esclen + startlen],
	     maxesclen - esclen - startlen,
	     "', %llu, physmessage_id FROM messages "
	     "WHERE unique_id = '%s'", block_size, unique_id);

    if (db_query(escaped_query) == -1) {
	my_free(escaped_query);
	trace(TRACE_ERROR, "%s,%s: dbquery failed\n",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    /* all done, clean up & exit */
    my_free(escaped_query);
    return 0;
}

int db_rollback_insert(u64_t owner_idnr, const char *unique_id)
{
    u64_t msgid;

    snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr "
	     "FROM messages m, mailboxes mb "
	     "WHERE mb.owner_idnr = '%llu' "
	     "AND m.mailbox_idnr = mb.mailbox_idnr "
	     "AND m.unique_id = '%s'", owner_idnr, unique_id);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not select message-id",
	      __FILE__, __FUNCTION__);
	return -1;
    }
    if (db_num_rows() < 1) {
	trace(TRACE_ERROR, "%s,%s: non-existent message specified",
	      __FILE__, __FUNCTION__);
	db_free_result();
	return 0;
    }

    msgid = strtoull(db_get_result(0, 0), NULL, 10);
    db_free_result();

    /* now just call db_delete_message to delete this message */
    if (db_delete_message(msgid) == -1) {
	trace(TRACE_ERROR, "%s,%s:error deleting message [%llu]",
	      __FILE__, __FUNCTION__, msgid);
	return -1;
    }

    /* recalculate used quotum */
    db_calculate_quotum_used(owner_idnr);

    return 0;
}

int db_log_ip(const char *ip)
{
    u64_t id = 0;
    char *query_result;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT idnr FROM pbsp WHERE ipnumber = '%s'", ip);
    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not access ip-log table "
	      "(pop/imap-before-smtp): %s", __FILE__, __FUNCTION__);
	return -1;
    }

    query_result = db_get_result(0, 0);
    id = query_result ? strtoull(query_result, NULL, 10) : 0;

    db_free_result();

    if (id) {
	/* this IP is already in the table, update the 'since' field */
	snprintf(query, DEF_QUERYSIZE, "UPDATE pbsp "
		 "SET since = CURRENT_TIMESTAMP " "WHERE idnr='%llu'", id);

	if (db_query(query) == -1) {
	    trace(TRACE_ERROR, "%s,%s: could not update ip-log "
		  "(pop/imap-before-smtp)", __FILE__, __FUNCTION__);
	    return -1;
	}
    } else {
	/* IP not in table, insert row */
	snprintf(query, DEF_QUERYSIZE,
		 "INSERT INTO pbsp (since, ipnumber) "
		 "VALUES (CURRENT_TIMESTAMP, '%s')", ip);
	if (db_query(query) == -1) {
	    trace(TRACE_ERROR, "%s,%s: could not log IP number to dbase "
		  "(pop/imap-before-smtp)", __FILE__, __FUNCTION__);
	    return -1;
	}
    }

    trace(TRACE_DEBUG, "%s,%s: ip [%s] logged\n", __FILE__, __FUNCTION__,
	  ip);

    return 0;
}

int db_cleanup_iplog(const char *lasttokeep)
{
    snprintf(query, DEF_QUERYSIZE, "DELETE FROM pbsp WHERE since < '%s'",
	     lasttokeep);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s:%s: error executing query",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    return 0;
}

int db_cleanup()
{
    return db_do_cleanup(DB_TABLENAMES, DB_NTABLES);
}

int db_empty_mailbox(u64_t user_idnr)
{
    u64_t *mboxids = NULL;
    unsigned n, i;
    int result = 0;
    char *query_result;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT mailbox_idnr FROM mailboxes WHERE owner_idnr='%llu'",
	     user_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: error executing query",
	      __FILE__, __FUNCTION__);
	return -1;
    }
    n = db_num_rows();
    if (n == 0) {
	db_free_result();
	trace(TRACE_WARNING,
	      "%s,%s: user [%llu] does not have any mailboxes?", __FILE__,
	      __FUNCTION__, user_idnr);
	return 0;
    }

    mboxids = (u64_t *) my_malloc(n * sizeof(u64_t));
    if (!mboxids) {
	trace(TRACE_ERROR, "%s,%s: not enough memory",
	      __FILE__, __FUNCTION__);
	db_free_result();
	return -2;
    }

    for (i = 0; i < n; i++) {
	query_result = db_get_result(i, 0);
	mboxids[i] = (query_result) ? strtoull(query_result, NULL, 10) : 0;
    }
    db_free_result();

    for (i = 0; i < n; i++) {
	if (db_delete_mailbox(mboxids[i], 1) == -1) {
	    trace(TRACE_ERROR, "%s,%s: error emptying mailbox [%llu]",
		  __FILE__, __FUNCTION__, mboxids[i]);
	    result = -1;
	}
    }
    my_free(mboxids);
    return result;
}

int db_icheck_messageblks(struct list *lost_list)
{
    char *query_result;
    u64_t messageblk_idnr;
    int i, n;
    list_init(lost_list);

    /* get all lost message blocks. Instead of doing all kinds of 
     * nasty stuff here, we let the RDBMS handle all this. Problem
     * is that MySQL cannot handle subqueries. This is handled by
     * a left join select query.
     * This query will select all message block idnr that have no
     * associated physmessage in the physmessage table.
     */
    snprintf(query, DEF_QUERYSIZE,
	     "SELECT mb.messageblk_idnr FROM messageblks mb "
	     "LEFT JOIN physmessage pm ON "
	     "mb.physmessage_id = pm.id "
	     "WHERE pm.id IS NULL");

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: Could not execute query",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    n = db_num_rows();
    if (n < 1) {
	trace(TRACE_DEBUG, "%s,%s: no lost messageblocks",
	      __FILE__, __FUNCTION__);
	db_free_result();
	return 0;
    }

    for (i = 0; i < n; i++) {
	query_result = db_get_result(i, 0);
	if (query_result)
	    messageblk_idnr = strtoull(query_result, NULL, 10);
	else
	    continue;

	trace(TRACE_INFO, "%s,%s: found lost block id [%llu]",
	      __FILE__, __FUNCTION__, messageblk_idnr);
	if (!list_nodeadd(lost_list, &messageblk_idnr, sizeof(u64_t))) {
	    trace(TRACE_ERROR, "%s,%s: could not add block to list",
		  __FILE__, __FUNCTION__);
	    list_freelist(&lost_list->start);
	    db_free_result();
	    return -2;
	}
    }
    db_free_result();
    return 0;
}

int db_icheck_messages(struct list *lost_list)
{
    u64_t message_idnr;
    char *query_result;
    int i, n;

    list_init(lost_list);

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT msg.message_idnr FROM messages msg "
	     "LEFT JOIN mailboxes mbx ON "
	     "msg.mailbox_idnr=mbx.mailbox_idnr "
	     "WHERE mbx.mailbox_idnr IS NULL");

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not execute query",
	      __FILE__, __FUNCTION__);
	return -2;
    }

    n = db_num_rows();
    if (n < 1) {
	trace(TRACE_DEBUG, "%s,%s: no lost messages",
	      __FILE__, __FUNCTION__);
	db_free_result();
	return 0;
    }

    for (i = 0; i < n; i++) {
	query_result = db_get_result(i, 0);
	if (query_result)
	    message_idnr = strtoull(query_result, NULL, 10);
	else
	    continue;

	trace(TRACE_INFO, "%s,%s: found lost message id [%llu]",
	      __FILE__, __FUNCTION__, message_idnr);
	if (!list_nodeadd(lost_list, &message_idnr, sizeof(u64_t))) {
	    trace(TRACE_ERROR, "%s,%s: could not add message to list",
		  __FILE__, __FUNCTION__);
	    list_freelist(&lost_list->start);
	    db_free_result();
	    return -2;
	}
    }
    db_free_result();
    return 0;
}

int db_icheck_mailboxes(struct list *lost_list)
{
    u64_t mailbox_idnr;
    char *query_result;
    int i, n;

    list_init(lost_list);

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT mbx.mailbox_idnr FROM mailboxes mbx "
	     "LEFT JOIN users usr ON "
	     "mbx.owner_idnr=usr.user_idnr "
	     "WHERE usr.user_idnr is NULL");

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not execute query",
	      __FILE__, __FUNCTION__);
	return -2;
    }

    n = db_num_rows();
    if (n < 1) {
	trace(TRACE_DEBUG, "%s,%s: no lost mailboxes",
	      __FILE__, __FUNCTION__);
	db_free_result();
	return 0;
    }

    for (i = 0; i < n; i++) {
	query_result = db_get_result(i, 0);
	if (query_result)
	    mailbox_idnr = strtoull(query_result, NULL, 10);
	else
	    continue;

	trace(TRACE_INFO, "%s,%s: found lost mailbox id [%llu]",
	      __FILE__, __FUNCTION__, mailbox_idnr);
	if (!list_nodeadd(lost_list, &mailbox_idnr, sizeof(u64_t))) {
	    trace(TRACE_ERROR, "%s,%s: could not add mailbox to list",
		  __FILE__, __FUNCTION__);
	    list_freelist(&lost_list->start);
	    db_free_result();
	    return -2;
	}
    }
    db_free_result();
    return 0;
}

int db_icheck_null_physmessages(struct list *lost_list)
{
	u64_t physmessage_id;
	char *result_string;
	unsigned i, n;
	
	list_init(lost_list);
	
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT pm.id FROM physmessage pm "
		 "LEFT JOIN messageblks mbk ON "
		 "pm.id = mbk.physmessage_id "
		 "WHERE mbk.physmessage_id is NULL");
	
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __FUNCTION__);
		return -1;
	}
	
	n = db_num_rows();
	if (n < 1) {
		trace(TRACE_DEBUG, "%s,%s: no null physmessages",
		      __FILE__, __FUNCTION__);
		return 0;
	}

	for (i = 0; i < n; i++) {
		result_string = db_get_result(i, 0);
		if (result_string)
			physmessage_id = strtoull(result_string, NULL, 10);
		else
			continue;
		
		trace(TRACE_INFO, "%s,%s: found empty physmessage_id [%llu]",
		      __FILE__, __FUNCTION__, physmessage_id);
		if (!list_nodeadd(lost_list, &physmessage_id, sizeof(u64_t))) {
			trace(TRACE_ERROR, "%s,%s: could not add physmessage "
			      "to list", __FILE__, __FUNCTION__);
			list_freelist(&lost_list->start);
			db_free_result();
			return -2;
		}
	}
	db_free_result();
	return 0;
}

int db_icheck_null_messages(struct list *lost_list)
{
    u64_t message_idnr;
    char *query_result;
    int i, n;

    list_init(lost_list);

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT msg.message_idnr FROM messages msg "
	     "LEFT JOIN physmessage pm ON "
	     "msg.physmessage_id = pm.id "
	     "WHERE pm.id is NULL");

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not execute query",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    n = db_num_rows();
    if (n < 1) {
	trace(TRACE_DEBUG, "%s,%s: no null messages",
	      __FILE__, __FUNCTION__);
	db_free_result();
	return 0;
    }

    for (i = 0; i < n; i++) {
	query_result = db_get_result(i, 0);
	if (query_result)
	    message_idnr = strtoull(query_result, NULL, 10);
	else
	    continue;

	trace(TRACE_INFO, "%s,%s: found empty message id [%llu]",
	      __FILE__, __FUNCTION__, message_idnr);
	if (!list_nodeadd(lost_list, &message_idnr, sizeof(u64_t))) {
	    trace(TRACE_ERROR, "%s,%s: could not add message to list",
		  __FILE__, __FUNCTION__);
	    list_freelist(&lost_list->start);
	    db_free_result();
	    return -2;
	}
    }
    db_free_result();
    return 0;
}

int db_set_message_status(u64_t message_idnr, int status)
{
    snprintf(query, DEF_QUERYSIZE, "UPDATE messages SET status = %d "
	     "WHERE message_idnr = '%llu'", status, message_idnr);
    return db_query(query);
}

int db_delete_messageblk(u64_t messageblk_idnr)
{
    snprintf(query, DEF_QUERYSIZE,
	     "DELETE FROM messageblks "
	     "WHERE messageblk_idnr = '%llu'", messageblk_idnr);
    return db_query(query);
}

int db_delete_physmessage(u64_t physmessage_id)
{
	snprintf(query, DEF_QUERYSIZE,
		 "DELETE FROM physmessage WHERE id = '%llu'",
		 physmessage_id);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __FUNCTION__);
		return -1;
	}
	
	/* if foreign keys do their work (not with MySQL ISAM tables :( )
	   the next query would not be necessary */
	snprintf(query, DEF_QUERYSIZE,
		 "DELETE FROM messageblks WHERE physmessage_id = '%llu'",
		 physmessage_id);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query. There "
		      "are now messageblocks in the database that have no "
		      "physmessage attached to them. run dbmail-maintenance "
		      "to fix this.",
		      __FILE__, __FUNCTION__);

		return -1;
	}
	
	return 1;
}

int db_delete_message(u64_t message_idnr)
{
    u64_t physmessage_id;

    /* find the physmessage_id of the message */
    if (db_get_physmessage_id(message_idnr, &physmessage_id) < 0) {
	    trace(TRACE_ERROR, "%s,%s: error getting physmessage_id",
		  __FILE__, __FUNCTION__);
	    return -1;
    }
    
    /* now delete the message from the message table */
    snprintf(query, DEF_QUERYSIZE,
	     "DELETE FROM messages WHERE message_idnr = '%llu'",
	     message_idnr);
    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not execute query",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    /* find if there are other messages pointing to the same
       physmessage entry */
    snprintf(query, DEF_QUERYSIZE,
	     "SELECT message_idnr FROM messages "
	     "WHERE physmessage_id = '%llu'", physmessage_id);
    if (db_query(query) == -1) {
	    trace(TRACE_ERROR, "%s,%s: could not execute query",
		  __FILE__, __FUNCTION__);
	    return -1;
    }
    if (db_num_rows() == 0) {
	    /* there are no other messages with the same physmessage left.
	     *  the physmessage records and message blocks now need to
	     * be removed */
	    db_free_result();
	    if (db_delete_physmessage(physmessage_id) < 0) {
		    trace(TRACE_ERROR,"%s,%s: error deleting physmessage",
			  __FILE__, __FUNCTION__);
		    return -1;
	    }
    } else 
	    db_free_result();
    return 1;
}

int db_delete_mailbox(u64_t mailbox_idnr, int only_empty)
{
    unsigned i, n;
    u64_t *message_idnrs;
    u64_t user_idnr = 0;
    int result;

    /* get the user_idnr of the owner of the mailbox */
    result = db_get_mailbox_owner(mailbox_idnr, &user_idnr);
    if (result < 0) {
	    trace(TRACE_ERROR, "%s,%s: cannot find owner of mailbox for "
		  "mailbox [%llu]", __FILE__, __FUNCTION__, mailbox_idnr);
	    return -1;
    }
    if (result == 0) {
	    trace(TRACE_ERROR, "%s,%s: unable to find owner of mailbox "
		  "[%llu]", __FILE__, __FUNCTION__, mailbox_idnr);
	    return 0;
    }

    /* remove the mailbox */
    if (!only_empty) {
	    /* delete mailbox */
	    snprintf(query, DEF_QUERYSIZE,
		     "DELETE FROM mailboxes WHERE mailbox_idnr = '%llu'",
		     mailbox_idnr);
	    
	    if (db_query(query) == -1) {
		    trace(TRACE_ERROR, "%s,%s: could not delete mailbox [%llu]",
			  mailbox_idnr);
		    return -1;
	    }
    }
    
    /* we want to delete all messages from the mailbox. So we
     * need to find all messages in the box */
    snprintf(query, DEF_QUERYSIZE,
	     "SELECT message_idnr FROM messages "
	     "WHERE mailbox_idnr = '%llu'", mailbox_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR,
	      "%s,%s: could not select message ID's for mailbox [%llu]",
	      mailbox_idnr);
	return -1;
    }

    n = db_num_rows();
    if (n == 0) {
	db_free_result();
	trace(TRACE_INFO, "%s,%s: mailbox is empty", __FILE__, __FUNCTION__);
    }

    if (!(message_idnrs = (u64_t *) my_malloc(n * sizeof(u64_t)))) {
	trace(TRACE_ERROR, "%s,%s: error allocating memory",
	      __FILE__, __FUNCTION__);
	return -1;
    }
    for (i = 0; i < n; i++)
	message_idnrs[i] = strtoull(db_get_result(0, 0), NULL, 10);
    db_free_result();
    /* delete every message in the mailbox */
    for (i = 0; i < n; i++) {
	    if (db_delete_message(message_idnrs[i]) == -1) {
		    trace(TRACE_ERROR, "%s,%s: error deleting message [%llu] "
			  "database might be inconsistent. run dbmail-maintenance",
			  __FILE__, __FUNCTION__, message_idnrs[i]);
		    return -1;
	    }
    }
    my_free(message_idnrs);

    /* calculate the new quotum */
    db_calculate_quotum_used(user_idnr);
    return 0;
}

int db_send_message_lines(void *fstream, u64_t message_idnr,
			  long lines, int no_end_dot)
{
    u64_t physmessage_id = 0;
    char *buffer = NULL;
    int buffer_pos;
    char *nextpos, *tmppos = NULL;
    int block_count;
    u64_t rowlength;
    int n;
    char *query_result;

    trace(TRACE_DEBUG, "%s,%s: request for [%d] lines",
	  __FILE__, __FUNCTION__, lines);

    /* first find the physmessage_id */
    snprintf(query, DEF_QUERYSIZE,
	     "SELECT physmessage_id FROM messages "
	     "WHERE message_idnr = '%llu'", message_idnr);
    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: error executing query",
	      __FILE__, __FUNCTION__);
	return 0;
    }
    physmessage_id = strtoull(db_get_result(0, 0), NULL, 10);
    db_free_result();

    memtst((buffer = (char *) my_malloc(WRITE_BUFFER_SIZE * 2)) == NULL);

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT messageblk FROM messageblks "
	     "WHERE physmessage_id='%llu' "
	     "ORDER BY messageblk_idnr ASC", physmessage_id);
    trace(TRACE_DEBUG, "%s,%s: executing query [%s]",
	  __FILE__, __FUNCTION__, query);

    if (db_query(query) == -1) {
	my_free(buffer);
	return 0;
    }

    trace(TRACE_DEBUG, "%s,%s: sending [%d] lines from message [%llu]",
	  __FILE__, __FUNCTION__, lines, message_idnr);

    block_count = 0;
    n = db_num_rows();
    /* loop over all rows in the result set, until the right amount of
     * lines has been read 
     */
    while ((block_count < n)
	   && ((lines > 0) || (lines == -2) || (block_count == 0))) {
	query_result = db_get_result(block_count, 0);
	nextpos = query_result;
	rowlength = (u64_t) db_get_length(block_count, 2);

	/* reset our buffer */
	memset(buffer, '\0', (WRITE_BUFFER_SIZE) * 2);
	buffer_pos = 0;

	while ((*nextpos != '\0') && (rowlength > 0)
	       && ((lines > 0) || (lines == -2) || (block_count == 0))) {
	     
	    if (*nextpos == '\n') {
		/* first block is always the full header 
		   so this should not be counted when parsing
		   if lines == -2 none of the lines should be counted 
		   since the whole message is requested */
		if ((lines != -2) && (block_count != 0))
		    lines--;

		if (tmppos != NULL) {
		     if (*tmppos == '\r') {
			  buffer[buffer_pos++] = *nextpos;
		     } else {
			  buffer[buffer_pos++] = '\r';
			  buffer[buffer_pos++] = *nextpos;
		     }
		} else {
		     buffer[buffer_pos++] = '\r';
		     buffer[buffer_pos++] = *nextpos;
		}
	    } else {
		if (*nextpos == '.') {
		    if (tmppos != NULL) {
			 if (*tmppos == '\n') {
			      buffer[buffer_pos++] = '.';
			      buffer[buffer_pos++] = *nextpos;
			 } else {
			      buffer[buffer_pos++] = *nextpos;
			 }
		    } else {
			 buffer[buffer_pos++] = *nextpos;
		    }
		} else {
		     buffer[buffer_pos++] = *nextpos;
		}
	    }

	    tmppos = nextpos;

	    /* get the next character */
	    nextpos++;
	    rowlength--;

	    if (rowlength % WRITE_BUFFER_SIZE == 0) {	
		 /* purge buffer at every WRITE_BUFFER_SIZE bytes  */
		 fwrite(buffer, sizeof(char), strlen(buffer),
			(FILE *) fstream);
		 /*  cleanup the buffer  */
		 memset(buffer, '\0', (WRITE_BUFFER_SIZE * 2));
		 buffer_pos = 0;
	    }
	}
	/* next block in while loop */
	block_count++;
	trace(TRACE_DEBUG, "%s,%s: getting nextblock [%d]\n", 
	      __FILE__, __FUNCTION__, block_count);
	/* flush our buffer */
	fwrite(buffer, sizeof(char), strlen(buffer), (FILE *) fstream);
    }
    /* delimiter */
    if (no_end_dot == 0)
	fprintf((FILE *) fstream, "\r\n.\r\n");

    db_free_result();
    my_free(buffer);
    return 1;
}

int db_createsession(u64_t user_idnr, PopSession_t * session_ptr)
{
    struct message tmpmessage;
    int message_counter = 0;
    unsigned i;
    char *query_result;
    u64_t inbox_mailbox_idnr;

    list_init(&session_ptr->messagelst);

    if (db_findmailbox("INBOX", user_idnr, &inbox_mailbox_idnr) <= 0) {
	 trace(TRACE_ERROR, "%s,%s: error finding mailbox_idnr of "
	       "INBOX for user [%llu], exiting..",
	       __FILE__, __FUNCTION__, user_idnr);
	 return -1;
    }
    /* query is <2 because we don't want deleted messages 
     * the unique_id should not be empty, this could mean that the message is still being delivered */
    snprintf(query, DEF_QUERYSIZE,
	     "SELECT pm.messagesize, msg.message_idnr, msg.status, "
	     "msg.unique_id FROM messages msg, physmessage pm "
	     "WHERE msg.mailbox_idnr = '%llu' "
	     "AND msg.status < 002 "
	     "AND msg.physmessage_id = pm.id "
	     "AND unique_id != '' order by status ASC",
	     inbox_mailbox_idnr);

    if (db_query(query) == -1) {
	return -1;
    }

    session_ptr->totalmessages = 0;
    session_ptr->totalsize = 0;

    message_counter = db_num_rows();

    if (message_counter < 1) {
	/* there are no messages for this user */
	db_free_result();
	return 1;
    }

    /* messagecounter is total message, +1 tot end at message 1 */
    message_counter++;

    /* filling the list */
    trace(TRACE_DEBUG, "%s,%s: adding items to list", __FILE__,
	  __FUNCTION__);
    for (i = 0; i < db_num_rows(); i++) {
	/* message size */
	query_result = db_get_result(i, 0);
	tmpmessage.msize =
	    query_result ? strtoull(query_result, NULL, 10) : 0;
	/* real message id */
	query_result = db_get_result(i, 1);
	tmpmessage.realmessageid =
	    query_result ? strtoull(query_result, NULL, 10) : 0;
	/* message status */
	query_result = db_get_result(i, 2);
	tmpmessage.messagestatus =
	    query_result ? strtoull(query_result, NULL, 10) : 0;
	/* virtual message status */
	tmpmessage.virtual_messagestatus = tmpmessage.messagestatus;
	/* unique id */
	query_result = db_get_result(i, 3);
	strncpy(tmpmessage.uidl, query_result, UID_SIZE);

	session_ptr->totalmessages++;
	session_ptr->totalsize += tmpmessage.msize;
	/* descending to create inverted list */
	message_counter--;
	tmpmessage.messageid = (u64_t) message_counter;
	list_nodeadd(&session_ptr->messagelst, &tmpmessage,
		     sizeof(tmpmessage));
    }

    trace(TRACE_DEBUG, "%s,%s: adding succesfull", __FILE__, __FUNCTION__);

    /* setting all virtual values */
    session_ptr->virtual_totalmessages = session_ptr->totalmessages;
    session_ptr->virtual_totalsize = session_ptr->totalsize;

    db_free_result();

    return 1;
}

void db_session_cleanup(PopSession_t * session_ptr)
{
    /* cleanups a session 
       removes a list and all references */
    session_ptr->totalsize = 0;
    session_ptr->virtual_totalsize = 0;
    session_ptr->totalmessages = 0;
    session_ptr->virtual_totalmessages = 0;
    list_freelist(&(session_ptr->messagelst.start));
}

int db_update_pop(PopSession_t * session_ptr)
{
    struct element *tmpelement;
    u64_t user_idnr = 0;

    /* get first element in list */
    tmpelement = list_getstart(&session_ptr->messagelst);

    while (tmpelement != NULL) {
	/* check if they need an update in the database */
	if (((struct message *) tmpelement->data)->virtual_messagestatus !=
	    ((struct message *) tmpelement->data)->messagestatus) {
	    /* use one message to get the user_idnr that goes with the
	       messages */
	    if (user_idnr == 0)
		user_idnr =
		    db_get_useridnr(((struct message *) tmpelement->data)->
				    realmessageid);

	    /* yes they need an update, do the query */
	    snprintf(query, DEF_QUERYSIZE,
		     "UPDATE messages set status='%llu' WHERE "
		     "message_idnr='%llu' AND status<002",
		     ((struct message *) 
		      tmpelement->data)->virtual_messagestatus,
		     ((struct message *) tmpelement->data)->realmessageid);

	    /* FIXME: a message could be deleted already if it has been accessed
	     * by another interface and be deleted by sysop
	     * we need a check if the query failes because it doesn't exists anymore
	     * now it will just bailout */
	    if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __FUNCTION__);
		return -1;
	    }
	}
	tmpelement = tmpelement->nextnode;
    }

    /* because the status of some messages might have changed (for instance
       to status >= 002, the quotum has to be recalculated */
    if (db_calculate_quotum_used(user_idnr) == -1) {
	trace(TRACE_ERROR, "%s,%s: error calculating quotum used",
	      __FILE__, __FUNCTION__);
	return -1;
    }
    return 0;
}

int db_set_deleted(u64_t *affected_rows)
{
     assert(affected_rows != NULL);
     *affected_rows = 0;
     
     snprintf(query, DEF_QUERYSIZE,
	      "UPDATE messages SET status = '003' WHERE status = '002'");
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: Could not execute query",
		__FILE__, __FUNCTION__);
	  return -1;
     }
     *affected_rows = db_get_affected_rows();
     return 1;
}

int db_deleted_purge(u64_t *affected_rows)
{
    unsigned i;
    u64_t *message_idnrs;

    assert(affected_rows != NULL);
    *affected_rows = 0;
    
    /* first we're deleting all the messageblks */
    snprintf(query, DEF_QUERYSIZE,
	     "SELECT message_idnr FROM messages WHERE status='003'");
    trace(TRACE_DEBUG, "%s,%s: executing query [%s]",
	  __FILE__, __FUNCTION__, query);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: Cound not fetch message ID numbers",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    *affected_rows = db_num_rows();
    if (*affected_rows == 0) {
	trace(TRACE_DEBUG, "%s,%s: no messages to purge",
	      __FILE__, __FUNCTION__);
	db_free_result();
	return 0;
    }
    if (!(message_idnrs = 
	  (u64_t *) my_malloc(*affected_rows * sizeof(u64_t)))) {
	trace(TRACE_ERROR, "%s,%s: error allocating memory",
	      __FILE__, __FUNCTION__);
	return -2;
    }


    /* delete each message */
    for (i = 0; i < *affected_rows; i++)
	message_idnrs[i] = strtoull(db_get_result(i, 0), NULL, 10);
    db_free_result();
    for (i = 0; i < *affected_rows; i++) {
	if (db_delete_message(message_idnrs[i]) == -1) {
	    trace(TRACE_ERROR, "%s,%s: error deleting message",
		  __FILE__, __FUNCTION__);
	    my_free(message_idnrs);
	    return -1;
	}
    }
    my_free(message_idnrs);
    return 1;
}

u64_t db_check_sizelimit(u64_t addblocksize UNUSED, u64_t message_idnr,
			 u64_t * user_idnr)
{
    u64_t currmail_size = 0;
    u64_t maxmail_size = 0;

    *user_idnr = db_get_useridnr(message_idnr);
    
    /* get currently used quotum */
    db_get_quotum_used(*user_idnr, &currmail_size);
    
    /* current mailsize from INBOX is now known, 
     * now check the maxsize for this user */
    auth_getmaxmailsize(*user_idnr, &maxmail_size);

    trace(TRACE_DEBUG, "%s,%s: comparing currsize + blocksize [%llu], "
	  "maxsize [%llu]\n",
	  __FILE__, __FUNCTION__, currmail_size, maxmail_size);
    
    /* currmail already represents the current size of
     * messages in this user's mailbox */
    if (((currmail_size) > maxmail_size) && (maxmail_size != 0)) {
	 trace(TRACE_INFO, "%s,%s: mailboxsize of useridnr "
	       "%llu exceed with %llu bytes\n",
	       *user_idnr, (currmail_size) - maxmail_size);
	 
	 /* user is exceeding, we're going to execute a rollback now */
	 if (db_delete_message(message_idnr) < 0) {
	      trace(TRACE_ERROR, "%s,%s: delete of message failed. database "
		    "might be inconsistent. Run dbmail-maintenance",
		    __FILE__, __FUNCTION__);
	      return -2;
	 }
	 /* return 1 to signal that the quotum was exceeded */
	 return 1;
    }
    return 0;
}

int db_imap_append_msg(const char *msgdata, u64_t datalen,
		       u64_t mailbox_idnr, u64_t user_idnr)
{
	timestring_t timestring;
	u64_t message_idnr;
	u64_t messageblk_idnr;
	u64_t physmessage_id = 0;
	u64_t count;
	int result;
	char unique_id[UID_SIZE];	/* unique id */
	
	create_current_timestring(&timestring);
    /* first create a new physmessage entry */
    snprintf(query, DEF_QUERYSIZE,
	     "INSERT INTO physmessage "
	     "(messagesize, rfcsize, internal_date) "
	     "VALUES ('0', '0', '%s')", timestring);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not create physmessage",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    physmessage_id = db_insert_result("physmessage_id");
    /* create a msg 
     * status and seen_flag are set to 001, which means the message 
     * has been read 
     */
    snprintf(query, DEF_QUERYSIZE,
	     "INSERT INTO messages "
	     "(mailbox_idnr, physmessage_id, unique_id, status,"
	     "seen_flag) VALUES ('%llu', '%llu', '', '001', '1')",
	     mailbox_idnr, physmessage_id);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not create message",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    /* fetch the id of the new message */
    message_idnr = db_insert_result("message_idnr");

    result = db_check_sizelimit(datalen, message_idnr, &user_idnr);
    if (result == -1 || result == -2) {
	trace(TRACE_ERROR, "%s,%s: error checking size limit",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    if (result == 1) {
	trace(TRACE_INFO, "%s,%s: user %llu would exceed quotum\n",
	      __FILE__, __FUNCTION__, user_idnr);
	return 2;
    }
    /* ok insert blocks */
    /* first the header: scan until double newline */
    for (count = 1; count < datalen; count++)
	if (msgdata[count - 1] == '\n' && msgdata[count] == '\n')
	    break;

    if (count == datalen) {
	trace(TRACE_INFO, "%s,%s: no double newline found [invalid msg]\n",
	      __FILE__, __FUNCTION__);
	if (db_delete_message(message_idnr) == -1) {
	    trace(TRACE_ERROR, "%s,%s: could not delete invalid message"
		  "%llu. Database could be invalid now..",
		  __FILE__, __FUNCTION__, message_idnr);
	}
	return 1;
    }

    if (count == datalen - 1) {
	/* msg consists of a single header */
	trace(TRACE_INFO, "%s,%s: msg only contains a header",
	      __FILE__, __FUNCTION__);

	if (db_insert_message_block(msgdata, datalen, 
				    message_idnr, &messageblk_idnr) == -1
	    || db_insert_message_block(" \n", 2, message_idnr, 
				       &messageblk_idnr) == -1) {
	    trace(TRACE_ERROR, "%s,%s: could not insert msg block\n",
		  __FILE__, __FUNCTION__);
	    if (db_delete_message(message_idnr) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not delete invalid message"
		      "%llu. Database could be invalid now..", __FILE__,
		      __FUNCTION__, message_idnr);
	    }
	    return -1;
	}
    } else {
	/* 
	 * output header: 
	 * the first count bytes is the header
	 */
	count++;

	if (db_insert_message_block(msgdata, count, message_idnr,
				    &messageblk_idnr) == -1) {
	    trace(TRACE_ERROR, "%s,%s: could not insert msg block\n");
	    if (db_delete_message(message_idnr) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not delete invalid message"
		      "%llu. Database could be  invalid now..", __FILE__,
		      __FUNCTION__, message_idnr);
	    }
	    return -1;
	}

	/* output message */
	while ((datalen - count) > READ_BLOCK_SIZE) {
	    if (db_insert_message_block(&msgdata[count],
					READ_BLOCK_SIZE,
					message_idnr,
					&messageblk_idnr) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not insert msg block",
		      __FILE__, __FUNCTION__);
		if (db_delete_message(message_idnr) == -1) {
		    trace(TRACE_ERROR, "%s,%s: could not delete invalid "
			  "message %llu. Database could be invalid now..",
			  __FILE__, __FUNCTION__, message_idnr);
		}
		return -1;
	    }
	    count += READ_BLOCK_SIZE;
	}


	if (db_insert_message_block(&msgdata[count],
				    datalen - count, message_idnr,
				    &messageblk_idnr) == -1) {
	    trace(TRACE_ERROR, "%s,%s:  could not insert msg block\n",
		  __FILE__, __FUNCTION__);
	    if (db_delete_message(message_idnr) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not delete invalid "
		      "message %llu. Database could be invalid now..",
		      __FILE__, __FUNCTION__, message_idnr);
	    }
	    return -1;
	}

    }

    /* create a unique id */
    create_unique_id(unique_id, message_idnr);
    //snprintf(unique_id, UID_SIZE, "%lluA%lu", message_idnr, td);

    /* set info on message */
    db_update_message(message_idnr, unique_id, datalen, 0);

    /* recalculate quotum used */
    db_calculate_quotum_used(user_idnr);

    return 0;
}

int db_findmailbox(const char *fq_name, u64_t user_idnr, u64_t *mailbox_idnr)
{
 	char *username = NULL;
	char *mailbox_name;
	char *name_str_copy;
	char *tempstr;
	size_t index;
	int result;
	u64_t owner_idnr;

	assert(mailbox_idnr != NULL);
	*mailbox_idnr = 0;

	trace(TRACE_DEBUG, "%s,%s: looking for mailbox with FQN [%s].",
	      __FILE__, __FUNCTION__, fq_name);

	name_str_copy = strdup(fq_name);
	/* see if this is a #User mailbox */
	if ((strlen(NAMESPACE_USER) > 0) &&
	    (strstr(fq_name, NAMESPACE_USER) == fq_name)) {
		index = strcspn(name_str_copy, MAILBOX_SEPERATOR);
		tempstr = &name_str_copy[index + 1];
		index = strcspn(tempstr, MAILBOX_SEPERATOR);
		username = tempstr;
		tempstr[index] = '\0';
		mailbox_name = &tempstr[index + 1];
	} else {
		if ((strlen(NAMESPACE_PUBLIC) > 0) &&
		    (strstr(fq_name, NAMESPACE_PUBLIC) == fq_name)) {
			index = strcspn(name_str_copy, MAILBOX_SEPERATOR);
			mailbox_name = &name_str_copy[index + 1];
			username = PUBLIC_FOLDER_USER;
		} else {
			mailbox_name = name_str_copy;
			owner_idnr = user_idnr;
		}
	}
	if (username) {
		trace(TRACE_DEBUG,"%s,%s: finding user with name [%s].",
		      __FILE__, __FUNCTION__, username);
		result = auth_user_exists(username, &owner_idnr);
		if (result < 0) {
			trace(TRACE_ERROR, "%s,%s: error checking id of "
			      "user.", __FILE__, __FUNCTION__);
			return -1;
		}
		if (result == 0) {
			trace(TRACE_INFO, "%s,%s user [%s] not found.",
			      __FILE__, __FUNCTION__, username);
			return 0;
		}
	}
	result = db_findmailbox_owner(mailbox_name, owner_idnr, mailbox_idnr);
	if (result < 0) {
		trace(TRACE_ERROR, "%s,%s: error finding mailbox [%s] with "
		      "owner [%s, %llu]", __FILE__, __FUNCTION__,
		      mailbox_name, username, owner_idnr);
		return -1;
	}
	my_free(name_str_copy);
	return result;
}


int db_findmailbox_owner(const char *name, u64_t owner_idnr, 
			 u64_t *mailbox_idnr)
{
    char *query_result;
    
    assert(mailbox_idnr != NULL);
    *mailbox_idnr = 0;

    /* if we check the INBOX, we need to be case insensitive */
    if (strncasecmp(name,"INBOX", 5) == 0) { 
	 snprintf(query, DEF_QUERYSIZE,
		  "SELECT mailbox_idnr FROM mailboxes "
		  "WHERE LOWER(name) = LOWER('%s') "
		  "AND owner_idnr='%llu'", name, owner_idnr);
    } else {
	 snprintf(query, DEF_QUERYSIZE,
		  "SELECT mailbox_idnr FROM mailboxes "
		  "WHERE name='%s' AND owner_idnr='%llu'", name, owner_idnr);
    }
    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not select mailbox '%s'\n",
	      __FILE__, __FUNCTION__, name);
	db_free_result();
	return -1;
    }

    if (db_num_rows() < 1) {
	    db_free_result();
	    return 0;
    } else {
	    query_result = db_get_result(0, 0);
	    *mailbox_idnr = (query_result) ? 
		    strtoull(query_result, NULL, 10) : 0;
	    db_free_result();
    }

    if (*mailbox_idnr == 0)
	    return 0;
    return 1;
}

int db_list_mailboxes_by_regex(u64_t user_idnr, int only_subscribed, 
			       regex_t *preg,
			       u64_t **mailboxes, unsigned int *nr_mailboxes)
{
     unsigned int i;
     u64_t *tmp;
     char *result_string;
     char *owner_idnr_str;
     u64_t owner_idnr;
     char *mailbox_name;

     assert(mailboxes != NULL);
     assert(nr_mailboxes != NULL);

     *mailboxes = NULL;
     *nr_mailboxes = 0;
     if (only_subscribed)
	     snprintf(query, DEF_QUERYSIZE,
		      "SELECT mbx.name, mbx.mailbox_idnr, mbx.owner_idnr "
		      "FROM mailboxes mbx "
		      "LEFT JOIN acl "
		      "ON acl.mailbox_id = mbx.mailbox_idnr "
		      "JOIN subscription sub ON sub.user_id = '%llu' "
		      "AND sub.mailbox_id = mbx.mailbox_idnr "
		      "WHERE mbx.owner_idnr = '%llu' "
		      "OR (acl.user_id = '%llu' AND acl.lookup_flag = '1') "
		      "GROUP BY mbx.name, mbx.mailbox_idnr, mbx.owner_idnr",
		      user_idnr, user_idnr, user_idnr);
     else
	     snprintf(query, DEF_QUERYSIZE,
		      "SELECT mbx.name, mbx.mailbox_idnr, mbx.owner_idnr "
		      "FROM mailboxes mbx "
		      "LEFT JOIN acl "
		      "ON mbx.mailbox_idnr = acl.mailbox_id "
		      "WHERE (acl.user_id = '%llu' AND acl.lookup_flag = '1') "
		      "OR mbx.owner_idnr = '%llu'", user_idnr, user_idnr);
     
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
	  owner_idnr_str = db_get_result(i, 2);
	  owner_idnr = owner_idnr_str ? strtoull(owner_idnr_str, NULL, 10): 0;
	  /* add possible namespace prefix to mailbox_name */
	  mailbox_name = mailbox_add_namespace(result_string, owner_idnr,
					       user_idnr);
	  if (mailbox_name) {
		  trace(TRACE_DEBUG, "%s,%s: comparing mailbox [%s] to "
			"regular expression", __FILE__, __FUNCTION__, mailbox_name);
		  if (regexec(preg, mailbox_name, 0, NULL, 0) == 0) {
			  tmp[*nr_mailboxes] = 
				  strtoull(db_get_result(i, 1), NULL, 10);
			  (*nr_mailboxes)++;
			  trace(TRACE_DEBUG, "%s,%s: regex match %s",
				__FILE__, __FUNCTION__, mailbox_name);
		  }
		  my_free(mailbox_name);
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

int db_findmailbox_by_regex(u64_t owner_idnr, const char *pattern,
			    u64_t ** children, unsigned *nchildren,
			    int only_subscribed)
{
	int result;
	regex_t preg;
    
	*children = NULL;

	if ((result = regcomp(&preg, pattern, REG_ICASE | REG_NOSUB)) != 0) {
		trace(TRACE_ERROR, "%s,%s: error compiling regex pattern: %d\n",
		      __FILE__, __FUNCTION__, result);
		return 1;
	}
    
	/* list normal mailboxes */
	if (db_list_mailboxes_by_regex(owner_idnr, only_subscribed, &preg, 
				       children, nchildren) < 0) {
		trace(TRACE_ERROR, "%s,%s: error listing mailboxes", 
		      __FILE__, __FUNCTION__);
		return -1;
	}

	if (nchildren == 0) {
		trace(TRACE_INFO, "%s, %s: did not find any mailboxes that "
		      "match pattern. returning 0, nchildren = 0", 
		      __FILE__, __FUNCTION__);
		return 0;
	}
	 

	/* store matches */
	trace(TRACE_INFO,"%s,%s: found [%d] mailboxes", __FILE__, __FUNCTION__,
	      *nchildren);
	return 0;
}

int db_getmailbox(mailbox_t * mb)
{
    char *query_result;
    u64_t highest_id;
    unsigned i;

    /* free existing MSN list */
    if (mb->seq_list) {
	my_free(mb->seq_list);
	mb->seq_list = NULL;
    }

    mb->flags = 0;
    mb->exists = 0;
    mb->unseen = 0;
    mb->recent = 0;
    mb->msguidnext = 0;

    /* select mailbox */
    snprintf(query, DEF_QUERYSIZE,
	     "SELECT permission,"
	     "seen_flag,"
	     "answered_flag,"
	     "deleted_flag,"
	     "flagged_flag,"
	     "recent_flag,"
	     "draft_flag "
	     "FROM mailboxes WHERE mailbox_idnr = '%llu'",
	     mb->uid);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not select mailbox\n",
	      __FILE__, __FUNCTION__);
	return -1;
    }


    if (db_num_rows() == 0) {
	trace(TRACE_ERROR, "%s,%s: invalid mailbox id specified",
	      __FILE__, __FUNCTION__);
	db_free_result();
	return -1;
    }

    mb->permission = atoi(db_get_result(0, 0));

    if (db_get_result(0, 1))
	mb->flags |= IMAPFLAG_SEEN;
    if (db_get_result(0, 2))
	mb->flags |= IMAPFLAG_ANSWERED;
    if (db_get_result(0, 3))
	mb->flags |= IMAPFLAG_DELETED;
    if (db_get_result(0, 4))
	mb->flags |= IMAPFLAG_FLAGGED;
    if (db_get_result(0, 5))
	mb->flags |= IMAPFLAG_RECENT;
    if (db_get_result(0, 6))
	mb->flags |= IMAPFLAG_DRAFT;

    db_free_result();

    /* select messages */
    snprintf(query, DEF_QUERYSIZE,
	     "SELECT message_idnr, seen_flag, recent_flag "
	     "FROM messages WHERE mailbox_idnr = '%llu' "
	     "AND status < 2 AND unique_id != '' "
	     "ORDER BY message_idnr ASC", mb->uid);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not retrieve messages",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    mb->exists = db_num_rows();

    /* alloc mem */
    mb->seq_list = (u64_t *) my_malloc(sizeof(u64_t) * mb->exists);
    if (!mb->seq_list) {
	/* out of mem */
	db_free_result();
	return -1;
    }

    for (i = 0; i < db_num_rows(); i++) {
	if (db_get_result(i, 1)[0] == '0')
	    mb->unseen++;
	if (db_get_result(i, 2)[0] == '1')
	    mb->recent++;

	mb->seq_list[i] = strtoull(db_get_result(i, 0), NULL, 10);
    }

    db_free_result();

    /* now determine the next message UID 
     * NOTE expunged messages are selected as well in order to be 
     * able to restore them 
     */
    snprintf(query, DEF_QUERYSIZE,
	     "SELECT MAX(message_idnr) FROM messages "
	     "WHERE unique_id != ''");

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not determine highest message ID",
	      __FILE__, __FUNCTION__);
	my_free(mb->seq_list);
	mb->seq_list = NULL;
	return -1;
    }

    query_result = db_get_result(0, 0);
    highest_id = (query_result) ? strtoull(query_result, NULL, 10) : 0;
    mb->msguidnext = highest_id + 1;
    db_free_result();

    return 0;
}

int db_createmailbox(const char *name, u64_t owner_idnr, u64_t *mailbox_idnr)
{
	const char *simple_name;
	assert(mailbox_idnr != NULL);
	*mailbox_idnr = 0;
	/* remove namespace information from mailbox name */
	if (!(simple_name = mailbox_remove_namespace(name))) {
		trace(TRACE_ERROR, "%s,%s: could not create simple mailbox name "
		      "from full name", __FILE__, __FUNCTION__);
		return -1;
	}
	snprintf(query, DEF_QUERYSIZE,
		 "INSERT INTO mailboxes (name, owner_idnr,"
		 "seen_flag, answered_flag, deleted_flag, flagged_flag, "
		 "recent_flag, draft_flag, permission)"
		 " VALUES ('%s', '%llu', 1, 1, 1, 1, 1, 1, 2)", simple_name,
		 owner_idnr);
     
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not create mailbox",
		      __FILE__, __FUNCTION__);
		return -1;
	}
	*mailbox_idnr = db_insert_result("mailbox_idnr");
	return 0;
}


int db_find_create_mailbox(const char *name, u64_t owner_idnr, u64_t *mailbox_idnr)
{
     u64_t mboxidnr;

     assert(mailbox_idnr != NULL);
     *mailbox_idnr = 0;

     /* Did we fail to find the mailbox? */
     if (db_findmailbox_owner(name, owner_idnr, &mboxidnr) != 1)
       {
         /* Did we fail to create the mailbox? */
         if (db_createmailbox(name, owner_idnr, &mboxidnr) != 0)
           {
             /* Serious failure situation! */
             trace(TRACE_ERROR, "%s, %s: seriously could not create mailbox [%s]",
                 __FILE__, __FUNCTION__, name);
             return -1;
           }
         trace(TRACE_DEBUG, "%s, %s: mailbox [%s] created on the fly",
             __FILE__, __FUNCTION__, name);
       }
     trace(TRACE_DEBUG, "%s, %s: mailbox [%s] found",
         __FILE__, __FUNCTION__, name);

     *mailbox_idnr = mboxidnr;
     return 0;
}

int db_listmailboxchildren(u64_t mailbox_idnr, u64_t user_idnr,
			   u64_t ** children, int *nchildren,
			   const char *filter)
{
    int i;
    char *mailbox_name;

    /* retrieve the name of this mailbox */
    snprintf(query, DEF_QUERYSIZE,
	     "SELECT name FROM mailboxes WHERE "
	     "mailbox_idnr = '%llu' AND owner_idnr = '%llu'",
	     mailbox_idnr, user_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not retrieve mailbox name\n",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    if (db_num_rows() == 0) {
	    trace(TRACE_WARNING, "%s,%s: No mailbox found with mailbox_idnr "
		  "[%llu]", __FILE__, __FUNCTION__, mailbox_idnr);
	    db_free_result();
	    *children = NULL;
	    *nchildren = 0;
	    return 0;
    }
	
    mailbox_name = db_get_result(0, 0);
    db_free_result();
    if (mailbox_name)
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT mailbox_idnr FROM mailboxes WHERE name LIKE '%s/%s'"
		 " AND owner_idnr = '%llu'",
		 mailbox_name, filter, user_idnr);
    else
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT mailbox_idnr FROM mailboxes WHERE name LIKE '%s'"
		 " AND owner_idnr = '%llu'", filter, user_idnr);

    /* now find the children */
    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not retrieve mailbox id",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    if (db_num_rows() == 0) {
	/* empty set */
	*children = NULL;
	*nchildren = 0;
	db_free_result();
	return 0;
    }

    *nchildren = db_num_rows();
    if (*nchildren == 0) {
	*children = NULL;
	db_free_result();
	return 0;
    }

    *children = (u64_t *) my_malloc(sizeof(u64_t) * (*nchildren));

    if (!(*children)) {
	/* out of mem */
	trace(TRACE_ERROR, "%s,%s: out of memory\n", __FILE__, __FILE__);
	db_free_result();
	return -1;
    }

    for (i = 0; i < *nchildren; i++) {
	(*children)[i] = strtoull(db_get_result(i, 0), NULL, 10);
    }

    db_free_result();
    return 0;			/* success */
}

/* this function is redundant! 
 * also, because mailbox_idnr is unique, owner_idnr is not
 * needed in the call.*/
int db_removemailbox(u64_t mailbox_idnr, u64_t owner_idnr UNUSED)
{
    if (db_delete_mailbox(mailbox_idnr, 0) == -1) {
	trace(TRACE_ERROR, "%s,%s: error deleting mailbox",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    return 0;
}

int db_isselectable(u64_t mailbox_idnr)
{
    char *query_result;
    long not_selectable;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT no_select FROM mailboxes WHERE mailbox_idnr = '%llu'",
	     mailbox_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not retrieve select-flag",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    query_result = db_get_result(0, 0);

    if (!query_result) {
	/* empty set, mailbox does not exist */
	db_free_result();
	return 0;
    }

    not_selectable = strtol(query_result, NULL, 10);
    db_free_result();
    if (not_selectable == 0)
	return 1;
    else
	return 0;
}

int db_noinferiors(u64_t mailbox_idnr)
{
    char *query_result;
    long no_inferiors;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT no_inferiors FROM mailboxes WHERE mailbox_idnr = '%llu'",
	     mailbox_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not retrieve noinferiors-flag",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    query_result = db_get_result(0, 0);

    if (!query_result) {
	/* empty set, mailbox does not exist */
	db_free_result();
	return 0;
    }
    no_inferiors = strtol(query_result, NULL, 10);
    db_free_result();

    return no_inferiors;
}

int db_setselectable(u64_t mailbox_idnr, int select_value)
{
    snprintf(query, DEF_QUERYSIZE,
	     "UPDATE mailboxes SET no_select = %d WHERE mailbox_idnr = '%llu'",
	     (!select_value), mailbox_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not set noselect-flag",
	      __FILE__, __FUNCTION__);
	return -1;
    }
    return 0;
}

int db_removemsg(u64_t mailbox_idnr)
{
    /* update messages belonging to this mailbox: mark as deleted (status 3) */
    snprintf(query, DEF_QUERYSIZE,
	     "UPDATE messages SET status='3' WHERE mailbox_idnr = '%llu'",
	     mailbox_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not update messages in mailbox",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    return 0;			/* success */
}

int db_movemsg(u64_t mailbox_to, u64_t mailbox_from)
{
    snprintf(query, DEF_QUERYSIZE,
	     "UPDATE messages SET mailbox_idnr='%llu' WHERE"
	     " mailbox_idnr = '%llu'", mailbox_to, mailbox_from);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not update messages in mailbox\n",
	      __FILE__, __FUNCTION__);
	return -1;
    }
    return 0;			/* success */
}

int db_get_message_size(u64_t message_idnr, u64_t *message_size) 
{
     char *result_string;

     assert (message_size != NULL);

     snprintf(query, DEF_QUERYSIZE,
	      "SELECT pm.messagesize FROM physmessage pm, messages msg "
	      "WHERE pm.id = msg.physmessage_id "
	      "AND message_idnr = '%llu'", message_idnr);
     
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR,
		"%s,%s: could not fetch message size for message id "
		"[%llu]", __FILE__, __FUNCTION__, message_idnr);
	  return -1;
     }
     
     if (db_num_rows() != 1) {
	  trace(TRACE_ERROR,
		"%s,%s: message [%llu] does not exist/has "
		"multiple entries\n", 
		__FILE__, __FUNCTION__, message_idnr);
	  db_free_result();
	  return -1;
     }
     
     result_string = db_get_result(0, 0);
     if (result_string)
	  *message_size = strtoull(result_string, NULL, 10);
     else {
	  trace(TRACE_ERROR,
		"%s,%s: no result set after requesting msgsize "
		"of msg [%llu]\n", 
		__FILE__, __FUNCTION__, message_idnr);
	  db_free_result();
	  return -1;
     }
     db_free_result();
     return 1;
     
}
int db_copymsg(u64_t msg_idnr, u64_t mailbox_to, u64_t user_idnr,
	       u64_t *newmsg_idnr)
{
     u64_t curr_quotum; /* current mailsize */
     u64_t maxmail; /* maximum mailsize */
     u64_t msgsize; /* size of message */
     time_t td;

     time(&td);			/* get time */
     
     if (db_get_quotum_used(user_idnr, &curr_quotum) < 0) {
	     trace(TRACE_ERROR,
		   "%s,%s: error fetching used quotum for user [%llu]",
		   __FILE__, __FUNCTION__, user_idnr);
	     return -1;
     }
     if (auth_getmaxmailsize(user_idnr, &maxmail) == -1) {
	     trace(TRACE_ERROR,
		   "%s,%s: error fetching max quotum for user [%llu]", 
		   __FILE__, __FUNCTION__, user_idnr);
	     return -1;
     }
     
     if (maxmail > 0) {
	  if (curr_quotum >= maxmail) {
	       trace(TRACE_INFO,
		  "%s,%s: quotum already exceeded for user [%llu]",
		     __FILE__, __FUNCTION__, user_idnr);
	       return -2;
	  }
	  if (db_get_message_size(msg_idnr, &msgsize) == -1) {
	       trace(TRACE_ERROR,"%s,%s: error getting message size for "
		     "message [%llu]", __FILE__, __FUNCTION__, msg_idnr);
	       return -1;
	  }
	  if (msgsize > maxmail - curr_quotum) {
	       trace(TRACE_INFO, "%s,%s: quotum would exceed for user [%llu]",
		     __FILE__, __FUNCTION__, user_idnr);
	       return -2;
	  }
     }

     /* copy: */
     /* first select the entry that has to be copied */
     snprintf(query, DEF_QUERYSIZE,
	      "SELECT physmessage_id, seen_flag, "
	      "answered_flag, deleted_flag, flagged_flag, recent_flag, "
	      "draft_flag, status FROM messages "
	      "WHERE message_idnr = '%llu'", msg_idnr);
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: could not select source message",
		__FILE__, __FUNCTION__);
	  return -1;
     }
     if (db_num_rows() < 1) {
	  trace(TRACE_ERROR, "%s,%s: could not select source message",
		__FILE__, __FUNCTION__);
	  return -1;
     }
     /* now insert a copy of the entry into the messages table */
     snprintf(query, DEF_QUERYSIZE,
	      "INSERT INTO messages (mailbox_idnr, physmessage_id, seen_flag, "
	      "answered_flag, deleted_flag, flagged_flag, recent_flag, "
	     "draft_flag, unique_id, status) VALUES "
	      "('%llu', '%s', '%s', '%s', '%s', "
	      "'%s', '%s', '%s', 'dummy', '%s')",
	      mailbox_to, db_get_result(0, 0), db_get_result(0, 1), 
	      db_get_result(0, 2), db_get_result(0, 3), db_get_result(0, 4), 
	      db_get_result(0, 5), db_get_result(0, 6), db_get_result(0,7));
     
     db_free_result();
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: could not insert copy in messages",
		__FILE__, __FUNCTION__);
	  return -1;
     }
     /* get the id of the inserted record */
     *newmsg_idnr = db_insert_result("message_idnr");
    /* all done, validate new msg by creating a new unique id
     * for the copied msg */
     snprintf(query, DEF_QUERYSIZE,
	      "UPDATE messages SET unique_id='%lluA%lu' "
	      "WHERE message_idnr='%llu'", *newmsg_idnr, (unsigned long) td, 
	      *newmsg_idnr);
     
     if (db_query(query) == -1) {
	  trace(TRACE_ERROR, "%s,%s: could not set unique ID for copied msg",
		__FILE__, __FUNCTION__);
	  return -1;
     }
     
     /* update quotum */
     if (db_calculate_quotum_used(user_idnr) == -1) {
	     trace(TRACE_ERROR, "%s,%s: error calculating quotum for "
		   "shared user with user_idnr [%llu]", 
		   __FILE__, __FUNCTION__, user_idnr);
	     return -1;
     }    
     return 1;			/* success */
}				/* end db_copymsg() */

int db_getmailboxname(u64_t mailbox_idnr, u64_t user_idnr, char *name)
{
	char *tmp_name, *tmp_fq_name;
	char *query_result;
	int result;
	u64_t owner_idnr;

	result = db_get_mailbox_owner(mailbox_idnr, &owner_idnr);
	if (result <= 0) {
		trace(TRACE_ERROR, "%s,%s: error checking ownership of "
		      "mailbox", __FILE__,__FUNCTION__);
		return -1;
	}
		
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT name FROM mailboxes WHERE mailbox_idnr = '%llu'",
		 mailbox_idnr);
    
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not retrieve name",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	if (db_num_rows() < 1) {
		db_free_result();
		*name = '\0';
		return 0;
	}

	query_result = db_get_result(0, 0);

	if (!query_result) {
		/* empty set, mailbox does not exist */
		db_free_result();
		*name = '\0';
		return 0;
	}
	if (!(tmp_name = my_malloc((strlen(query_result) + 1) * 
				   sizeof(char)))) {
		trace(TRACE_ERROR,"%s,%s: error allocating memory",
		      __FILE__, __FUNCTION__);
		return -1;
	}
	
	strncpy(tmp_name, query_result, IMAP_MAX_MAILBOX_NAMELEN);
	db_free_result();
	tmp_fq_name = mailbox_add_namespace(tmp_name, owner_idnr, user_idnr);
	if (!tmp_fq_name) {
		trace(TRACE_ERROR,"%s,%s: error getting fully qualified "
		      "mailbox name", __FILE__, __FUNCTION__);
		return -1;
	}
	strncpy(name, tmp_fq_name, IMAP_MAX_MAILBOX_NAMELEN);
	my_free(tmp_name);
	my_free(tmp_fq_name);
	return 0;
}

int db_setmailboxname(u64_t mailbox_idnr, const char *name)
{
	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE mailboxes SET name = '%s' "
		 "WHERE mailbox_idnr = '%llu'",
		 name, mailbox_idnr);
	
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not set name", __FILE__,
		      __FUNCTION__);
		return -1;
	}
	
	return 0;
}

int db_expunge(u64_t mailbox_idnr, u64_t ** msg_idnrs, u64_t * nmsgs)
{
    u64_t i;
    char *query_result;

    if (nmsgs && msg_idnrs) {
	/* first select msg UIDs */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT message_idnr FROM messages WHERE "
		 "mailbox_idnr = '%llu' AND deleted_flag='1' "
		 "AND status<'2' "
		 "ORDER BY message_idnr DESC", mailbox_idnr);

	if (db_query(query) == -1) {

	    trace(TRACE_ERROR,
		  "%s,%s: could not select messages in mailbox", __FILE__,
		  __FUNCTION__);
	    return -1;
	}

	/* now alloc mem */
	*nmsgs = db_num_rows();
	*msg_idnrs = (u64_t *) my_malloc(sizeof(u64_t) * (*nmsgs));
	if (!(*msg_idnrs)) {
	    /* out of mem */
	    *nmsgs = 0;
	    db_free_result();
	    return -1;
	}

	/* save ID's in array */
	for (i = 0; i < *nmsgs; i++) {
	    query_result = db_get_result(i, 0);
	    (*msg_idnrs)[i] =
		query_result ? strtoull(query_result, NULL, 10) : 0;
	}
	db_free_result();
    }

    /* update messages belonging to this mailbox: 
     * mark as expunged (status 2) */
    snprintf(query, DEF_QUERYSIZE,
	     "UPDATE messages SET status='002' "
	     "WHERE mailbox_idnr = '%llu' "
	     "AND deleted_flag='1' AND status < '002'", mailbox_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not update messages in mailbox",
	      __FILE__, __FUNCTION__);
	if (msg_idnrs)
	    my_free(*msg_idnrs);

	if (nmsgs)
	    *nmsgs = 0;

	return -1;
    }

    /* calculate new quotum (use the msg_idnrs to get the user_idnr */
    if (nmsgs && *nmsgs > 0)
	 db_calculate_quotum_used(db_get_useridnr((*msg_idnrs)[0]));
    return 0;			/* success */
}

u64_t db_first_unseen(u64_t mailbox_idnr)
{
    u64_t id;
    char *query_result;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT MIN(message_idnr) FROM messages "
	     "WHERE mailbox_idnr = '%llu' "
	     "AND status < '2' AND seen_flag = '0' AND unique_id != ''",
	     mailbox_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not select messages",
	      __FILE__, __FUNCTION__);
	return (u64_t) (-1);
    }

    if (db_num_rows() < 1) {
	 id = 0;
    } else {
	 query_result = db_get_result(0, 0);
	 id = (query_result) ? strtoull(query_result, NULL, 10): 0;
    }
    db_free_result();
    return id;
}

int db_subscribe(u64_t mailbox_idnr, u64_t user_idnr)
{
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT * FROM subscription "
		 "WHERE mailbox_id = '%llu' "
		 "AND user_id = '%llu'", mailbox_idnr, user_idnr);
	
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not verify subscription",
		      __FILE__, __FUNCTION__);
		return (-1);
	}
	
	if (db_num_rows() > 0) {
		trace(TRACE_DEBUG, "%s,%s: already subscribed to mailbox "
		      "[%llu]", __FILE__, __FUNCTION__, mailbox_idnr);
		db_free_result();
		return 0;
	}
	  
	db_free_result();
	
	snprintf(query, DEF_QUERYSIZE,
		 "INSERT INTO subscription (user_id, mailbox_id) "
		 "VALUES ('%llu', '%llu')", user_idnr, mailbox_idnr);
	  
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not insert subscription",
		      __FILE__, __FUNCTION__);
		return -1;
	}
	
	return 0;
}

int db_unsubscribe(u64_t mailbox_idnr, u64_t user_idnr)
{
	snprintf(query, DEF_QUERYSIZE,
		 "DELETE FROM subscription "
		 "WHERE user_id = '%llu' AND mailbox_id = '%llu'",
		 user_idnr, mailbox_idnr);
	  	  
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not update mailbox",
		      __FILE__, __FUNCTION__);
		return (-1);
	}
	return 0;
}

int db_get_msgflag(const char *flag_name, u64_t msg_idnr,
		   u64_t mailbox_idnr)
{
    char the_flag_name[DEF_QUERYSIZE / 2];	/* should be sufficient ;) */
    long val = 0;
    char *query_result;

    /* determine flag */
    if (strcasecmp(flag_name, "seen") == 0)
	snprintf(the_flag_name, DEF_QUERYSIZE / 2, "seen_flag");
    else if (strcasecmp(flag_name, "deleted") == 0)
	snprintf(the_flag_name, DEF_QUERYSIZE / 2, "deleted_flag");
    else if (strcasecmp(flag_name, "answered") == 0)
	snprintf(the_flag_name, DEF_QUERYSIZE / 2, "answered_flag");
    else if (strcasecmp(flag_name, "flagged") == 0)
	snprintf(the_flag_name, DEF_QUERYSIZE / 2, "flagged_flag");
    else if (strcasecmp(flag_name, "recent") == 0)
	snprintf(the_flag_name, DEF_QUERYSIZE / 2, "recent_flag");
    else if (strcasecmp(flag_name, "draft") == 0)
	snprintf(the_flag_name, DEF_QUERYSIZE / 2, "draft_flag");
    else
	return 0;		/* non-existent flag is not set */

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT %s FROM messages "
	     "WHERE message_idnr = '%llu' AND status< '2' "
	     "AND unique_id != '' "
	     "AND mailbox_idnr = '%llu'",
	     the_flag_name, msg_idnr, mailbox_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not select message",
	      __FILE__, __FUNCTION__);
	return (-1);
    }

    if (db_num_rows() < 1)
	val = 0;
    else {
	query_result = db_get_result(0, 0);
	if (query_result)
	    val = strtol(query_result, NULL, 10);
	else
	    val = 0;
    }

    db_free_result();
    return (int) val;
}

int db_get_msgflag_all(u64_t msg_idnr, u64_t mailbox_idnr, int *flags)
{
    int i;
    char *query_result;

    memset(flags, 0, sizeof(int) * IMAP_NFLAGS);

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT seen_flag, answered_flag, deleted_flag, "
	     "flagged_flag, draft_flag, recent_flag FROM messages "
	     "WHERE message_idnr = '%llu' AND status<2 AND unique_id != '' "
	     "AND mailbox_idnr = '%llu'", msg_idnr, mailbox_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not select message",
	      __FILE__, __FUNCTION__);
	return (-1);
    }

    if (db_num_rows() > 0) {
	for (i = 0; i < IMAP_NFLAGS; i++) {
	    query_result = db_get_result(0, i);
	    if (query_result && query_result[0] != '0')
		flags[i] = 1;
	}
    }
    db_free_result();
    return 0;
}

int db_set_msgflag(u64_t msg_idnr, u64_t mailbox_idnr,
		   int *flags, int action_type)
{
    /* we're lazy.. just call db_set_msgflag_range with range
     * msg_idnr to msg_idnr! */

    if (db_set_msgflag_range(msg_idnr, msg_idnr, mailbox_idnr,
			     flags, action_type) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not set message flags",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    return 0;

}

int db_set_msgflag_range(u64_t msg_idnr_low, u64_t msg_idnr_high,
			 u64_t mailbox_idnr, int *flags, int action_type)
{
    int i;
    int placed = 0;
    int left;

    snprintf(query, DEF_QUERYSIZE, "UPDATE messages SET ");

    for (i = 0; i < IMAP_NFLAGS; i++) {
	left = DEF_QUERYSIZE - strlen(query);
	switch (action_type) {
	case IMAPFA_ADD:
	    if (flags[i] > 0) {
		strncat(query, db_flag_desc[i], left);
		left = DEF_QUERYSIZE - strlen(query);
		strncat(query, "=1,", left);
		placed = 1;
	    }
	    break;
	case IMAPFA_REMOVE:
	    if (flags[i] > 0) {
		strncat(query, db_flag_desc[i], left);
		left = DEF_QUERYSIZE - strlen(query);
		strncat(query, "=0,", left);
		placed = 1;
	    }
	    break;

	case IMAPFA_REPLACE:
	    strncat(query, db_flag_desc[i], left);
	    left = DEF_QUERYSIZE - strlen(query);
	    if (flags[i] == 0)
		strncat(query, "=0,", left);
	    else
		strncat(query, "=1,", left);
	    placed = 1;
	    break;
	}
    }

    if (!placed)
	return 0;		/* nothing to update */

    /* last character in string is comma, replace it --> strlen()-1 */
    left = DEF_QUERYSIZE - strlen(query);
    snprintf(&query[strlen(query) - 1], left,
	     " WHERE message_idnr BETWEEN '%llu' AND '%llu' AND "
	     "status < '2' AND mailbox_idnr = '%llu'",
	     msg_idnr_low, msg_idnr_high, mailbox_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not set flags",
	      __FILE__, __FUNCTION__);
	return (-1);
    }

    return 0;
}

int db_get_msgdate(u64_t mailbox_idnr, u64_t msg_idnr, char *date)
{
    char *query_result;
    snprintf(query, DEF_QUERYSIZE,
	     "SELECT pm.internal_date FROM physmessage pm, messages msg "
	     "WHERE msg.mailbox_idnr = '%llu' "
	     "AND msg.message_idnr = '%llu' AND msg.unique_id!='' "
	     "AND pm.id = msg.physmessage_id", mailbox_idnr, msg_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not get message",
	      __FILE__, __FUNCTION__);
	return (-1);
    }

    if ((db_num_rows() > 0) && (query_result = db_get_result(0, 0))) {
	strncpy(date, query_result, IMAP_INTERNALDATE_LEN);
	date[IMAP_INTERNALDATE_LEN - 1] = '\0';
    } else {
	/* no date ? let's say 1 jan 1970 */
	strncpy(date, "1970-01-01 00:00:01", IMAP_INTERNALDATE_LEN);
	date[IMAP_INTERNALDATE_LEN - 1] = '\0';
    }

    db_free_result();
    return 0;
}

int db_set_rfcsize(u64_t rfcsize, u64_t msg_idnr, u64_t mailbox_idnr)
{
    u64_t physmessage_id = 0;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT physmessage_id FROM messages "
	     "WHERE message_idnr = '%llu' "
	     "AND mailbox_idnr = '%llu'", msg_idnr, mailbox_idnr);
    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not get physmessage_id for "
	      "message [%llu]", __FILE__, __FUNCTION__, msg_idnr);
	return -1;
    }

    if (db_num_rows() == 0) {
	trace(TRACE_DEBUG, "%s,%s: no such message [%llu]",
	      __FILE__, __FUNCTION__, msg_idnr);
	return 0;
    }

    physmessage_id = strtoull(db_get_result(0, 0), NULL, 10);

    snprintf(query, DEF_QUERYSIZE,
	     "UPDATE physmessage SET rfcsize = '%llu' "
	     "WHERE id = '%llu'", rfcsize, physmessage_id);
    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not update  "
	      "message [%llu]", __FILE__, __FUNCTION__, msg_idnr);
	return -1;
    }

    return 0;
}

int db_get_rfcsize(u64_t msg_idnr, u64_t mailbox_idnr, u64_t *rfc_size)
{
    char *query_result;

    assert(rfc_size != NULL);
    *rfc_size = 0;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT pm.rfcsize FROM physmessage pm, messages msg "
	     "WHERE pm.id = msg.physmessage_id "
	     "AND msg.message_idnr = '%llu' "
	     "AND msg.status< '2' "
	     "AND msg.unique_id != '' "
	     "AND msg.mailbox_idnr = '%llu'", msg_idnr, mailbox_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not fetch RFC size from table",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    if (db_num_rows() < 1) {
	trace(TRACE_ERROR, "%s,%s: message not found",
	      __FILE__, __FUNCTION__);
	db_free_result();
	return -1;
    }

    query_result = db_get_result(0, 0);
    if (query_result)
	 *rfc_size = strtoull(query_result, NULL, 10);
    else
	 *rfc_size = 0;
    
    db_free_result();
    return 1;
}

int db_get_msginfo_range(u64_t msg_idnr_low, u64_t msg_idnr_high,
			 u64_t mailbox_idnr, int get_flags,
			 int get_internaldate,
			 int get_rfcsize, int get_msg_idnr,
			 msginfo_t ** result, unsigned *resultsetlen)
{
    unsigned nrows, i, j;
    char *query_result;
    *result = 0;
    *resultsetlen = 0;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT seen_flag, answered_flag, deleted_flag, flagged_flag, "
	     "draft_flag, recent_flag, internal_date, rfcsize, message_idnr "
	     "FROM messages msg, physmessage pm "
	     "WHERE pm.id = msg.physmessage_id "
	     "AND message_idnr BETWEEN '%llu' AND '%llu' "
	     "AND mailbox_idnr = '%llu' AND status < '2' AND unique_id != '' "
	     "ORDER BY message_idnr ASC", msg_idnr_low, msg_idnr_high,
	     mailbox_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not select message",
	      __FILE__, __FUNCTION__);
	return (-1);
    }

    if ((nrows = db_num_rows()) == 0) {
	db_free_result();
	return 0;
    }

    *result = (msginfo_t *) my_malloc(nrows * sizeof(msginfo_t));
    if (!(*result)) {
	trace(TRACE_ERROR, "%s,%s: out of memory", __FILE__, __FUNCTION__);
	db_free_result();
	return -2;
    }

    memset(*result, 0, nrows * sizeof(msginfo_t));

    for (i = 0; i < nrows; i++) {
	if (get_flags) {
	    for (j = 0; j < IMAP_NFLAGS; j++) {
		query_result = db_get_result(i, j);
		(*result)[i].flags[j] =
		     (query_result && query_result[0] != '0') ? 1 : 0;
	    }
	}

	if (get_internaldate) {
	    query_result = db_get_result(i, IMAP_NFLAGS);
	    strncpy((*result)[i].internaldate,
		    (query_result) ? query_result : "1970-01-01 00:00:01",
		    IMAP_INTERNALDATE_LEN);
	}
	if (get_rfcsize) {
	    query_result = db_get_result(i, IMAP_NFLAGS + 1);
	    (*result)[i].rfcsize =
		(query_result) ? strtoull(query_result, NULL, 10) : 0;
	}
	if (get_msg_idnr) {
	    query_result = db_get_result(i, IMAP_NFLAGS + 2);
	    (*result)[i].uid =
		(query_result) ? strtoull(query_result, NULL, 10) : 0;
	}
    }
    db_free_result();

    *resultsetlen = nrows;
    return 0;
}

int db_get_main_header(u64_t msg_idnr, struct list *hdrlist)
{
    char *query_result;
    u64_t dummy = 0, sizedummy = 0;
    int result;

    if (!hdrlist)
	return 0;

    if (hdrlist->start)
	list_freelist(&hdrlist->start);

    list_init(hdrlist);

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT messageblk "
	     "FROM messageblks blk, messages msg "
	     "WHERE blk.physmessage_id = msg.physmessage_id "
	     "AND msg.message_idnr = '%llu' "
	     "ORDER BY blk.messageblk_idnr ASC", msg_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not get message header",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    if (db_num_rows() > 0) {
	query_result = db_get_result(0, 0);
	if (!query_result) {
	    trace(TRACE_ERROR, "%s,%s: no header for message found",
		  __FILE__, __FUNCTION__);
	    db_free_result();
	    return -1;
	}
    } else {
	trace(TRACE_ERROR, "%s,%s: no message blocks found for message",
	      __FILE__, __FUNCTION__);
	db_free_result();
	return -1;
    }

    result = mime_readheader(query_result, &dummy, hdrlist, &sizedummy);

    db_free_result();

    if (result == -1) {
	/* parse error */
	trace(TRACE_ERROR, "%s,%s: error parsing header of message [%llu]",
	      msg_idnr);
	if (hdrlist->start) {
	    list_freelist(&hdrlist->start);
	    list_init(hdrlist);
	}
	return -3;
    }

    if (result == -2) {
	/* out of memory */
	trace(TRACE_ERROR, "%s,%s: out of memory", __FILE__, __FUNCTION__);
	if (hdrlist->start) {
	    list_freelist(&hdrlist->start);
	    list_init(hdrlist);
	}
	return -2;
    }

    /* success ! */
    return 0;
}

int db_mailbox_msg_match(u64_t mailbox_idnr, u64_t msg_idnr)
{
    int val;

    snprintf(query, DEF_QUERYSIZE,
	     "SELECT message_idnr FROM messages "
	     "WHERE message_idnr = '%llu' "
	     "AND mailbox_idnr = '%llu' "
	     "AND status< '002' AND unique_id!=''", msg_idnr,
	     mailbox_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not get message",
	      __FILE__, __FUNCTION__);
	return (-1);
    }

    val = db_num_rows();
    db_free_result();
    return val;
}

int db_get_user_aliases(u64_t user_idnr, struct list *aliases)
{
    int i, n;
    char *query_result;
    if (!aliases) {
	trace(TRACE_ERROR, "%s,%s: got a NULL pointer as argument",
	      __FILE__, __FUNCTION__);
	return -2;
    }

    list_init(aliases);

    /* do a inverted (DESC) query because adding the names to the 
     * final list inverts again */
    snprintf(query, DEF_QUERYSIZE,
	     "SELECT alias FROM aliases WHERE deliver_to = '%llu' "
	     "ORDER BY alias DESC", user_idnr);

    if (db_query(query) == -1) {
	trace(TRACE_ERROR, "%s,%s: could not retrieve  list",
	      __FILE__, __FUNCTION__);
	return -1;
    }

    n = db_num_rows();
    for (i = 0; i < n; i++) {
	query_result = db_get_result(i, 0);
	if (!
	    (list_nodeadd
	     (aliases, query_result, strlen(query_result) + 1))) {
	    list_freelist(&aliases->start);
	    db_free_result();
	    return -2;
	}
    }

    db_free_result();
    return 0;
}

int db_acl_has_right(u64_t userid, u64_t mboxid, const char *right_flag)
{
	int result;
	int owner_result;
	
	trace(TRACE_DEBUG, "%s,%s: checking ACL for user [%llu] on "
	      "mailbox [%llu]", __FILE__, __FUNCTION__, userid, mboxid);
	owner_result = db_user_is_mailbox_owner(userid, mboxid);

	if (owner_result < 0) {
		trace(TRACE_ERROR, "%s,%s: error checking mailbox ownership.",
		      __FILE__, __FUNCTION__);
		return -1;
	}
	
	if (owner_result == 1)
		return 1;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT * FROM acl "
		 "WHERE user_id = '%llu' "
		 "AND mailbox_id = '%llu' "
		 "AND %s = '1'", userid, mboxid, right_flag);
	
	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: error finding acl_right",
		      __FILE__, __FUNCTION__);
		return -1;
	}
	
	if (db_num_rows() == 0)
		result = 0;
	else
		result = 1;
	
	db_free_result();
	return result;
}

static int db_acl_has_acl(u64_t userid, u64_t mboxid)
{
	int result;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT user_id, mailbox_id FROM acl "
		 "WHERE user_id = '%llu' AND mailbox_id = '%llu'",
		 userid, mboxid);
	
	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: Error finding ACL entry",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	if (db_num_rows() == 0)
		result = 0;
	else
		result = 1;

	db_free_result();
	return result;
}

static int db_acl_create_acl(u64_t userid, u64_t mboxid)
{
	snprintf(query, DEF_QUERYSIZE,
		 "INSERT INTO acl (user_id, mailbox_id) "
		 "VALUES ('%llu', '%llu')", userid, mboxid);

	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: Error creating ACL entry for user "
		      "[%llu], mailbox [%llu]."
		      __FILE__, __FUNCTION__, userid, mboxid);
		return -1;
	}

	return 1;
}

int db_acl_set_right(u64_t userid, u64_t mboxid, const char *right_flag,
		     int set)
{
	int owner_result;
	int result;

	assert (set == 0 || set == 1);

	trace(TRACE_DEBUG, "%s, %s: Setting ACL for user [%llu], mailbox "
	      "[%llu].", __FILE__, __FUNCTION__, userid, mboxid);

	owner_result = db_user_is_mailbox_owner(userid, mboxid);
	if (owner_result < 0) {
		trace(TRACE_ERROR, "%s,%s: error checking ownership of "
		      "mailbox.", __FILE__, __FUNCTION__);
		return -1;
	}
	if (owner_result == 1) 
		return 0;

	// if necessary, create ACL for user, mailbox
	result = db_acl_has_acl(userid, mboxid);
	if (result == -1) {
		trace(TRACE_ERROR, "%s,%s: Error finding acl for user "
		      "[%llu], mailbox [%llu]",
		      __FILE__, __FUNCTION__, userid, mboxid);
		return -1;
	}
	
	if (result == 0) {
		if (db_acl_create_acl(userid, mboxid) == -1) {
			trace(TRACE_ERROR, "%s,%s: Error creating ACL for "
			      "user [%llu], mailbox [%llu]",
			      __FILE__, __FUNCTION__, userid, mboxid);
			return -1;
		}
	}
		
	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE acl SET %s = '%i' "
		 "WHERE user_id = '%llu' AND mailbox_id = '%llu'",
		 right_flag, set, userid, mboxid);

	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: Error updating ACL for user "
		      "[%llu], mailbox [%llu].", __FILE__, __FUNCTION__,
		      userid, mboxid);
		return -1;
	}
	trace(TRACE_DEBUG, "%s,%s: Updated ACL for user [%llu], "
	      "mailbox [%llu].", __FILE__, __FUNCTION__, userid, mboxid);
	return 1;
}
		      
int db_acl_delete_acl(u64_t userid, u64_t mboxid)
{
	trace(TRACE_DEBUG, "%s,%s: deleting ACL for user [%llu], "
	      "mailbox [%llu].", __FILE__, __FUNCTION__, userid, mboxid);

	snprintf(query, DEF_QUERYSIZE,
		 "DELETE FROM acl "
		 "WHERE user_id = '%llu' AND mailbox_id = '%llu'",
		 userid, mboxid);
	
	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: error deleting ACL",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	return 1;
}

int db_acl_get_identifier(u64_t mboxid, struct list *identifier_list)
{
	unsigned i, n;
	char *result_string;

	assert(identifier_list != NULL);

	list_init(identifier_list);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT users.userid FROM users, acl "
		 "WHERE acl.mailbox_id = '%llu' "
		 "AND users.user_idnr = acl.user_id",
		 mboxid);
	
	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: error getting acl identifiers "
		      "for mailbox [%llu].", __FILE__, __FUNCTION__, mboxid);
		return -1;
	}

	n = db_num_rows();
	for (i = 0; i < n; i++) {
		result_string = db_get_result(i, 0);
		trace(TRACE_DEBUG, "%s,%s: adding %s to identifier list",
		      __FILE__, __FUNCTION__, result_string);
		if (!list_nodeadd(identifier_list,
				  result_string, strlen(result_string) + 1)) {
			db_free_result();
			return -2;
		}
	}
	db_free_result();
	return 1;
}

int db_get_mailbox_owner(u64_t mboxid, u64_t *owner_id)
{
	char *result_string;
       
	assert(owner_id != NULL);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT owner_idnr FROM mailboxes "
		 "WHERE mailbox_idnr = '%llu'", mboxid);
	
	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: error finding owner of mailbox "
		      "[%llu]", __FILE__, __FUNCTION__, mboxid);
		return -1;
	}
	
	if (db_num_rows() == 0) {
		db_free_result();
		return 0;
	} else {
		result_string = db_get_result(0, 0);
		*owner_id = result_string ? strtoull(result_string, NULL, 10):
			0;
		db_free_result();
		if (*owner_id == 0)
			return 0;
		else
			return 1;
	}
}

int db_user_is_mailbox_owner(u64_t userid, u64_t mboxid) 
{
	int result;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT mailbox_idnr FROM mailboxes "
		 "WHERE mailbox_idnr = '%llu' "
		 "AND owner_idnr = '%llu'",
		 mboxid, userid);
	
	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: error checking if user [%llu] is "
		      "owner of mailbox [%llu]", __FILE__, __FUNCTION__,
		      userid, mboxid);
		return -1;
	}

	if (db_num_rows() == 0)
		result = 0;
	else 
		result = 1;
	
	db_free_result();
	return result;
}
