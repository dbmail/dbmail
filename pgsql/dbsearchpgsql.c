/*
 * $Id$
 * (c) 2000-2002 IC&S, The Netherlands (http://www.ic-s.nl)
 *
 * function implementations for searching messages
 */

#include "../dbsearch.h"
#include "../db.h"
/*#include "/usr/local/pgsql/include/libpq-fe.h"*/
/*#include "/usr/include/postgresql/libpq-fe.h"*/
#include "/Library/PostgreSQL/include/libpq-fe.h"

#include "../rfcmsg.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* 
 * var's from dbpgsql.c: 
 */

extern PGconn *conn;  
extern PGresult *res;
extern PGresult *checkres;
extern char *query;
extern char *value; /* used for PQgetvalue */
extern unsigned long PQcounter; /* used for PQgetvalue loops */

const char *month_desc[]= 
{ 
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};


/* used only locally */
unsigned db_binary_search(const u64_t *array, int arraysize, u64_t key);
int db_exec_search(mime_message_t *msg, search_key_t *sk, u64_t msguid);
int db_search_range(db_pos_t start, db_pos_t end, const char *key, u64_t msguid);
int num_from_imapdate(const char *date);


/*
 * db_search()
 *
 * searches the dbase for messages belonging to mailbox mb and matching the specified key
 * entries of rset will be set for matching msgs (using their MSN as identifier)
 * 
 * returns 0 on succes, -1 on dbase error, -2 on memory error,
 * 1 on synchronisation error (search returned a UID which was not in the MSN-list,
 * mailbox should be updated)
 */
int db_search(int *rset, int setlen, const char *key, mailbox_t *mb)
{
  u64_t uid;
  int msn;
  unsigned i;

  if (!key)
    return -2;

  memset(rset, 0, setlen * sizeof(int));

  snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr FROM messages WHERE mailbox_idnr = %llu::bigint "
	   "AND status<2 AND unique_id!='' AND %s", mb->uid, key);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_search(): could not execute query\n");
      return (-1);
    }

  for (i=0; i<PQntuples(res); i++)
    {
      uid = strtoull(PQgetvalue(res, i, 0), NULL, 10);
      msn = db_binary_search(mb->seq_list, mb->exists, uid);

      if (msn == -1 || msn >= setlen)
	{
	  PQclear(res);
	  return 1;
	}

      rset[msn] = 1;
    }
	  
  PQclear(res);
  return 0;
}



/*
 * db_search_parsed()
 *
 * searches messages in mailbox mb matching the specified criterion.
 * to be used with search keys that require message parsing
 */
int db_search_parsed(int *rset, int setlen, search_key_t *sk, mailbox_t *mb)
{
  int i,result;
  mime_message_t msg;

  if (mb->exists != setlen)
    return 1;

  memset(rset, 0, sizeof(int)*setlen);

  for (i=0; i<setlen; i++)
    {
      memset(&msg, 0, sizeof(msg));

      result = db_fetch_headers(mb->seq_list[i], &msg);
      if (result != 0)
	continue; /* ignore parse errors */

      if (sk->type == IST_SIZE_LARGER)
	{
	  rset[i] = ((msg.rfcheadersize + msg.bodylines + msg.bodysize) > sk->size) ? 1 : 0;
	}
      else if (sk->type == IST_SIZE_SMALLER)
	{
	  rset[i] = ((msg.rfcheadersize + msg.bodylines + msg.bodysize) < sk->size) ? 1 : 0;
	}
      else
	{
	  rset[i] = db_exec_search(&msg, sk, mb->seq_list[i]);
	}

      db_free_msg(&msg);
    }

  return 0;
}


/*
 * recursively executes a search on the body of a message;
 *
 * returns 1 if the msg matches, 0 if not
 */
