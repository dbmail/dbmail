/* $Id$
 * Functions for connecting and talking to the Mysql database */

#include "dbmysql.h"
#include "config.h"
#include "pop3.h"
#include "dbmd5.h"

#define DEF_QUERYSIZE 1024

MYSQL conn;  
MYSQL_RES *res;
MYSQL_ROW row;

int db_connect ()
{
  /* connecting */
  mysql_init(&conn);
  mysql_real_connect (&conn,HOST,USER,PASS,MAILDATABASE,0,NULL,0); 

#ifdef mysql_errno
  if (mysql_errno(&conn)) {
    trace(TRACE_ERROR,"dbconnect(): mysql_real_connect failed: %s",mysql_error(&conn));
    return -1;
  }
#endif
	
  /* selecting the right database 
	  don't know if this needs to stay */
/*   if (mysql_select_db(&conn,MAILDATABASE)) {
    trace(TRACE_ERROR,"dbconnect(): mysql_select_db failed: %s",mysql_error(&conn));
    return -1;
  }  */

  return 0;
}

unsigned long db_insert_result ()
{
  unsigned long insert_result;
  insert_result=mysql_insert_id(&conn);
  return insert_result;
}


int db_query (char *query)
{
  if (mysql_real_query(&conn, query,strlen(query)) <0) 
    {
      trace(TRACE_ERROR,"db_query(): mysql_real_query failed: %s",mysql_error(&conn)); 
      return -1;
    }
  return 0;
}

unsigned long db_insert_message (unsigned long *useridnr)
{
  char *ckquery;
  /* allocating memory for query */
  memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);

  sprintf (ckquery,"INSERT INTO message(useridnr,messagesize,status,unique_id) VALUES (%lu,0,0,\" \")",
	   *useridnr);

  trace (TRACE_DEBUG,"db_insert_message(): inserting message query [%s]",ckquery);
  if (db_query (ckquery)==-1)
	{
	free(ckquery);
	trace(TRACE_STOP,"db_insert_message(): dbquery failed");
	}	
  free (ckquery);
  return db_insert_result();
}


unsigned long db_update_message (unsigned long *messageidnr, char *unique_id,
		unsigned long messagesize)
{
	char *ckquery;
	/* allocating memory for query */
  memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);

  sprintf (ckquery,
		  "UPDATE message SET messagesize=%lu, unique_id=\"%s\" where messageidnr=%lu",
		  messagesize, unique_id, *messageidnr);
  
  trace (TRACE_DEBUG,"db_update_message(): updating message query [%s]",ckquery);
  if (db_query (ckquery)==-1)
	{
	free(ckquery);
	trace(TRACE_STOP,"db_update_message(): dbquery failed");
	}	
  free (ckquery);
  return 0;
}


unsigned long db_insert_message_block (char *block, int messageidnr)
{
  char *escblk, *tmpquery;
  /* allocate memory twice as much, for eacht character might be escaped */
  memtst((escblk=(char *)malloc((strlen(block)*2)))==NULL);
  mysql_escape_string (escblk,block,strlen(block));

  /* add an extra 500 characters for the query */
  memtst((tmpquery=(char *)malloc(strlen(escblk)+500))==NULL);
	
  sprintf (tmpquery,"INSERT INTO messageblk(messageblk,blocksize,messageidnr) VALUES (\"%s\",%d,%d)",escblk,strlen(block),messageidnr);

  if (db_query (tmpquery)==-1)
    {
      free(tmpquery);
      trace(TRACE_STOP,"db_insert_message_block(): dbquery failed");
    }
  /* freeing buffers */
  free (tmpquery);
  free (escblk);
  return db_insert_result(&conn);
}


