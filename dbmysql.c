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
	
	/* selecting the right database */
	if (mysql_select_db(&conn,MAILDATABASE)) {
		trace(TRACE_ERROR,"dbconnect(): mysql_select_db failed: %s",mysql_error(&conn));
		return -1;
	}  
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

int db_send_header (void *fstream, unsigned long messageidnr)
	{
	/* this function writes the header to stream */
	char *ckquery;

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
      trace(TRACE_ERROR,"db_send_header: mysql_store_result failed: %s",mysql_error(&conn));
      free(ckquery);
      return 0;
      }
   if (mysql_num_rows(res)<1)
      {
      trace (TRACE_ERROR,"db_send_header(): no messageblks for messageid %li",messageidnr);
		mysql_free_result(res);
      free(ckquery);
		return 0;
		}

		while ((row = mysql_fetch_row(res))!=NULL)
				fprintf ((FILE *)fstream,row[2]);
		mysql_free_result(res);
		return 0;
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
   sprintf (ckquery, "SELECT useridnr FROM mailbox WHERE userid=\"%s\" AND passwd=\"%s\"",
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
					trace(TRACE_ERROR,"db_update_pop(): could not execute query: [%s]");
					free(ckquery);
					return -1;
					}
				}
			tmpelement=tmpelement->nextnode;
		}
	return 0;
	}

int db_disconnect (char *query)
{
	return 0;
}
