/* $Id$
 * Functions for connecting and talking to the Mysql database */

#include "dbmysql.h"
#include "config.h"
#include "pop3.h"
#include "dbmd5.h"
#include "list.h"
#include "mime.h"
#include "pipe.h"
#include <ctype.h>

#define DEF_QUERYSIZE 1024
#define MSGBUF_WINDOWSIZE (128ul*1024ul)
#define MSGBUF_FORCE_UPDATE -1

#define MAX_EMAIL_SIZE 250

MYSQL conn;  
MYSQL_RES *res,*_msg_result;
MYSQL_ROW row;
MYSQL_ROW _msgrow;
int _msg_fetch_inited = 0;

/*
 * CONDITIONS FOR MSGBUF
 *
 * rowlength         length of current row
 * rowpos            current pos in row (_msgrow[0][rowpos-1] is last read char)
 * msgidx            index within msgbuf, 0 <= msgidx < buflen
 * buflen            current buffer length: msgbuf[buflen] == '\0'
 * zeropos           absolute position (block/offset) of msgbuf[0]
 */

char msgbuf[MSGBUF_WINDOWSIZE];
unsigned long rowlength,msgidx,buflen,rowpos;
db_pos_t zeropos;


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

unsigned long db_get_inboxid (unsigned long *useridnr)
{
	/* returns the mailbox id (of mailbox inbox) for a user or a 0 if no mailboxes were found */
  char *ckquery;
  
  /* allocating memory for query */
  memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);

	sprintf (ckquery,"SELECT mailboxidnr FROM mailbox WHERE name='INBOX' AND owneridnr=%lu",
			*useridnr);

  trace(TRACE_DEBUG,"db_get_inboxid(): executing query : [%s]",ckquery);
  if (db_query(ckquery)==-1)
    {
      free(ckquery);
      return 0;
    }
  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_mailboxid(): mysql_store_result failed: %s",mysql_error(&conn));
      free(ckquery);
      return 0;
    }
  if (mysql_num_rows(res)<1) 
    {
      trace (TRACE_DEBUG,"db_get_mailboxid(): user has no INBOX");
      mysql_free_result(res);
      free(ckquery);
      return 0; 
    } 

	if ((row = mysql_fetch_row(res))==NULL)
	{
		trace (TRACE_DEBUG,"db_insert_message(): fetch_row call failed");
	}
	/* return the inbox id */
	return atoi(row[0]);
}



unsigned long db_insert_message (unsigned long *useridnr)
{
  char *ckquery;
  /* allocating memory for query */
  memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);

  sprintf (ckquery,"INSERT INTO message(mailboxidnr,messagesize,unique_id) VALUES (%lu,0,\" \")",
	   db_get_inboxid(useridnr));

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
  char *ckquery, *tmp;
  int occurences=0;
	
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
	
  /* row[2] is the deliver_to field */
  while ((row = mysql_fetch_row(res))!=NULL)
    {
      occurences++;
		/* created this because it seems that the mysql response (the row[]'s) aren't
			null terminated */ 
		tmp=(char *)row[2];
		tmp[strlen(tmp)+1]='\0';
		/* --- */
      list_nodeadd(userids, tmp,strlen(tmp)+1);
		trace (TRACE_DEBUG,"db_check_user(): adding [%s] to deliver_to address",tmp);
    }

  trace(TRACE_INFO,"db_check_user(): user [%s] has [%d] entries",username,occurences);
  mysql_free_result(res);
  return occurences;
}