int db_exec_search(mime_message_t *msg, search_key_t *sk, u64_t msguid)
{
  struct element *el;
  struct mime_record *mr;
  int i,givendate,sentdate;

  if (!sk->search)
    return 0;

  switch (sk->type)
    {
    case IST_HDR:
      if (list_getstart(&msg->mimeheader))
	{
	  mime_findfield(sk->hdrfld, &msg->mimeheader, &mr);
	  if (mr)
	    {
	      for (i=0; mr->value[i]; i++)
		if (strncasecmp(&mr->value[i], sk->search, strlen(sk->search)) == 0)
		  return 1;
	    }
	}
      if (list_getstart(&msg->rfcheader))
	{
	  mime_findfield(sk->hdrfld, &msg->rfcheader, &mr);
	  if (mr)
	    {
	      for (i=0; mr->value[i]; i++)
		if (strncasecmp(&mr->value[i], sk->search, strlen(sk->search)) == 0)
		  return 1;
	    }
	}

      break;

    case IST_HDRDATE_BEFORE:
    case IST_HDRDATE_ON: 
    case IST_HDRDATE_SINCE:
      /* do not check children */
      if (list_getstart(&msg->rfcheader))
	{
	  mime_findfield("date", &msg->rfcheader, &mr);
	  if (mr && strlen(mr->value) >= strlen("Day, d mon yyyy "))
	                                      /* 01234567890123456 */     
	    {
	      givendate = num_from_imapdate(sk->search);

	      if (mr->value[6] == ' ')
		mr->value[15] = 0;
	      else
		mr->value[16] = 0;

	      sentdate = num_from_imapdate(&mr->value[5]);

	      switch (sk->type)
		{
		case IST_HDRDATE_BEFORE: return sentdate < givendate;
		case IST_HDRDATE_ON:     return sentdate == givendate;
		case IST_HDRDATE_SINCE:  return sentdate > givendate;
		}
	    }
	}
      return 0;

    case IST_DATA_TEXT:
      el = list_getstart(&msg->rfcheader);
      while (el)
	{
	  mr = (struct mime_record*)el->data;
	  
	  for (i=0; mr->field[i]; i++)
	    if (strncasecmp(&mr->field[i], sk->search, strlen(sk->search)) == 0)
	      return 1;

	  for (i=0; mr->value[i]; i++)
	    if (strncasecmp(&mr->value[i], sk->search, strlen(sk->search)) == 0)
	      return 1;
	  
	  el = el->nextnode;
	}

      el = list_getstart(&msg->mimeheader);
      while (el)
	{
	  mr = (struct mime_record*)el->data;
	  
	  for (i=0; mr->field[i]; i++)
	    if (strncasecmp(&mr->field[i], sk->search, strlen(sk->search)) == 0)
	      return 1;

	  for (i=0; mr->value[i]; i++)
	    if (strncasecmp(&mr->value[i], sk->search, strlen(sk->search)) == 0)
	      return 1;
	  
	  el = el->nextnode;
	}

    case IST_DATA_BODY: 
      /* only check body if there are no children */
      if (list_getstart(&msg->children))
	break;

      /* only check text bodies */
      mime_findfield("content-type", &msg->mimeheader, &mr);
      if (mr && strncasecmp(mr->value, "text", 4) != 0)
	break;
	
      mime_findfield("content-type", &msg->rfcheader, &mr);
      if (mr && strncasecmp(mr->value, "text", 4) != 0)
	break;
	
      return db_search_range(msg->bodystart, msg->bodyend, sk->search, msguid);
   }  

  /* no match found yet, try the children */
  el = list_getstart(&msg->children);
  while (el)
    {
      if (db_exec_search((mime_message_t*)el->data, sk, msguid) == 1)
	return 1;
      
      el = el->nextnode;
    }
  return 0;
}


/*
 * db_search_messages()
 *
 * searches the dbase for messages matching the search_keys
 * supported search_keys: 
 * (un)answered
 * (un)deleted
 * (un)seen
 * (un)flagged
 * draft
 * recent
 *
 * results will be an ascending ordered array of message UIDS
 *
 *
 */