int db_check_user (char *username, struct list *userids) 
{
  char *ckquery;
  int occurences=0;
  unsigned long messageid;
	
  trace(TRACE_DEBUG,"db_check_user(): checking user [%s] in alias table",username);
  memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);
  sprintf (ckquery, "SELECT * FROM aliases WHERE alias=\"%s\"",username);
  trace(TRACE_DEBUG,"db_check_user(): executing query : [%s]",ckquery);
  if (db_query(ckquery)==-1)
    {
      free(ckquery);
      return occurences;
    }
  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_check_user: mysql_store_result failed: %s",mysql_error(&conn));
      free(ckquery);
      return occurences;
    }
  if (mysql_num_rows(res)<1) 
    {
      trace (TRACE_DEBUG,"db_check_user(): user %s not in aliases table", username);
      mysql_free_result(res);
      free(ckquery);
      return occurences; 
    } 
	
  /* row[2] is the idnumber */
  while ((row = mysql_fetch_row(res))!=NULL)
    {
      occurences++;
      messageid=atol(row[2]);
      list_nodeadd(userids, &messageid,sizeof(messageid));
    }

  trace(TRACE_INFO,"db_check_user(): user [%s] has [%d] entries",username,occurences);
  mysql_free_result(res);
  return occurences;
}

int db_send_message_lines (void *fstream, unsigned long messageidnr, unsigned long lines)
{
  /* this function writes the header to stream */
  char *ckquery;
  char *currpos, *prevpos, *nextpos;

  memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);
  sprintf (ckquery, "SELECT * FROM messageblk WHERE messageidnr=%li",
	   messageidnr);
  if (db_query(ckquery)==-1)
    {
      free(ckquery);
      return 0;
    }
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_send_message_lines: mysql_store_result failed: %s",mysql_error(&conn));
      free(ckquery);
      return 0;
    }
  if (mysql_num_rows(res)<1)
    {
      trace (TRACE_ERROR,"db_send_message_lines(): no messageblks for messageid %lu",messageidnr);
      mysql_free_result(res);
      free(ckquery);
      return 0;
    }

	trace (TRACE_DEBUG,"db_send_message_lines(): sending header first");

	row = mysql_fetch_row(res);
	if (row!=NULL)
			fprintf ((FILE *)fstream,"%s",row[2]);
	else 
		{
			trace(TRACE_MESSAGE,"db_send_message_lines(): this is weird, now result rows!");
			return 0;
		}
	
	trace (TRACE_DEBUG,"db_send_message_lines(): sending [%lu] lines from message [%lu]",lines,messageidnr);
  
  while (((row = mysql_fetch_row(res))!=NULL) && (lines>0))
	{
	/* we're going to do this one line at the time */  
	prevpos = row[2];
	nextpos = prevpos;
	while ((lines>0) && (nextpos!=NULL)) 
		{
		/* search for a newline character in prevpos (which is the buffer) */
		currpos=strchr(prevpos,'\n');	
		trace (TRACE_DEBUG,"db_send_message_lines(): linesize %d",currpos-prevpos);
		trace (TRACE_DEBUG, "db_send_message_lines(): newline character found at %d",currpos);
			if ((currpos!=NULL) && (strlen (currpos)>0))
				{
				/* newline found */
				nextpos=currpos+1;
				/* to delimiter the buffer for fprintf */
		trace (TRACE_DEBUG,"db_send_message_lines(): before linesize %d",strlen(prevpos));
				currpos[0]='\0';	
		trace (TRACE_DEBUG,"db_send_message_lines(): after linesize %d",strlen(prevpos));

				/* the \n is added because it was stripped */
				trace (TRACE_DEBUG,"db_send_message_lines(): sending line[%s], linecounter [%lu]",prevpos,lines);
				fprintf ((FILE *)fstream,"%s\n",prevpos);
				}
			else
				{
				trace (TRACE_DEBUG,"db_send_message_lines(): sending line[%s], linecounter [%lu]",prevpos,lines);
				/* \n needs to be included because it was set to \0 */
				fprintf ((FILE *)fstream,"%s",prevpos);
				nextpos=NULL;
				}
			lines--;
			/* set prevpos to the new position */
			prevpos=nextpos;
		}
	}
   /* delimiter */
   fprintf ((FILE *)fstream,"\r\n.\r\n");
   mysql_free_result(res);
   return 1;
}