int db_send_message_lines (void *fstream, unsigned long messageidnr, long lines)
{
  /* this function writes "lines" to fstream.
	  if lines == -2 then the whole message is dumped to fstream 
	  newlines are rewritten to crlf */

  char *ckquery;
  char *buffer;
  char *nextpos, *tmppos;
  int block_count;
  
  trace (TRACE_DEBUG,"db_send_message_lines(): request for [%d] lines",lines);

  memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);
  memtst ((buffer=(char *)malloc(READ_BLOCK_SIZE*2))==NULL);
  sprintf (ckquery, "SELECT * FROM messageblk WHERE messageidnr=%lu ORDER BY messageblknr ASC",
	   messageidnr);
  trace (TRACE_DEBUG,"db_send_message_lines(): executing query [%s]",ckquery);
  if (db_query(ckquery)==-1)
    {
      free(ckquery);
      return 0;
    }
  if ((res = mysql_use_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_send_message_lines: mysql_store_result failed: %s",mysql_error(&conn));
      free(ckquery);
      return 0;
    }
  /* if (mysql_num_rows(res)<1)
    {
      trace (TRACE_ERROR,"db_send_message_lines(): no messageblks for messageid %lu",messageidnr);
      mysql_free_result(res);
      free(ckquery);
      return 0;
    } */

	trace (TRACE_DEBUG,"db_send_message_lines(): sending [%d] lines from message [%lu]",lines,messageidnr);
  
	block_count=0;

  while (((row = mysql_fetch_row(res))!=NULL) && ((lines>0) || (lines==-2)))
	{
		nextpos=row[2];
		
		/* reset our buffer */
		buffer[0]='\0';

		
		while ((*nextpos!='\0') && ((lines>0) || (lines==-2)))
		{
			if (*nextpos=='\n')
			{

				/* we found an newline, now check if there's
					a return before that */
				
				if (lines!=-2)
					lines--;
				
				if (tmppos!=NULL)
				{
					if (*tmppos=='\r')
						sprintf (buffer,"%s%c",buffer,*nextpos);
					else 
						sprintf (buffer,"%s\r%c",buffer,*nextpos);
				}
				else 
					sprintf (buffer,"%s\r%c",buffer,*nextpos);
			}
			else
			{
				if (*nextpos=='.')
					{
						if (tmppos!=NULL)
							{
								trace (TRACE_DEBUG,"nextpos [%c] tmppos [%c]",*nextpos,*tmppos);
								if (*tmppos=='\n')
									sprintf (buffer,"%s.%c",buffer,*nextpos);
								else
									sprintf (buffer,"%s%c",buffer,*nextpos);
							}
						else 
							sprintf (buffer,"%s%c",buffer,*nextpos);
					}
				else	
					sprintf (buffer,"%s%c",buffer,*nextpos);
			}

			tmppos=nextpos;
				
			/* get the next character */
			nextpos++;
		}
		/* flush our buffer */
		fprintf (fstream,"%s",buffer);
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
  sprintf (ckquery, "SELECT passwd,useridnr FROM user WHERE userid=\"%s\"",username);
	
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
  sprintf (ckquery, "SELECT * FROM message WHERE mailboxidnr=%lu AND status<002 AND unique_id!=\"\" order by status ASC",
	   (db_get_inboxid(&useridnr)));

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
      tmpmessage.msize=atol(row[MESSAGE_MESSAGESIZE]);
      tmpmessage.realmessageid=atol(row[MESSAGE_MESSAGEIDNR]);
      tmpmessage.messagestatus=atoi(row[MESSAGE_STATUS]);
      strncpy(tmpmessage.uidl,row[MESSAGE_UNIQUE_ID],UID_SIZE);
		
      tmpmessage.virtual_messagestatus=atoi(row[MESSAGE_STATUS]);
		
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

unsigned long db_deleted_purge()
{
	/* purges all the messages with a deleted status */
	char *ckquery;
	
	memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);
	
	/* first we're deleting all the messageblks */
	sprintf (ckquery,"SELECT messageidnr FROM message WHERE status=003");
	trace (TRACE_DEBUG,"db_deleted_purge(): executing query [%s]",ckquery);
	
	if (db_query(ckquery)==-1)
	{
		trace(TRACE_ERROR,"db_deleted_purge(): Cound not execute query [%s]",ckquery);
		free(ckquery);
		return -1;
	}
  
	if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_deleted_purge(): mysql_store_result failed:  %s",mysql_error(&conn));
      free(ckquery);
      return -1;
    }
  
	if (mysql_num_rows(res)<1)
    {
      free(ckquery);
      return 0;
    }
	
  while ((row = mysql_fetch_row(res))!=NULL)
	{
		sprintf (ckquery,"DELETE FROM messageblk WHERE messageidnr=%s",row[0]);
		trace (TRACE_DEBUG,"db_deleted_purge(): executing query [%s]",ckquery);
		if (db_query(ckquery)==-1)
			return -1;
	}

	/* messageblks are deleted. Now delete the messages */
	sprintf (ckquery,"DELETE FROM message WHERE status=003");
	trace (TRACE_DEBUG,"db_deleted_purge(): executing query [%s]",ckquery);
	if (db_query(ckquery)==-1)
		return -1;
	
	return mysql_affected_rows(&conn);
}

unsigned long db_set_deleted ()
{
	/* sets al messages with status 002 to status 003 for final
	 * deletion */

	char *ckquery;
	
	memtst((ckquery=(char *)malloc(DEF_QUERYSIZE))==NULL);
	
	/* first we're deleting all the messageblks */
	sprintf (ckquery,"UPDATE message SET status=003 WHERE status=002");
	trace (TRACE_DEBUG,"db_set_deleted(): executing query [%s]",ckquery);

	if (db_query(ckquery)==-1)
	{
		trace(TRACE_ERROR,"db_set_deleted(): Cound not execute query [%s]",ckquery);
		free(ckquery);
		return -1;
	}
 
	return mysql_affected_rows(&conn);
}


int db_disconnect()
{
	mysql_close(&conn);
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
 * calls db_build_msn_list() to build message sequence number list
 *
 * returns 
 *  -1  error
 *   0  success
 *   1  warning: operation not completed: msn list not build
 *               getmailbox() should be called again
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
  snprintf(query, DEF_QUERYSIZE, "SELECT COUNT(*) FROM message WHERE mailboxidnr = %lu AND status!=3", 
	   mb->uid);

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
	   "mailboxidnr = %lu AND status!=3", mb->uid);

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
  

  /* now select messages: UNSEEN */
  snprintf(query, DEF_QUERYSIZE, "SELECT COUNT(*) FROM message WHERE seen_flag=0 AND "
	   "mailboxidnr = %lu AND status!=3", mb->uid);

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

  
  /* now determine the next message UID */
  /*
   * NOTE EXPUNGED MESSAGES ARE SELECTED AS WELL IN ORDER TO BE ABLE TO RESTORE THEM 
   */

  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message ORDER BY messageidnr DESC LIMIT 0,1");
  
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_getmailbox(): could not determine highest message ID\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getmailbox(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);
  if (row)
    mb->msguidnext = atoi(row[0])+1;
  else
    mb->msguidnext = 1; /* empty set: no messages yet in dbase */

  mysql_free_result(res);


  /* build msn list & done */
  return db_build_msn_list(mb);
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
 * matching the search criterion
 *
 * returns -1 on error, 0 on succes
 */