int db_search_messages(char **search_keys, u64_t **search_results, int *nsresults,
		       u64_t mboxid)
{
  int i,qidx=0;

  trace(TRACE_WARNING, "db_search_messages(): SEARCH requested, arguments: ");
  for (i=0; search_keys[i]; i++)
    trace(TRACE_WARNING, "%s ", search_keys[i]);
  trace(TRACE_WARNING,"\n");

  qidx = snprintf(query, DEF_QUERYSIZE,
		  "SELECT message_idnr FROM messages WHERE mailbox_idnr = %llu::bigint AND status<2 "
		  "AND unique_id!=''",
		  mboxid);

  i = 0;
  while (search_keys[i])
    {
      if (search_keys[i][0] == '(' || search_keys[i][0] == ')')
	{
	  qidx += sprintf(&query[qidx], " %c",search_keys[i][0]);
	}
      else if (strcasecmp(search_keys[i], "answered") == 0)
	{
	  qidx += sprintf(&query[qidx], " AND answered_flag=1");
	}
      else if (strcasecmp(search_keys[i], "deleted") == 0)
	{
	  qidx += sprintf(&query[qidx], " AND deleted_flag=1");
	}
      else if (strcasecmp(search_keys[i], "seen") == 0)
	{
	  qidx += sprintf(&query[qidx], " AND seen_flag=1");
	}
      else if (strcasecmp(search_keys[i], "flagged") == 0)
	{
	  qidx += sprintf(&query[qidx], " AND flagged_flag=1");
	}
      else if (strcasecmp(search_keys[i], "recent") == 0)
	{
	  qidx += sprintf(&query[qidx], " AND recent_flag=1");
	}
      else if (strcasecmp(search_keys[i], "draft") == 0)
	{
	  qidx += sprintf(&query[qidx], " AND draft_flag=1");
	}
      else if (strcasecmp(search_keys[i], "unanswered") == 0)
	{
	  qidx += sprintf(&query[qidx], " AND answered_flag=0");
	}
      else if (strcasecmp(search_keys[i], "undeleted") == 0)
	{
	  qidx += sprintf(&query[qidx], " AND deleted_flag=0");
	}
      else if (strcasecmp(search_keys[i], "unseen") == 0)
	{
	  qidx += sprintf(&query[qidx], " AND seen_flag=0");
	}
      else if (strcasecmp(search_keys[i], "unflagged") == 0)
	{
	  qidx += sprintf(&query[qidx], " AND flagged_flag=0");
	}
      i++;
    }
      
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_search_messages(): could not execute query\n");
      return (-1);
    }

  *nsresults = PQntuples(res);
  if (*nsresults == 0)
    {
      *search_results = NULL;
      PQclear(res);
      return 0;
    }

  *search_results = (u64_t*)my_malloc(sizeof(u64_t) * *nsresults);
  if (!*search_results)
    {
      trace(TRACE_ERROR, "db_search_messages(): out of memory\n");
      PQclear(res);
      return -1;
    }

  i=0;
  while (i<*nsresults)
    {
      (*search_results)[i] = strtoull(PQgetvalue(res, i, 0),NULL,10);
      i++;
    }
      

  PQclear(res);
  return 0;
}


/*
 * db_binary_search()
 *
 * performs a binary search on array to find key
 * array should be ascending in values
 *
 * returns index of key in array or -1 if not found
 */
unsigned db_binary_search(const u64_t *array, int arraysize, u64_t key)
{
  unsigned low,high,mid;

  low = 0;
  high = arraysize-1;

  while (low <= high && high != (unsigned)(-1))
    {
      mid = (high+low)/2;
      if (array[mid] < key)
	low = mid+1;
      else if (array[mid] > key)
	high = mid-1;
      else
	return mid;
    }

  return -1; /* not found */
}



/* 
 * converts an IMAP date to a number (strictly ascending in date)
 * valid IMAP dates:
 * d-mon-yyyy or dd-mon-yyyy; '-' may be a space
 *               01234567890
 */
int num_from_imapdate(const char *date)
{
  int j=0,i;
  char datenum[] = "YYYYMMDD";
  char sub[4];

  if (date[1] == ' ' || date[1] == '-')
    j = 1;

  strncpy(datenum, &date[7-j], 4);

  strncpy(sub, &date[3-j], 3);
  sub[3] = 0;

  for (i=0; i<12; i++)
    {
      if (strcasecmp(sub, month_desc[i]) == 0)
	break;
    }

  i++;
  if (i > 12)
    i = 12;

  sprintf(&datenum[4], "%02d", i);

  if (j)
    {
      datenum[6] = '0';
      datenum[7] = date[0];
    }
  else
    {
      datenum[6] = date[0];
      datenum[7] = date[1];
    }

  return atoi(datenum);
}
  