int db_send_message (void *fstream, unsigned long messageidnr)
{
  /* this function writes the messageid to stream 
   * returns -1 on err, 1 on success */
  char *ckquery;

  memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);
  sprintf (ckquery, "SELECT * FROM messageblk WHERE messageidnr=%lu",
	   messageidnr);
  if (db_query(ckquery)==-1)
    {
      free(ckquery);
      return -1;
    }
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_send_message(): mysql_store_result failed:  %s",mysql_error(&conn));
      free(ckquery);
      return -1;
    }
  if (mysql_num_rows(res)<1)
    {
      trace (TRACE_ERROR,"db_send_message(): no message blocks for user %lu",messageidnr);
      free(ckquery);
      return -1;
    }
		
  trace(TRACE_DEBUG,"db_send_message(): retrieving message=[%lu]",
	messageidnr);
  while ((row = mysql_fetch_row(res))!=NULL)
    {
      fprintf ((FILE *)fstream,"%s",row[2]);
    }
  trace(TRACE_DEBUG,"db_send_message(): done sending, freeing result");
  mysql_free_result(res);
		
  /* end of stream for pop 
		 * has to be send on a clearline */

  fprintf ((FILE *)fstream,"\r\n.\r\n");
  return 1;
}

unsigned long db_validate (char *user, char *password)
{
  /* returns useridnr on OK, 0 on validation failed, -1 on error */
  char *ckquery;

  memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);
  sprintf (ckquery, "SELECT useridnr FROM user WHERE userid=\"%s\" AND passwd=\"%s\"",
	   user,password);
	
  if (db_query(ckquery)==-1)
    {
      free(ckquery);
      return -1;
    }
	
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_validate(): mysql_store_result failed:  %s",mysql_error(&conn));
      free(ckquery);
      return -1;
    }

  if (mysql_num_rows(res)<1)
    {
      /* no such user found */
      free(ckquery);
      return 0;
    }
	
  row = mysql_fetch_row(res);
	
  return atoi(row[0]);
}

unsigned long db_md5_validate (char *username,unsigned char *md5_apop_he, char *apop_stamp)
{
  /* returns useridnr on OK, 0 on validation failed, -1 on error */
  char *ckquery;
  char *checkstring;
  unsigned char *md5_apop_we;
	
  memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);
  sprintf (ckquery, "SELECT passwd,useridnr FROM mailbox WHERE userid=\"%s\"",username);
	
  if (db_query(ckquery)==-1)
    {
      free(ckquery);
      return -1;
    }
	
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_md5_validate(): mysql_store_result failed:  %s",mysql_error(&conn));
      free(ckquery);
      return -1;
    }

  if (mysql_num_rows(res)<1)
    {
      /* no such user found */
      free(ckquery);
      return 0;
    }
	
  row = mysql_fetch_row(res);
	
	/* now authenticate using MD5 hash comparisation 
	 * row[0] contains the password */

  trace (TRACE_DEBUG,"db_md5_validate(): apop_stamp=[%s], userpw=[%s]",apop_stamp,row[0]);
	
  memtst((checkstring=(char *)malloc(strlen(apop_stamp)+strlen(row[0])+2))==NULL);
  sprintf(checkstring,"%s%s",apop_stamp,row[0]);

  md5_apop_we=makemd5(checkstring);
	
  trace(TRACE_DEBUG,"db_md5_validate(): checkstring for md5 [%s] -> result [%s]",checkstring,
	md5_apop_we);
  trace(TRACE_DEBUG,"db_md5_validate(): validating md5_apop_we=[%s] md5_apop_he=[%s]",
	md5_apop_we, md5_apop_he);

  if (strcmp(md5_apop_he,makemd5(checkstring))==0)
    {
      trace(TRACE_MESSAGE,"db_md5_validate(): user [%s] is validated using APOP",username);
      return atoi(row[1]);
    }
	
  trace(TRACE_MESSAGE,"db_md5_validate(): user [%s] could not be validated",username);
  return 0;
}