int db_listmailboxchildren(unsigned long uid, unsigned long **children, int *nchildren, 
			   const char *filter)
{
  char query[DEF_QUERYSIZE];
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
  if (row)
    snprintf(query, DEF_QUERYSIZE, "SELECT mailboxidnr FROM mailbox WHERE name LIKE '%s/%s'",
	     row[0],filter);
  else
    snprintf(query, DEF_QUERYSIZE, "SELECT mailboxidnr FROM mailbox WHERE name LIKE '%s'",filter);

  mysql_free_result(res);
  
  /* now find the children */
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
      *children = NULL;
      *nchildren = 0;
      mysql_free_result(res);
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
	  mysql_free_result(res);
	  trace(TRACE_ERROR, "db_listmailboxchildren: data when none expected.\n");
	  return -1;
	}

      (*children)[i++] = strtoul(row[0], NULL, 10);
    }
  while ((row = mysql_fetch_row(res)));

  mysql_free_result(res);

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

  if (db_removemsg(uid) == -1) /* remove all msg */
    return -1;

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


/*
 * db_isselectable()
 *
 * returns 1 if the specified mailbox is selectable, 0 if not and -1 on failure
 */  
int db_isselectable(unsigned long uid)
{
  char query[DEF_QUERYSIZE];

  snprintf(query, DEF_QUERYSIZE, "SELECT no_select FROM mailbox WHERE mailboxidnr = %lu",uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_isselectable(): could not retrieve select-flag\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_isselectable(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);

  if (!row)
    {
      /* empty set, mailbox does not exist */
      mysql_free_result(res);
      return 0;
    }

  if (atoi(row[0]) == 0)
    {    
      mysql_free_result(res);
      return 1;
    }

  mysql_free_result(res);
  return 0;
}
  

/*
 * db_noinferiors()
 *
 * checks if mailbox has no_inferiors flag set
 *
 * returns
 *   1  flag is set
 *   0  flag is not set
 *  -1  error
 */
int db_noinferiors(unsigned long uid)
{
  char query[DEF_QUERYSIZE];

  snprintf(query, DEF_QUERYSIZE, "SELECT no_inferiors FROM mailbox WHERE mailboxidnr = %lu",uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_noinferiors(): could not retrieve noinferiors-flag\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_noinferiors(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);

  if (!row)
    {
      /* empty set, mailbox does not exist */
      mysql_free_result(res);
      return 0;
    }

  if (atoi(row[0]) == 1)
    {    
      mysql_free_result(res);
      return 1;
    }

  mysql_free_result(res);
  return 0;
}


/*
 * db_setselectable()
 *
 * set the noselect flag of a mailbox on/off
 * returns 0 on success, -1 on failure
 */
int db_setselectable(unsigned long uid, int value)
{
  char query[DEF_QUERYSIZE];

  snprintf(query, DEF_QUERYSIZE, "UPDATE mailbox SET no_select = %d WHERE mailboxidnr = %lu",
	   (!value), uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_setselectable(): could not set noselect-flag\n");
      return -1;
    }

  return 0;
}


/*
 * db_removemsg()
 *
 * removes ALL messages from a mailbox
 * removes by means of setting status to 3
 *
 * returns -1 on failure, 0 on success
 */
int db_removemsg(unsigned long uid)
{
  char query[DEF_QUERYSIZE];

  /* update messages belonging to this mailbox: mark as deleted (status 3) */
  snprintf(query, DEF_QUERYSIZE, "UPDATE message SET status=3 WHERE"
	   " mailboxidnr = %lu", uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_removemsg(): could not update messages in mailbox\n");
      return -1;
    }

  return 0; /* success */
}


/*
 * db_expunge()
 *
 * removes all messages from a mailbox with delete-flag
 * removes by means of setting status to 3
 * makes a list of delete msg UID's 
 *
 * returns -1 on failure, 0 on success
 */
int db_expunge(unsigned long uid,unsigned long **msgids,int *nmsgs)
{
  char query[DEF_QUERYSIZE];
  int i;

  /* first select msg UIDs */
  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message WHERE"
	   " mailboxidnr = %lu AND deleted_flag=1 ORDER BY messageidnr DESC", uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_expunge(): could not select messages in mailbox\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_expunge(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  /* now alloc mem */
  *nmsgs = mysql_num_rows(res);
  *msgids = (unsigned long *)malloc(sizeof(unsigned long) * (*nmsgs));
  if (!(*msgids))
    {
      /* out of mem */
      *nmsgs = 0;
      mysql_free_result(res);
      return -1;
    }

  /* save ID's in array */
  i = 0;
  while ((row = mysql_fetch_row(res)))
    {
      (*msgids)[i++] = strtoul(row[0], NULL, 10);
    }
  mysql_free_result(res);
  
  /* update messages belonging to this mailbox: mark as expunged (status 3) */
  snprintf(query, DEF_QUERYSIZE, "UPDATE message SET status=3 WHERE"
	   " mailboxidnr = %lu AND deleted_flag=1", uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_expunge(): could not update messages in mailbox\n");
      free(*msgids);
      *nmsgs = 0;
      return -1;
    }

  return 0; /* success */
}
    

/*
 * db_movemsg()
 *
 * moves all msgs from one mailbox to another
 * returns -1 on error, 0 on success
 */
int db_movemsg(unsigned long to, unsigned long from)
{
  char query[DEF_QUERYSIZE];

  /* update messages belonging to this mailbox: mark as deleted (status 3) */
  snprintf(query, DEF_QUERYSIZE, "UPDATE message SET mailboxidnr=%ld WHERE"
	   " mailboxidnr = %lu", to, from);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_movemsg(): could not update messages in mailbox\n");
      return -1;
    }

  return 0; /* success */
}


/*
 * db_copymsg()
 *
 * copies a msg to a specified mailbox
 * returns 0 on success, -1 on failure
 */
int db_copymsg(unsigned long msgid, unsigned long destmboxid)
{
  char query[DEF_QUERYSIZE];
  char *insert;
  unsigned long newid,*lengths,len,allocsize;

  /* retrieve message */
  snprintf(query, DEF_QUERYSIZE, "SELECT * FROM message WHERE messageidnr = %lu", msgid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not select messages\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_copymsg(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);

  if (!row)
    {
      /* empty set, message does not exist ??? */
      trace(TRACE_ERROR,"db_copymsg(): requested msg (id %lu) does not exist\n",msgid);
      mysql_free_result(res);
      return -1;
    }

  /* insert new message */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO message (mailboxidnr, messagesize, status, "
	   "deleted_flag, seen_flag, answered_flag, draft_flag, flagged_flag, recent_flag) "
	   "VALUES (%lu, %lu, %d, %d, %d, %d, %d, %d, %d)", destmboxid,
	   strtoul(row[MESSAGE_MESSAGESIZE], NULL, 10), atoi(row[MESSAGE_STATUS]),
	   atoi(row[MESSAGE_DELETED_FLAG]), atoi(row[MESSAGE_SEEN_FLAG]), 
	   atoi(row[MESSAGE_ANSWERED_FLAG]), atoi(row[MESSAGE_DRAFT_FLAG]), 
	   atoi(row[MESSAGE_FLAGGED_FLAG]), atoi(row[MESSAGE_RECENT_FLAG]));

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not insert new message\n");
      mysql_free_result(res);
      return -1;
    }

  mysql_free_result(res);

  /* retrieve new msg id */
  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message ORDER BY messageidnr DESC "
	   "LIMIT 0,1");

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not retrieve new message ID\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_copymsg(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);

  if (!row)
    {
      /* huh? just inserted a message */
      trace(TRACE_ERROR,"db_copymsg(): no records found where expected\n");
      mysql_free_result(res);
      return -1;
    }

  newid = strtoul(row[0], NULL, 10);

  mysql_free_result(res);

  /* now fetch associated msgblocks */
  snprintf(query, DEF_QUERYSIZE, "SELECT * FROM messageblk WHERE messageidnr = %lu",msgid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not retrieve associated messageblocks\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_copymsg(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  while ((row = mysql_fetch_row(res)))
    {
      lengths = mysql_fetch_lengths(res);
      allocsize = DEF_QUERYSIZE + lengths[MESSAGEBLK_MESSAGEBLK];
      
      insert = (char*)malloc(allocsize);
      if (!insert)
	{
	  /* out of mem */
	  mysql_free_result(res);
	  return -1;
	}
      
      snprintf(insert, DEF_QUERYSIZE, "INSERT INTO messageblk (messageblk, blocksize, messageidnr) "
	       "VALUES ('");

      len = strlen(insert);

      /* now add messageblk data */
      memcpy(&insert[len], row[MESSAGEBLK_MESSAGEBLK], lengths[MESSAGEBLK_MESSAGEBLK]);

      /* add rest of query */
      snprintf(&insert[len+lengths[MESSAGEBLK_MESSAGEBLK]], 
	       allocsize-len-lengths[MESSAGEBLK_MESSAGEBLK], "', %lu, %lu)",
	       strtoul(row[MESSAGEBLK_BLOCKSIZE], NULL, 10), newid);

      len += strlen(&insert[len + lengths[MESSAGEBLK_MESSAGEBLK]]) ;

      if (mysql_real_query(&conn, query, len))
	{
	  trace(TRACE_ERROR, "db_copymsg(): could not insert new messageblocks\n");
	  return -1;
	}

      /* free mem */
      free(insert);
      insert = NULL;
    }

  mysql_free_result(res);

  return 0; /* success */
}


/*
 * db_getmailboxname()
 *
 * retrieves the name of a specified mailbox
 * *name should be large enough to contain the name (IMAP_MAX_MAILBOX_NAMELEN)
 * returns -1 on error, 0 on success
 */
int db_getmailboxname(unsigned long uid, char *name)
{
  char query[DEF_QUERYSIZE];

  snprintf(query, DEF_QUERYSIZE, "SELECT name FROM mailbox WHERE mailboxidnr = %lu",uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_getmailboxname(): could not retrieve name\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getmailboxname(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);

  if (!row)
    {
      /* empty set, mailbox does not exist */
      mysql_free_result(res);
      *name = '\0';
      return 0;
    }

  strncpy(name, row[0], IMAP_MAX_MAILBOX_NAMELEN);
  mysql_free_result(res);
  return 0;
}
  

/*
 * db_setmailboxname()
 *
 * sets the name of a specified mailbox
 * returns -1 on error, 0 on success
 */
int db_setmailboxname(unsigned long uid, const char *name)
{
  char query[DEF_QUERYSIZE];

  snprintf(query, DEF_QUERYSIZE, "UPDATE mailbox SET name = '%s' WHERE mailboxidnr = %lu",
	   name, uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_setmailboxname(): could not set name\n");
      return -1;
    }

  return 0;
}


/*
 * db_build_msn_list()
 *
 * builds a MSN (message sequence number) list containing msg UID's
 *
 * returns 
 *  -1 error
 *   0 success
 *   1 warning: mailbox data is not up-to-date, list not generated
 */
int db_build_msn_list(mailbox_t *mb)
{
  char query[DEF_QUERYSIZE];
  unsigned long cnt,i;

  /* free existing list */
  if (mb->seq_list)
    free(mb->seq_list);

  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message WHERE mailboxidnr = %lu "
	   "AND status != 3 ORDER BY messageidnr ASC", mb->uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_build_msn_list(): could not retrieve messages\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_build_msn_list(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  cnt = mysql_num_rows(res);
  if (cnt != mb->exists)
    {
      /* mailbox data type is not uptodate */
      mb->seq_list = NULL;
      return 1;
    }

  /* alloc mem */
  mb->seq_list = (unsigned long*)malloc(sizeof(unsigned long) * cnt);
  if (!mb->seq_list)
    {
      /* out of mem */
      mysql_free_result(res);
      return -1;
    }

  i=0;
  while ((row = mysql_fetch_row(res)))
    {
      mb->seq_list[i++] = strtoul(row[0],NULL,10);
    }
  

  mysql_free_result(res);
  return 0;
}


/*
 * db_first_unseen()
 *
 * return the message UID of the first unseen msg or -1 on error
 */
unsigned long db_first_unseen(unsigned long uid)
{
  char query[DEF_QUERYSIZE];
  unsigned long id;

  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message WHERE mailboxidnr = %lu "
	   "AND status != 3 AND seen_flag = 0 ORDER BY messageidnr ASC LIMIT 0,1", uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_first_unseen(): could not select messages\n");
      return (unsigned long)(-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_first_unseen(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (unsigned long)(-1);
    }
  
  row = mysql_fetch_row(res);
  if (row)
    id = strtoul(row[0],NULL,10);
  else
    id = 0; /* none found */
      
  mysql_free_result(res);
  return id;
}
  
  
/*
 * db_get_msgflag()
 *
 * gets a flag value specified by 'name' (i.e. 'seen' would check the Seen flag)
 *
 * returns:
 *  -1  error
 *   0  flag not set
 *   1  flag set
 */
int db_get_msgflag(const char *name, unsigned long mailboxuid, unsigned long msguid)
{
  char query[DEF_QUERYSIZE];
  char flagname[DEF_QUERYSIZE/2]; /* should be sufficient ;) */
  int val;

  /* determine flag */
  if (strcasecmp(name,"seen") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "seen_flag");
  else if (strcasecmp(name,"deleted") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "deleted_flag");
  else if (strcasecmp(name,"answered") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "answered_flag");
  else if (strcasecmp(name,"flagged") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "flagged_flag");
  else if (strcasecmp(name,"recent") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "recent_flag");
  else if (strcasecmp(name,"draft") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "draft_flag");
  else
    return 0; /* non-existent flag is not set */

  snprintf(query, DEF_QUERYSIZE, "SELECT %s FROM message WHERE mailboxidnr = %lu "
	   "AND status != 3 AND messageidnr = %lu", flagname, mailboxuid, msguid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_get_msgflag(): could not select message\n");
      return (-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_get_msgflag(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (-1);
    }
  
  row = mysql_fetch_row(res);
  if (row)
    val = atoi(row[0]);
  else
    val = 0; /* none found */
      
  mysql_free_result(res);
  return val;
}


/*
 * db_set_msgflag()
 *
 * sets a flag specified by 'name' to on/off
 *
 * returns:
 *  -1  error
 *   0  success
 */
int db_set_msgflag(const char *name, unsigned long mailboxuid, unsigned long msguid, int val)
{
  char query[DEF_QUERYSIZE];
  char flagname[DEF_QUERYSIZE/2]; /* should be sufficient ;) */

  /* determine flag */
  if (strcasecmp(name,"seen") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "seen_flag");
  else if (strcasecmp(name,"deleted") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "deleted_flag");
  else if (strcasecmp(name,"answered") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "answered_flag");
  else if (strcasecmp(name,"flagged") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "flagged_flag");
  else if (strcasecmp(name,"recent") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "recent_flag");
  else if (strcasecmp(name,"draft") == 0)
    snprintf(flagname, DEF_QUERYSIZE/2, "draft_flag");
  else
    return 0; /* non-existent flag is cannot set */

  snprintf(query, DEF_QUERYSIZE, "UPDATE message SET %s = %d WHERE mailboxidnr = %lu "
	   "AND status != 3 AND messageidnr = %lu", flagname, val, mailboxuid, msguid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_set_msgflag(): could not set flag\n");
      return (-1);
    }

  return 0;
}


/*
 * db_get_msgdate()
 *
 * retrieves msg internal date; 'date' should be large enough (IMAP_INTERNALDATE_LEN)
 * returns -1 on error, 0 on success
 */
int db_get_msgdate(unsigned long mailboxuid, unsigned long msguid, char *date)
{
  char query[DEF_QUERYSIZE];

  snprintf(query, DEF_QUERYSIZE, "SELECT internaldate FROM message WHERE mailboxidnr = %lu "
	   "AND messageidnr = %lu", mailboxuid, msguid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_get_msgdate(): could not get message\n");
      return (-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_get_msgdate(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (-1);
    }

  row = mysql_fetch_row(res);
  if (row)
    {
      strncpy(date, row[0], IMAP_INTERNALDATE_LEN);
      date[IMAP_INTERNALDATE_LEN - 1] = '\0';
    }
  else
    {
      /* ??? no date */
      date[0] = '\0';
    }

  mysql_free_result(res);
  return 0;
}


/*
 * db_init_msgfetch()
 *  
 * initializes a msg fetch
 * returns -1 on error, 1 on success, 0 if already inited (call db_close_msgfetch() first)
 */
int db_init_msgfetch(unsigned long uid)
{
  char query[DEF_QUERYSIZE];
  
  if (_msg_fetch_inited)
    return 0;

  snprintf(query, DEF_QUERYSIZE, "SELECT messageblk FROM messageblk WHERE "
	   "messageidnr = %lu", uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_init_msgfetch(): could not get message\n");
      return (-1);
    }

  if ((_msg_result = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_init_msgfetch(): mysql_store_result failed: %s\n",
	    mysql_error(&conn));
      return (-1);
    }

  _msg_fetch_inited = 1;
  msgidx = 0;

  /* save rows */
  _msgrow = mysql_fetch_row(_msg_result);
  if (!_msgrow)
    {
      _msg_fetch_inited = 0;
      return -1;                     /* msg should have 1 block at least */
    }

  rowlength = (mysql_fetch_lengths(_msg_result))[0];
  strncpy(msgbuf, _msgrow[0], MSGBUF_WINDOWSIZE-1);
  zeropos.block = 0;
  zeropos.pos = 0;

  if (rowlength >= MSGBUF_WINDOWSIZE-1)
    {
      buflen = MSGBUF_WINDOWSIZE-1;
      rowpos = MSGBUF_WINDOWSIZE;            /* remember store pos */
      msgbuf[buflen] = '\0';                 /* terminate buff */
      return 1;                              /* msgbuf full */
    }

  buflen = rowlength;   /* NOTE \0 has been copied from _msgrow) */
  rowpos = rowlength;   /* no more to read from this row */
  _msgrow = mysql_fetch_row(_msg_result);
  if (!_msgrow)
    {
      rowlength = rowpos = 0;
      return 1;
    }

  rowlength = (mysql_fetch_lengths(_msg_result))[0];
  rowpos = 0;
  strncpy(&msgbuf[buflen], _msgrow[0], MSGBUF_WINDOWSIZE - buflen - 1);

  if (rowlength <= MSGBUF_WINDOWSIZE - buflen - 1)
    {
      /* 2nd block fits entirely */
      rowpos = rowlength;
      buflen += rowlength;
    }
  else
    {
      rowpos = MSGBUF_WINDOWSIZE - (buflen+1);
      buflen = MSGBUF_WINDOWSIZE-1;
    }

  msgbuf[buflen] = '\0';           /* add NULL */
  return 1;
}


/*
 * db_update_msgbuf()
 *
 * update msgbuf:
 * if minlen < 0, update is forced else update only if there are less than 
 * minlen chars left in buf
 *
 * returns 1 on succes, -1 on error, 0 if no more chars in rows
 */
int db_update_msgbuf(int minlen)
{
  trace(TRACE_DEBUG,"update msgbuf start %lu %lu %lu %lu\n",MSGBUF_WINDOWSIZE,
	buflen,rowlength,rowpos);

  if (!_msgrow)
    return 0; /* no more */

  if (msgidx > buflen)
    return -1;             /* error, msgidx should be within buf */

  if (minlen > 0 && (buflen-msgidx) > minlen)
    return 1;                                 /* ok, need no update */
      

  trace(TRACE_DEBUG,"update msgbuf updating\n");

  /* move buf to make msgidx 0 */
  memmove(msgbuf, &msgbuf[msgidx], msgidx);
  if (msgidx > ((buflen+1) - rowpos))
    {
      zeropos.block++;
      zeropos.pos = (msgidx - ((buflen+1) - rowpos));
    }
  else
    zeropos.pos += msgidx;

  buflen -= msgidx;
  msgidx = 0;

  if ((rowlength-rowpos) >= (MSGBUF_WINDOWSIZE - buflen))
    {
      trace(TRACE_DEBUG,"update msgbuf 1\n");

      /* rest of row does not fit entirely in buf */
      strncpy(&msgbuf[buflen], &_msgrow[0][rowpos], MSGBUF_WINDOWSIZE - buflen);
      rowpos += (MSGBUF_WINDOWSIZE - buflen);

      buflen = MSGBUF_WINDOWSIZE-1;
      msgbuf[buflen] = '\0';

      return 1;
    }

  trace(TRACE_DEBUG,"update msgbuf 2 %s\n",_msgrow[0]);

  strncpy(&msgbuf[buflen], &_msgrow[0][rowpos], (rowlength-rowpos));
  buflen += (rowlength-rowpos);
  msgbuf[buflen] = '\0';
  rowpos = rowlength;
  
  /* try to fetch a new row */
  _msgrow = mysql_fetch_row(_msg_result);
  if (!_msgrow)
    {
      rowlength = rowpos = 0;

      trace(TRACE_DEBUG,"update msgbuf succes NOMORE\n");
      return 0;
    }

  rowlength = (mysql_fetch_lengths(_msg_result))[0];
  rowpos = 0;

  trace(TRACE_DEBUG,"update msgbuf 3\n");

  strncpy(&msgbuf[buflen], _msgrow[0], MSGBUF_WINDOWSIZE - buflen - 1);
  if (rowlength <= MSGBUF_WINDOWSIZE - buflen - 1)
    {
      /* 2nd block fits entirely */
      rowpos = rowlength;
      buflen += rowlength;
    }
  else
    {
      rowpos = MSGBUF_WINDOWSIZE - (buflen+1);
      buflen = MSGBUF_WINDOWSIZE-1;
    }

  msgbuf[buflen] = '\0' ;          /* add NULL */

  trace(TRACE_DEBUG,"update msgbuf succes\n");
  return 1;
}


/*
 * db_close_msgfetch()
 *
 * finishes a msg fetch
 */
void db_close_msgfetch()
{
  if (!_msg_fetch_inited)
    return; /* nothing to be done */

  mysql_free_result(_msg_result);
  _msg_fetch_inited = 0;
}



/*
 * db_fetch_headers()
 *
 * builds up an array containing message headers and the start/end position of the 
 * associated body part(s)
 *
 * creates a linked-list of headers found
 *
 * returns:
 * -1 error
 *  0 success
 */
int db_fetch_headers(unsigned long msguid, mime_message_t *msg)
{
  int result;

  if (db_init_msgfetch(msguid) != 1)
    {
      trace(TRACE_ERROR,"db_fetch_headers(): could not init msgfetch\n");
      return -1;
    }

  result = db_start_msg(msg, NULL); /* fetch message */
  if (result == -1)
    {
      db_free_msg(msg);
      return -1;
    }

  db_reverse_msg(msg);

  db_close_msgfetch();
  return 0;
}

      
/* 
 * frees all the memory associated with a msg
 */
void db_free_msg(mime_message_t *msg)
{
  struct element *tmp;

  if (!msg)
    return;

  /* free the children msg's */
  tmp = list_getstart(&msg->children);

  while (tmp)
    {
      db_free_msg((mime_message_t*)tmp->data);
      tmp = tmp->nextnode;
    }

  tmp = list_getstart(&msg->children);
  list_freelist(&tmp);
  
  tmp = list_getstart(&msg->mimeheader);
  list_freelist(&tmp);

}

      
/* 
 * reverses the children lists of a msg
 */
void db_reverse_msg(mime_message_t *msg)
{
  struct element *tmp;

  if (!msg)
    return;

  /* reverse the children msg's */
  tmp = list_getstart(&msg->children);

  while (tmp)
    {
      db_reverse_msg((mime_message_t*)tmp->data);
      tmp = tmp->nextnode;
    }

  /* reverse this list */
  tmp = list_getstart(&msg->children);
  list_reverse(tmp);
}


void db_give_msgpos(db_pos_t *pos)
{
  if (msgidx > ((buflen+1)-rowpos))
    {
      pos->block = zeropos.block+1;
      pos->pos   = ((buflen+1)-rowpos) - msgidx;
    }
  else
    {
      pos->block = zeropos.block;
      pos->pos = zeropos.pos + msgidx;
    }
}


/*
 * db_start_msg()
 *
 * reads in a msg
 */
int db_start_msg(mime_message_t *msg, char *stopbound)
{
  int len,sblen,result;
  struct mime_record *mr;
  char *newbound,*bptr;

  list_init(&msg->children);
  db_give_msgpos(&msg->headerstart);

  /* read header */
  if (db_update_msgbuf(MSGBUF_FORCE_UPDATE) == -1)
    return -1;

  mime_readheader(&msgbuf[msgidx], &msgidx, &msg->mimeheader);
  db_give_msgpos(&msg->headerend);

  msgidx++;
  db_give_msgpos(&msg->bodystart);

  mime_findfield("content-type", &msg->mimeheader, mr);
  if (mr && strncasecmp(mr->value,"multipart", strlen("multipart")) == 0)
    {
      trace(TRACE_DEBUG,"db_start_msg(): found multipart msg\n");

      /* multipart msg, find new boundary */
      for (bptr = mr->value; *bptr; bptr++) 
	if ((*bptr == 'b' || *bptr == 'B') && 
	    strncasecmp(bptr, "boundary=", strlen("boundary=")) == 0)
	    break;

      if (!bptr)
	return -1; /* no new boundary ??? */

      bptr += strlen("boundary=");
      if (*bptr == '\"')      
	bptr++;

      newbound = bptr;
      while (*newbound && *newbound != '\"' && !isspace(*newbound)) newbound++;

      len = newbound - bptr;
      if (!(newbound = (char*)malloc(len+1)))
	return -1;

      strncpy(newbound, bptr, len);
      newbound[len] = '\0';

      /* advance to first boundary */
      if (db_update_msgbuf(MSGBUF_FORCE_UPDATE) == -1)
	{
	  free(newbound);
	  return -1;
	}

      while (msgbuf[msgidx])
	{
	  if (msgbuf[msgidx] == '\n' && strncmp(&msgbuf[msgidx+1], newbound, strlen(newbound)) == 0)
	    break;
	}

      if (msgbuf[msgidx]) msgidx++; /* skip  newline */

      if (!msgbuf[msgidx])
	{
	  free(newbound);
	  return -1;
	}
      

      /* find MIME-parts */
      if (db_add_mime_children(&msg->children, newbound) == -1)
	{
	  free(newbound);
	  return -1;
	}

      free(newbound);
      db_give_msgpos(&msg->bodyend);
      return 0;                        /* done */
    }
  else
    {
      /* single part msg, read untill stopbound OR end of buffer */
      trace(TRACE_DEBUG,"db_start_msg(): found singlepart msg\n");

      if (stopbound)
	{
	  sblen = strlen(stopbound)+2;

	  while (msgbuf[msgidx])
	    {
	      if (db_update_msgbuf(sblen) == -1)
		return -1;

	      if (msgbuf[msgidx] == '\n' && 
		  strncmp(&msgbuf[msgidx+1], stopbound, strlen(stopbound)) == 0)
		{
		  db_give_msgpos(&msg->bodyend);
		  return 0;
		}
	    }

	  /* end of buffer reached, bodyend is prev pos */
	  msgidx--;
	  db_give_msgpos(&msg->bodyend);
	  msgidx++;
	}
      else
	{
	  /* walk on till end of buffer */
	  do
	    {
	      msgidx = buflen - 1;
	      result = db_update_msgbuf(MSGBUF_FORCE_UPDATE);
	      if (result == -1)
		return -1;
	    } while (result == 1);

	  db_give_msgpos(&msg->bodyend);
	}
    }

  trace(TRACE_DEBUG,"db_start_msg(): exit\n");

  return 0;
}



/*
 * assume to enter just after a splitbound 
 */
int db_add_mime_children(struct list *brothers, char *splitbound)
{
  mime_message_t part;
  struct mime_record *mr;
  char *boundary;
  int sblen;

  trace(TRACE_DEBUG,"boundary: '%s'\n",boundary);

  do
    {
      db_update_msgbuf(MSGBUF_FORCE_UPDATE);
      memset(&part, 0, sizeof(part));

      /* should have a MIME header right here */
      mime_readheader(&msgbuf[msgidx], &msgidx, &part.mimeheader);
      mime_findfield("content-type", &part.mimeheader, mr);

      if (mr && strncasecmp(mr->value, "message/rfc822", strlen("message/rfc822")) == 0)
	{
	  /* a message will follow */

	  db_start_msg(&part, splitbound);

	  /* advance to after splitbound */
	  msgidx++;
	  msgidx += strlen(splitbound);

	}
      else
	{
	  /* just body data follows, advance to splitbound */
	  db_give_msgpos(&part.bodystart);
	  sblen = strlen(splitbound)+2;

	  while (msgbuf[msgidx])
	    {
	      if (db_update_msgbuf(sblen) == -1)
		return -1;

	      if (msgbuf[msgidx] == '\n' && 
		  strncmp(&msgbuf[msgidx+1], splitbound, strlen(splitbound)) == 0)
		break;
	    }

	  if (!msgbuf[msgidx])
	    {
	      /* ?? splitbound should follow */
	      return -1;
	    }
	  db_give_msgpos(&part.bodyend);
	}

      /* add this part to brother list */
      if (list_nodeadd(brothers, &part, sizeof(part)) == NULL)
	return -1;

      /* if double newline follows we're done */
      if (msgbuf[msgidx] && msgbuf[msgidx] == '\n' && msgbuf[msgidx+1] == '\n')
	{
	  msgidx += 2; /* skip \n */
	  return 0;
	}
    }
  while (msgbuf[msgidx]) ;

  return 0;
}


/*
 * db_msgdump()
 *
 * dumps a message to stderr
 */
int db_msgdump(mime_message_t *msg)
{
  struct element *curr;
  struct mime_record *mr;

  if (!msg)
    {
      trace(TRACE_DEBUG,"db_msgdump: got null\n");
      return 0;
    }

  trace(TRACE_DEBUG,"MIME-header: \n");
  curr = list_getstart(&msg->mimeheader);
  if (!curr)
    trace(TRACE_DEBUG,"null\n");
  else
    {
      while (curr)
	{
	  mr = (struct mime_record *)curr->data;
	  trace(TRACE_DEBUG,"[%s] : [%s]\n",mr->field, mr->value);
	  curr = curr->nextnode;
	}
    }
     
  trace(TRACE_DEBUG,"RFC822-header: \n");
  db_dump_range(msg->headerstart, msg->headerend);
  trace(TRACE_DEBUG,"*** header end\n");

  trace(TRACE_DEBUG,"body: \n");
  db_dump_range(msg->bodystart, msg->bodyend);
  trace(TRACE_DEBUG,"*** body end\n");

  trace(TRACE_DEBUG,"Children of this msg:\n");
  
  curr = list_getstart(&msg->children);
  while (curr)
    {
      db_msgdump((mime_message_t*)curr->data);
      curr = curr->nextnode;
    }

  return 1;
}


void db_dump_range(db_pos_t start, db_pos_t end)
{
  
  trace(TRACE_DEBUG,"Range: (%d,%d) - (%d,%d)\n",start.block, start.pos, end.block, end.pos);
}