int db_createsession (unsigned long useridnr, struct session *sessionptr)
{
  /* returns 1 with a successfull session, -1 when something goes wrong 
   * sessionptr is changed with the right session info
   * useridnr is the userid index for the user whose mailbox we're viewing */
	
  /* first we do a query on the messages of this user */

  char *ckquery;
  struct message tmpmessage;
  unsigned long messagecounter=0;
	
  memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);
  /* query is <2 because we don't want deleted messages 
   * the unique_id should not be empty, this could mean that the message is still being delivered */
  sprintf (ckquery, "SELECT * FROM message WHERE useridnr=%li AND status<002 AND unique_id!=\"\" order by status ASC",
	   useridnr);

  if (db_query(ckquery)==-1)
    {
      free(ckquery);
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_validate(): mysql_store_result failed:  %s",mysql_error(&conn));
      free(ckquery);
      return -1;
    }
		
  sessionptr->totalmessages=0;
  sessionptr->totalsize=0;

  
  if ((messagecounter=mysql_num_rows(res))<1)
    {
      /* there are no messages for this user */
      return 1;
    }

  /* messagecounter is total message, +1 tot end at message 1 */
  messagecounter+=1;
	 
  /* filling the list */
	
  trace (TRACE_DEBUG,"db_create_session(): adding items to list");
  while ((row = mysql_fetch_row(res))!=NULL)
    {
      tmpmessage.msize=atol(row[2]);
      tmpmessage.realmessageid=atol(row[0]);
      tmpmessage.messagestatus=atoi(row[3]);
      strncpy(tmpmessage.uidl,row[4],UID_SIZE);
		
      tmpmessage.virtual_messagestatus=atoi(row[3]);
		
      sessionptr->totalmessages+=1;
      sessionptr->totalsize+=tmpmessage.msize;
      /* descending to create inverted list */
      messagecounter-=1;
      tmpmessage.messageid=messagecounter;
      list_nodeadd (&sessionptr->messagelst, &tmpmessage, sizeof (tmpmessage));
    }
	
  trace (TRACE_DEBUG,"db_create_session(): adding succesfull");
	
	/* setting all virtual values */
  sessionptr->virtual_totalmessages=sessionptr->totalmessages;
  sessionptr->virtual_totalsize=sessionptr->totalsize;
	
  mysql_free_result(res); 
  return 1;
}
		
int db_update_pop (struct session *sessionptr)
{
  char *ckquery;
  struct element *tmpelement;
		
  memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);

  /* get first element in list */
  tmpelement=list_getstart(&sessionptr->messagelst);
	

  while (tmpelement!=NULL)
    {
      /* check if they need an update in the database */
      if (((struct message *)tmpelement->data)->virtual_messagestatus!=
	  ((struct message *)tmpelement->data)->messagestatus) 
	{
	  /* yes they need an update, do the query */
	  sprintf (ckquery, "UPDATE message set status=%lu WHERE messageidnr=%lu",
		   ((struct message *)tmpelement->data)->virtual_messagestatus,
		   ((struct message *)tmpelement->data)->realmessageid);
	
				/* FIXME: a message could be deleted already if it has been accessed
				 * by another interface and be deleted by sysop
				 * we need a check if the query failes because it doesn't excists anymore
				 * now it will just bailout */
	
	  if (db_query(ckquery)==-1)
	    {
	      trace(TRACE_ERROR,"db_update_pop(): could not execute query: []");
	      free(ckquery);
	      return -1;
	    }
	}
      tmpelement=tmpelement->nextnode;
    }
  return 0;
}

int db_disconnect()
{
  return 0;
}


/*
 * db_findmailbox()
 *
 * checks wheter the mailbox designated by 'name' exists for user 'useridnr'
 *
 * returns 0 if the mailbox is not found, 
 * (unsigned)(-1) on error,
 * or the UID of the mailbox otherwise.
 */
unsigned long db_findmailbox(const char *name, unsigned long useridnr)
{
  char query[DEF_QUERYSIZE];
  unsigned long id;

  snprintf(query, DEF_QUERYSIZE, "SELECT mailboxidnr FROM mailbox WHERE name='%s' AND owneridnr=%lu",
	   name, useridnr);
  
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_findmailbox(): could not select mailbox '%s'\n",name);
      return (unsigned long)(-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_findmailbox(): mysql_store_result failed:  %s\n",mysql_error(&conn));
      return (unsigned long)(-1);
    }
  
  
  row = mysql_fetch_row(res);
  if (row)
    id = atoi(row[0]);
  else
    id = 0;

  mysql_free_result(res);

  return id;
}
  

/*
 * db_getmailbox()
 * 
 * gets mailbox info from dbase
 *
 * returns -1 on error, 0 on success
 */
int db_getmailbox(mailbox_t *mb, unsigned long userid)
{
  char query[DEF_QUERYSIZE];

  snprintf(query, DEF_QUERYSIZE, 
	   "SELECT permission,"
	   "seen_flag,"
	   "answered_flag,"
	   "deleted_flag,"
	   "flagged_flag,"
	   "recent_flag,"
	   "draft_flag "
	   " FROM mailbox WHERE mailboxidnr = %lu", mb->uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_getmailbox(): could not select mailbox\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getmailbox(): mysql_store_result failed:  %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);
  if (!row)
    {
      trace(TRACE_ERROR,"db_getmailbox(): invalid mailbox id specified\n");
      return -1;
    }

  mb->permission = atoi(row[0]);
  mb->flags = 0;

  if (row[1]) mb->flags |= IMAPFLAG_SEEN;
  if (row[2]) mb->flags |= IMAPFLAG_ANSWERED;
  if (row[3]) mb->flags |= IMAPFLAG_DELETED;
  if (row[4]) mb->flags |= IMAPFLAG_FLAGGED;
  if (row[5]) mb->flags |= IMAPFLAG_RECENT;
  if (row[6]) mb->flags |= IMAPFLAG_DRAFT;

  mysql_free_result(res);


  /* now select messages: ALL */
  snprintf(query, DEF_QUERYSIZE, "SELECT COUNT(*) FROM message WHERE mailboxidnr = %lu", mb->uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_getmailbox(): could not select messages\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getmailbox(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);
  if (row)
    mb->exists = atoi(row[0]);
  else
    mb->exists = 0;

  mysql_free_result(res);


  /* now select messages:  RECENT */
  snprintf(query, DEF_QUERYSIZE, "SELECT COUNT(*) FROM message WHERE recent_flag=1 AND "
	   "mailboxidnr = %lu", mb->uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_getmailbox(): could not select recent messages\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getmailbox(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);
  if (row)
    mb->recent = atoi(row[0]);
  else
    mb->recent = 0;

  mysql_free_result(res);
  

  /* now select messages:  UNSEEN */
  snprintf(query, DEF_QUERYSIZE, "SELECT COUNT(*) FROM message WHERE seen_flag=0 AND "
	   "mailboxidnr = %lu", mb->uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_getmailbox(): could not select unseen messages\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getmailbox(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);
  if (row)
    mb->unseen = atoi(row[0]);
  else
    mb->unseen = 0;

  mysql_free_result(res);


  /* done */
  return 0;
}


/*
 * db_createmailbox()
 *
 * creates a mailbox for the specified user
 * does not perform hierarchy checks
 *
 * returns -1 on error, 0 on succes
 */
int db_createmailbox(const char *name, unsigned long ownerid)
{
  char query[DEF_QUERYSIZE];

  snprintf(query, DEF_QUERYSIZE, "INSERT INTO mailbox (name, owneridnr,"
	   "seen_flag, answered_flag, deleted_flag, flagged_flag, recent_flag, draft_flag, permission)"
	   " VALUES ('%s', %lu, 1, 1, 1, 1, 1, 1, 2)", name,ownerid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_createmailbox(): could not create mailbox\n");
      return -1;
    }

  return 0;
}
  

/*
 * db_listmailboxchildren()
 *
 * produces a list containing the UID's of the specified mailbox' children
 *
 * returns -1 on error, 0 on succes
 */
int db_listmailboxchildren(unsigned long uid, unsigned long **children, int *nchildren)
{
  char query[DEF_QUERYSIZE];
  char *name;
  int i;

  /* retrieve the name of this mailbox */
  snprintf(query, DEF_QUERYSIZE, "SELECT name FROM mailbox WHERE"
	   " mailboxidnr = %lu", uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_listmailboxchildren(): could not retrieve mailbox name\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_listmailboxchildren(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);
  if (!row)
    {
      /* empty set */
      children = NULL;
      nchildren = 0;
      mysql_free_result(res);
      return 0;
    }

  /* alloc mem */
  name = (char*)malloc((strlen(row[0])+1) *sizeof(char));
  if (!name)
    {
      /* out of mem */
      trace(TRACE_ERROR,"db_listmailboxchildren(): out of memory\n");
      mysql_free_result(res);
      children = NULL;
      nchildren = 0;
      return -1;
    }

  /* copy name */
  strcpy(name, row[0]);
  mysql_free_result(res);
  
  /* now find the children */
  snprintf(query, DEF_QUERYSIZE, "SELECT mailboxidnr FROM mailbox WHERE name LIKE '%s/%%'",name);
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_listmailboxchildren(): could not retrieve mailbox name\n");
      free(name);
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_listmailboxchildren(): mysql_store_result failed: %s\n",mysql_error(&conn));
      free(name);
      return -1;
    }

  row = mysql_fetch_row(res);
  if (!row)
    {
      /* empty set */
      *children = NULL;
      nchildren = 0;
      mysql_free_result(res);
      free(name);
      return 0;
    }

  *nchildren = mysql_num_rows(res);
  if (*nchildren == 0)
    {
      *children = NULL;
      return 0;
    }
  *children = (unsigned long*)malloc(sizeof(unsigned long) * (*nchildren));

  if (!(*children))
    {
      /* out of mem */
      trace(TRACE_ERROR,"db_listmailboxchildren(): out of memory\n");
      mysql_free_result(res);
      free(name);
      return -1;
    }

  i = 0;
  do
    {
      if (i == *nchildren)
	{
	  /*  big fatal */
	  free(*children);
	  *children = NULL;
	  *nchildren = 0;
	  free(name);
	  mysql_free_result(res);
	  trace(TRACE_ERROR, "db_listmailboxchildren: data when none expected.\n");
	  return -1;
	}

      (*children)[i++] = strtoul(row[0], NULL, 10);
    }
  while ((row = mysql_fetch_row(res)));

  mysql_free_result(res);
  free(name);

  return 0; /* success */
}
 

/*
 * db_removemailbox()
 *
 * removes the mailbox indicated by UID/ownerid and all the messages associated with it
 * the mailbox SHOULD NOT have any children but no checks are performed
 *
 * returns -1 on failure, 0 on succes
 */
int db_removemailbox(unsigned long uid, unsigned long ownerid)
{
  char query[DEF_QUERYSIZE];

  /* first update messages belonging to this mailbox: mark as deleted (status 3) */
  snprintf(query, DEF_QUERYSIZE, "UPDATE message SET status=3 WHERE"
	   " mailboxidnr = %lu", uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_removemailbox(): could not update messages in mailbox\n");
      return -1;
    }

  /* now remove mailbox */
  snprintf(query, DEF_QUERYSIZE, "DELETE FROM mailbox WHERE mailboxidnr = %lu", uid);
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_removemailbox(): could not remove mailbox\n");
      return -1;
    }
  
  /* done */
  return 0;
}

  

    

