/* $iD: Dbmysql.c,v 1.63 2001/09/23 12:36:41 eelco Exp $
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * mysql driver file
 * Functions for connecting and talking to the Mysql database */

#include "dbmysql.h"
#include "config.h"
#include "pop3.h"
#include "dbmd5.h"
#include "list.h"
#include "mime.h"
#include "pipe.h"
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <regex.h>

#define DEF_QUERYSIZE 1024
#define MSGBUF_WINDOWSIZE (128ul*1024ul)
#define MSGBUF_FORCE_UPDATE -1

#define MAX_EMAIL_SIZE 250

const char *month_desc[]= 
{ 
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};


/* used only locally */
int db_binary_search(const unsigned long *array, int arraysize, unsigned long key);
int db_exec_search(mime_message_t *msg, search_key_t *sk, unsigned long msguid);
int db_search_range(db_pos_t start, db_pos_t end, const char *key, unsigned long msguid);
int num_from_imapdate(const char *date);

MYSQL conn;  
MYSQL_RES *res,*_msg_result;
MYSQL_RES *checkres;
MYSQL_ROW row;
MYSQL_ROW _msgrow;
char query[DEF_QUERYSIZE];
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
unsigned long rowlength = 0,msgidx=0,buflen=0,rowpos=0;
db_pos_t zeropos;
unsigned nblocks = 0;
unsigned long *blklengths = NULL;



/*
 * queries to create/drop temporary tables
 */
const char *create_tmp_tables_queries[] = 
{ "CREATE TABLE tmpmessage ("
  "messageidnr bigint(21) DEFAULT '0' NOT NULL auto_increment,"
  "mailboxidnr int(21) DEFAULT '0' NOT NULL,"
  "messagesize bigint(21) DEFAULT '0' NOT NULL,"
  "seen_flag tinyint(1) default '0' not null,"
  "answered_flag tinyint(1) default '0' not null,"
  "deleted_flag tinyint(1) default '0' not null,"
  "flagged_flag tinyint(1) default '0' not null,"
  "recent_flag tinyint(1) default '0' not null,"
  "draft_flag tinyint(1) default '0' not null,"
  "unique_id varchar(70) NOT NULL,"
  "internal_date datetime default '0' not null,"
  "status tinyint(3) unsigned zerofill default '000' not null,"
  "PRIMARY KEY (messageidnr),"
  "KEY messageidnr (messageidnr),"
  "UNIQUE messageidnr_2 (messageidnr))" ,

  "CREATE TABLE tmpmessageblk ("
  "messageblknr bigint(21) DEFAULT '0' NOT NULL auto_increment,"
  "messageidnr bigint(21) DEFAULT '0' NOT NULL,"
  "messageblk longtext NOT NULL,"
  "blocksize bigint(21) DEFAULT '0' NOT NULL,"
  "PRIMARY KEY (messageblknr),"
  "KEY messageblknr (messageblknr),"
  "KEY msg_index (messageidnr),"
  "UNIQUE messageblknr_2 (messageblknr)"
  ") TYPE=MyISAM "
};

const char *drop_tmp_tables_queries[] = { "DROP TABLE tmpmessage", "DROP TABLE tmpmessageblk" };

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


int db_query (const char *thequery)
{
  unsigned int querysize = 0;

  if (thequery != NULL)
    {
      querysize = strlen(thequery);

      if (querysize > 0 )
	{
	  if (mysql_real_query(&conn, thequery, querysize) <0) 
	    {
	      trace(TRACE_ERROR,"db_query(): mysql_real_query failed: %s\n",mysql_error(&conn)); 
	      return -1;
	    }
	}
      else
	{
	  trace (TRACE_ERROR,"db_query(): querysize is wrong: [%d]\n",querysize);
	  return -1;
	}
    }
  else
    {
      trace (TRACE_ERROR,"db_query(): query buffer is NULL, this is not supposed to happen\n",
	     querysize);
      return -1;
    }
  return 0;
}


/*
 * clears the configuration table
 */
int db_clear_config()
{
  return db_query("DELETE FROM config");
}


int db_insert_config_item (char *item, char *value)
{
  /* insert_config_item will insert a configuration item in the database */

  /* allocating memory for query */
  snprintf (query, DEF_QUERYSIZE,"INSERT INTO config (item,value) VALUES ('%s', '%s')",item, value);
  trace (TRACE_DEBUG,"insert_config_item(): executing query: [%s]",query);

  if (db_query(query)==-1)
    {
      trace (TRACE_DEBUG,"insert_config_item(): item [%s] value [%s] failed",item,value);
      return -1;
    }
  else 
    {
      return 0;
    }
}


char *db_get_config_item (char *item, int type)
{
  /* retrieves an config item from database */
  char *result = NULL;
  
	
  snprintf (query, DEF_QUERYSIZE, "SELECT value FROM config WHERE item = '%s'",item);
  trace (TRACE_DEBUG,"db_get_config_item(): retrieving config_item %s by query %s\n",
	 item, query);

  if (db_query(query)==-1)
    {
      if (type == CONFIG_MANDATORY)
	trace (TRACE_FATAL,"db_get_config_item(): query failed could not get value for %s. "
	       "This is needed to continue\n",item);
      else
	if (type == CONFIG_EMPTY)
	  trace (TRACE_ERROR,"db_get_config_item(): query failed. Could not get value for %s\n",
		 item);

      return NULL;
    }
  
  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      if (type == CONFIG_MANDATORY)
	trace(TRACE_FATAL,"db_get_config_item(): mysql_store_result failed: %s\n",
	      mysql_error(&conn));
      else
	if (type == CONFIG_EMPTY)
	  trace (TRACE_ERROR,"db_get_config_item(): mysql_store_result failed (fatal): %s\n",
		 mysql_error(&conn));

      return 0;
    }

  if ((row = mysql_fetch_row(res))==NULL)
    {
      if (type == CONFIG_MANDATORY)
	trace (TRACE_FATAL,"db_get_config_item(): configvalue not found for %s. rowfetch failure. "
	       "This is needed to continue\n",item);
      else
	if (type == CONFIG_EMPTY)
	  trace (TRACE_ERROR,"db_get_config_item(): configvalue not found. rowfetch failure.  "
		 "Could not get value for %s\n",item);

      mysql_free_result(res);
      return NULL;
    }
	
  if (row[0]!=NULL)
    {
      result=(char *)my_malloc(strlen(row[0])+1);
      if (result!=NULL)
	strcpy (result,row[0]);
      trace (TRACE_DEBUG,"Ok result [%s]\n",result);
    }
	
  mysql_free_result(res);
  return result;
}

	
unsigned long db_adduser (char *username, char *password, char *clientid, char *maxmail)
{
  /* adds a new user to the database 
   * and adds a INBOX 
   * returns a useridnr on succes, -1 on failure */

  unsigned long useridnr;
	

  snprintf (query, DEF_QUERYSIZE,"INSERT INTO user (userid,passwd,clientid,maxmail_size) VALUES "
	   "('%s','%s',%s,%s)",
	   username,password,clientid, maxmail);
	
  trace (TRACE_DEBUG,"db_adduser(): executing query for user: [%s]", query);

  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_adduser(): query for adding user failed : [%s]", query);
      
      return -1;
    }

  useridnr = db_insert_result ();
	
  /* creating query for adding mailbox */
  snprintf (query, DEF_QUERYSIZE,"INSERT INTO mailbox (owneridnr, name) VALUES (%lu,'INBOX')",
	   useridnr);
	
  trace (TRACE_DEBUG,"db_adduser(): executing query for mailbox: [%s]", query);

	
  if (db_query(query))
    {
      trace (TRACE_ERROR,"db_adduser(): query failed for adding mailbox: [%s]",query);
      return -1;
    }

  
  return useridnr;
}

int db_addalias (unsigned long useridnr, char *alias, int clientid)
{
  /* adds an alias for a specific user */
  

  snprintf (query, DEF_QUERYSIZE,
	    "INSERT INTO aliases (alias,deliver_to,client_id) VALUES ('%s','%lu',%d)",
	   alias, useridnr, clientid);
	
  trace (TRACE_DEBUG,"db_addalias(): executing query for user: [%s]", query);

  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_addalias(): query for adding alias failed : [%s]", query);
      return -1;
    }
  
  return 0;
}

unsigned long db_get_inboxid (unsigned long *useridnr)
{
  /* returns the mailbox id (of mailbox inbox) for a user or a 0 if no mailboxes were found */
  unsigned long inboxid;

  snprintf (query, DEF_QUERYSIZE,"SELECT mailboxidnr FROM mailbox WHERE name='INBOX' AND owneridnr=%lu",
	   *useridnr);

  trace(TRACE_DEBUG,"db_get_inboxid(): executing query : [%s]",query);
  if (db_query(query)==-1)
    {
      return 0;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_mailboxid(): mysql_store_result failed: %s",mysql_error(&conn));
      return 0;
    }

  if (mysql_num_rows(res)<1) 
    {
      trace (TRACE_DEBUG,"db_get_mailboxid(): user has no INBOX");
      mysql_free_result(res);
      
      return 0; 
    } 

  if ((row = mysql_fetch_row(res))==NULL)
    {
      trace (TRACE_DEBUG,"db_get_mailboxid(): fetch_row call failed");
    }

  inboxid = (row && row[0]) ? atol(row[0]) : 0; 

  mysql_free_result (res);
  
	
  return inboxid;
}

char *db_get_userid (unsigned long *useridnr)
{
  /* returns the mailbox id (of mailbox inbox) for a user or a 0 if no mailboxes were found */
  
  char *returnid = NULL;
  
  snprintf (query, DEF_QUERYSIZE,"SELECT userid FROM user WHERE useridnr = %lu",
	   *useridnr);

  trace(TRACE_DEBUG,"db_get_userid(): executing query : [%s]",query);
  if (db_query(query)==-1)
    {
      return 0;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_userid(): mysql_store_result failed: %s",mysql_error(&conn));
      
      return 0;
    }

  if (mysql_num_rows(res)<1) 
    {
      trace (TRACE_DEBUG,"db_get_userid(): user has no username?");
      mysql_free_result(res);
      
      return 0; 
    } 

  if ((row = mysql_fetch_row(res))==NULL)
    {
      trace (TRACE_DEBUG,"db_get_userid(): fetch_row call failed");
      mysql_free_result(res);
      return NULL;
    }

  if (row[0])
    {
      if (!(returnid = (char *)my_malloc(strlen(row[0])+1)))
	{
	  trace(TRACE_ERROR,"db_get_userid(): out of memory");
	  mysql_free_result(res);
	  return NULL;
	}
	  
      strcpy (returnid, row[0]);
    }
  
  mysql_free_result(res);
  
  return returnid;
}

unsigned long db_get_message_mailboxid (unsigned long *messageidnr)
{
  /* returns the mailbox id of a message */
  unsigned long mailboxid;
  
  
  snprintf (query, DEF_QUERYSIZE,"SELECT mailboxidnr FROM message WHERE messageidnr = %lu",
	   *messageidnr);

  trace(TRACE_DEBUG,"db_get_message_mailboxid(): executing query : [%s]",query);
  if (db_query(query)==-1)
    {
      return 0;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_message_mailboxid(): mysql_store_result failed: %s",
	    mysql_error(&conn));
      
      return 0;
    }

  if (mysql_num_rows(res)<1) 
    {
      trace (TRACE_DEBUG,"db_get_message_mailboxid(): this message had no mailboxid? "
	     "Message without a mailbox!");
      mysql_free_result(res);
      
      return 0; 
    } 

  if ((row = mysql_fetch_row(res))==NULL)
    {
      trace (TRACE_DEBUG,"db_get_mailboxid(): fetch_row call failed");
    }

  mailboxid = (row && row[0]) ? atol(row[0]) : 0;
	
  mysql_free_result (res);
  
	
  return mailboxid;
}


unsigned long db_get_useridnr (unsigned long messageidnr)
{
  /* returns the userid from a messageidnr */
  unsigned long mailboxidnr;
  unsigned long userid;
  
  snprintf (query, DEF_QUERYSIZE,"SELECT mailboxidnr FROM message WHERE messageidnr = %lu",
	   messageidnr);

  trace(TRACE_DEBUG,"db_get_useridnr(): executing query : [%s]",query);
  if (db_query(query)==-1)
    {
      return 0;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_useridnr(): mysql_store_result failed: %s",mysql_error(&conn));
      
      return 0;
    }

  if (mysql_num_rows(res)<1) 
    {
      trace (TRACE_DEBUG,"db_get_useridnr(): this is not right!");
      mysql_free_result(res);
      
      return 0; 
    } 

  if ((row = mysql_fetch_row(res))==NULL)
    {
      trace (TRACE_DEBUG,"db_get_useridnr(): fetch_row call failed");
    }

  mailboxidnr = (row && row[0]) ? atol(row[0]) : -1;
  mysql_free_result(res);
	
  if (mailboxidnr == -1)
    {
      
      return 0;
    }

  snprintf (query, DEF_QUERYSIZE, "SELECT owneridnr FROM mailbox WHERE mailboxidnr = %lu",
	   mailboxidnr);

  if (db_query(query)==-1)
    {
      return 0;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_useridnr(): mysql_store_result failed: %s",mysql_error(&conn));
      return 0;
    }

  if (mysql_num_rows(res)<1) 
    {
      trace (TRACE_DEBUG,"db_get_useridnr(): this is not right!");
      mysql_free_result(res);
      
      return 0; 
    } 

  if ((row = mysql_fetch_row(res))==NULL)
    {
      trace (TRACE_DEBUG,"db_get_useridnr(): fetch_row call failed");
    }
	
  userid = (row && row[0]) ? atol(row[0]) : 0;
	
  mysql_free_result (res);
  
	
  return userid;
}


/* 
 * inserts into inbox ! 
 */
unsigned long db_insert_message (unsigned long *useridnr)
{
  char timestr[30];
  time_t td;
  struct tm tm;

  time(&td);              /* get time */
  tm = *localtime(&td);   /* get components */
  strftime(timestr, sizeof(timestr), "%G-%m-%d %H:%M:%S", &tm);
  
  snprintf (query, DEF_QUERYSIZE,"INSERT INTO message(mailboxidnr,messagesize,unique_id,internal_date)"
	   " VALUES (%lu,0,\" \",\"%s\")",
	   db_get_inboxid(useridnr), timestr);

  trace (TRACE_DEBUG,"db_insert_message(): inserting message query [%s]",query);
  if (db_query (query)==-1)
    {
      trace(TRACE_STOP,"db_insert_message(): dbquery failed");
    }	
  
  return db_insert_result();
}


unsigned long db_update_message (unsigned long *messageidnr, char *unique_id,
		unsigned long messagesize)
{
  snprintf (query, DEF_QUERYSIZE,
	   "UPDATE message SET messagesize=%lu, unique_id=\"%s\" where messageidnr=%lu",
	   messagesize, unique_id, *messageidnr);
  
  trace (TRACE_DEBUG,"db_update_message(): updating message query [%s]",query);
  if (db_query (query)==-1)
    {
      
      trace(TRACE_STOP,"db_update_message(): dbquery failed");
    }
	
  
  return 0;
}


/*
 * insert a msg block
 * returns msgblkid on succes, -1 on failure
 */
unsigned long db_insert_message_block (char *block, int messageidnr)
{
  char *escblk=NULL, *tmpquery=NULL;
  int len,esclen=0;

  if (block != NULL)
    {
      len = strlen(block);

      trace (TRACE_DEBUG,"db_insert_message_block(): inserting a %d bytes block\n",
	     len);

      /* allocate memory twice as much, for eacht character might be escaped 
	 added aditional 250 bytes for possible function err */

      memtst((escblk=(char *)my_malloc(((len*2)+250)))==NULL); 

      /* escape the string */
      if ((esclen = mysql_escape_string(escblk, block, len)) > 0)
	{
	  /* add an extra 500 characters for the query */
	  memtst((tmpquery=(char *)my_malloc(esclen + 500))==NULL);
	
	  snprintf (tmpquery, esclen+500,
		   "INSERT INTO messageblk(messageblk,blocksize,messageidnr) "
		   "VALUES (\"%s\",%d,%d)",
		   escblk,len,messageidnr);

	  if (db_query (tmpquery)==-1)
	    {
	      my_free(escblk);
	      my_free(tmpquery);
	      trace(TRACE_ERROR,"db_insert_message_block(): dbquery failed\n");
	      return -1;
	    }

	  /* freeing buffers */
	  my_free(tmpquery);
	  my_free(escblk);
	  return db_insert_result();
	}
      else
	{
	  trace (TRACE_ERROR,"db_insert_message_block(): mysql_real_escape_string() "
		 "returned empty value\n");

	  my_free(escblk);
	  return -1;
	}
    }
  else
    {
      trace (TRACE_ERROR,"db_insert_message_block(): value of block cannot be NULL, "
	     "insertion not possible\n");
      return -1;
    }

  return -1;
}


int db_check_user (char *username, struct list *userids) 
{
  
  int occurences=0;
	
  trace(TRACE_DEBUG,"db_check_user(): checking user [%s] in alias table",username);
  
  snprintf (query, DEF_QUERYSIZE,  "SELECT * FROM aliases WHERE alias=\"%s\"",username);
  trace(TRACE_DEBUG,"db_check_user(): executing query : [%s]",query);
  if (db_query(query)==-1)
    {
      
      return occurences;
    }

  
  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_check_user: mysql_store_result failed: %s",mysql_error(&conn));
      return occurences;
    }

  if (mysql_num_rows(res)<1) 
    {
      trace (TRACE_DEBUG,"db_check_user(): user %s not in aliases table", username);
      mysql_free_result(res);
      return occurences; 
    } 
	
  /* row[2] is the deliver_to field */
  while ((row = mysql_fetch_row(res))!=NULL)
    {
      occurences++;

      list_nodeadd(userids, row[2], strlen(row[2])+1);
      trace (TRACE_DEBUG,"db_check_user(): adding [%s] to deliver_to address",row[2]);
    }

  trace(TRACE_INFO,"db_check_user(): user [%s] has [%d] entries",username,occurences);
  mysql_free_result(res);
  return occurences;
}


/* 
   this function writes "lines" to fstream.
   if lines == -2 then the whole message is dumped to fstream 
   newlines are rewritten to crlf 
   This is excluding the header 
*/
int db_send_message_lines (void *fstream, unsigned long messageidnr, long lines, int no_end_dot)
{
  char *buffer = NULL;
  char *nextpos, *tmppos = NULL;
  int block_count;
  unsigned long *lengths;
  
  trace (TRACE_DEBUG,"db_send_message_lines(): request for [%d] lines",lines);

  
  memtst ((buffer=(char *)my_malloc(READ_BLOCK_SIZE*2))==NULL);

  snprintf (query, DEF_QUERYSIZE, 
	    "SELECT * FROM messageblk WHERE messageidnr=%lu ORDER BY messageblknr ASC",
	   messageidnr);
  trace (TRACE_DEBUG,"db_send_message_lines(): executing query [%s]",query);

  if (db_query(query)==-1)
    {
      my_free(buffer);
      return 0;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_send_message_lines: mysql_store_result failed: %s",mysql_error(&conn));
      my_free(buffer);
      return 0;
    }
  
  trace (TRACE_DEBUG,"db_send_message_lines(): sending [%d] lines from message [%lu]",
	 lines,messageidnr);
  
  block_count=0;

  while (((row = mysql_fetch_row(res))!=NULL) && ((lines>0) || (lines==-2) || (block_count==0)))
  {
      nextpos=row[2];
      lengths = mysql_fetch_lengths(res);
      rowlength = lengths[2];
		
      /* reset our buffer */
      memset (buffer, '\0', (READ_BLOCK_SIZE)*2);
		
      while ((*nextpos!='\0') && (rowlength>0) && ((lines>0) || (lines==-2) || (block_count==0)))
      {
          if (*nextpos=='\n')
          {
              /* first block is always the full header 
                 so this should not be counted when parsing
                 if lines == -2 none of the lines should be counted 
                 since the whole message is requested */
              if ((lines!=-2) && (block_count!=0))
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
          rowlength--;
	   
          if (rowlength%3000==0)  /* purge buffer at every 3000 bytes  */
          {
              /* fprintf ((FILE *)fstream,"%s",buffer); */
              /* fflush ((FILE *)fstream); */

              fwrite (buffer, sizeof(char), strlen(buffer), (FILE *)fstream);
          
              /*  cleanup the buffer  */
              memset (buffer, '\0', (READ_BLOCK_SIZE*2));
          }
      }

      /* next block in while loop */
      block_count++;
      trace (TRACE_DEBUG,"db_send_message_lines(): getting nextblock [%d]\n",block_count);
		
      /* flush our buffer */
      /* fprintf ((FILE *)fstream,"%s",buffer); */
      fwrite (buffer, sizeof(char), strlen(buffer), (FILE *)fstream);
      /* fflush ((FILE *)fstream); */
  }

  /* delimiter */
  if (no_end_dot == 0)
      fprintf ((FILE *)fstream,"\r\n.\r\n");
	
  mysql_free_result(res);
	
  my_free(buffer);
  return 1;
}

unsigned long db_validate (char *user, char *password)
{
  /* returns useridnr on OK, 0 on validation failed, -1 on error */
  
  unsigned long id;

  
  snprintf (query, DEF_QUERYSIZE, "SELECT useridnr FROM user WHERE userid=\"%s\" AND passwd=\"%s\"",
	   user,password);

  trace (TRACE_DEBUG,"db_validate(): validating using query %s\n",query);
	
  if (db_query(query)==-1)
    {
      
      return -1;
    }
	
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_validate(): mysql_store_result failed: %s\n",mysql_error(&conn));
      
      return -1;
    }

  row = mysql_fetch_row(res);

  id = (row && row[0]) ? strtoul(row[0], NULL, 10) : 0;
  
  
  mysql_free_result(res);
  return id;
}

unsigned long db_md5_validate (char *username,unsigned char *md5_apop_he, char *apop_stamp)
{
  /* returns useridnr on OK, 0 on validation failed, -1 on error */
  
  char *checkstring;
  unsigned char *md5_apop_we;
  unsigned long useridnr;	

  
  
  snprintf (query, DEF_QUERYSIZE, "SELECT passwd,useridnr FROM user WHERE userid=\"%s\"",username);
	
  if (db_query(query)==-1)
    {
      
      return -1;
    }
	
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_md5_validate(): mysql_store_result failed:  %s",mysql_error(&conn));
      
      return -1;
    }

  if (mysql_num_rows(res)<1)
    {
      /* no such user found */
      
      return 0;
    }
	
  row = mysql_fetch_row(res);
	
	/* now authenticate using MD5 hash comparisation 
	 * row[0] contains the password */

  trace (TRACE_DEBUG,"db_md5_validate(): apop_stamp=[%s], userpw=[%s]",apop_stamp,row[0]);
	
  memtst((checkstring=(char *)my_malloc(strlen(apop_stamp)+strlen(row[0])+2))==NULL);
  snprintf(checkstring, strlen(apop_stamp)+strlen(row[0])+2, "%s%s",apop_stamp,row[0]);

  md5_apop_we=makemd5(checkstring);
	
  trace(TRACE_DEBUG,"db_md5_validate(): checkstring for md5 [%s] -> result [%s]",checkstring,
	md5_apop_we);
  trace(TRACE_DEBUG,"db_md5_validate(): validating md5_apop_we=[%s] md5_apop_he=[%s]",
	md5_apop_we, md5_apop_he);

  if (strcmp(md5_apop_he,makemd5(checkstring))==0)
    {
      trace(TRACE_MESSAGE,"db_md5_validate(): user [%s] is validated using APOP",username);
		
      useridnr = (row && row[1]) ? atol(row[1]) : 0;
	
      mysql_free_result(res);
      
      my_free(checkstring);

      return useridnr;
    }
	
  trace(TRACE_MESSAGE,"db_md5_validate(): user [%s] could not be validated",username);

  if (res!=NULL)
    mysql_free_result(res);
  
  
  my_free(checkstring);
  
  return 0;
}

void db_session_cleanup (struct session *sessionptr)
{
  /* cleanups a session 
     removes a list and all references */

  sessionptr->totalsize=0;
  sessionptr->virtual_totalsize=0;
  sessionptr->totalmessages=0;
  sessionptr->virtual_totalmessages=0;
  list_freelist(&(sessionptr->messagelst.start));
}


/* returns 1 with a successfull session, -1 when something goes wrong 
 * sessionptr is changed with the right session info
 * useridnr is the userid index for the user whose mailbox we're viewing 
 */
int db_createsession (unsigned long useridnr, struct session *sessionptr)
{
  /* first we do a query on the messages of this user */

  
  struct message tmpmessage;
  unsigned long messagecounter=0;
	
  
  /* query is <2 because we don't want deleted messages 
   * the unique_id should not be empty, this could mean that the message is still being delivered */
  snprintf (query, DEF_QUERYSIZE, "SELECT * FROM message WHERE mailboxidnr=%lu AND status<002 AND "
	   "unique_id!=\"\" order by status ASC",
	   (db_get_inboxid(&useridnr)));

  if (db_query(query)==-1)
    {
      
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_validate(): mysql_store_result failed:  %s",mysql_error(&conn));
      
      return -1;
    }
		
  sessionptr->totalmessages=0;
  sessionptr->totalsize=0;

  
  if ((messagecounter=mysql_num_rows(res))<1)
    {
      /* there are no messages for this user */
      
      mysql_free_result(res);
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
  
  struct element *tmpelement;
		
  

  /* get first element in list */
  tmpelement=list_getstart(&sessionptr->messagelst);
	

  while (tmpelement!=NULL)
    {
      /* check if they need an update in the database */
      if (((struct message *)tmpelement->data)->virtual_messagestatus!=
	  ((struct message *)tmpelement->data)->messagestatus) 
	{
	  /* yes they need an update, do the query */
	  snprintf (query,DEF_QUERYSIZE,
		    "UPDATE message set status=%lu WHERE messageidnr=%lu AND status<002",
		   ((struct message *)tmpelement->data)->virtual_messagestatus,
		   ((struct message *)tmpelement->data)->realmessageid);
	
	  /* FIXME: a message could be deleted already if it has been accessed
	   * by another interface and be deleted by sysop
	   * we need a check if the query failes because it doesn't exists anymore
	   * now it will just bailout */
	
	  if (db_query(query)==-1)
	    {
	      trace(TRACE_ERROR,"db_update_pop(): could not execute query: []");
	      
	      return -1;
	    }
	}
      tmpelement=tmpelement->nextnode;
    }
  
  return 0;
}

unsigned long db_check_mailboxsize (unsigned long mailboxid)
{
  /* checks the size of a mailbox */
  
  unsigned long size;

  /* checking current size */
  
  snprintf (query, DEF_QUERYSIZE,
	    "SELECT SUM(messagesize) FROM message WHERE mailboxidnr = %lu AND status<002",
	   mailboxid);

  trace (TRACE_DEBUG,"db_check_mailboxsize(): executing query [%s]\n",
	 query);

  if (db_query(query) != 0)
    {
      trace (TRACE_ERROR,"db_check_mailboxsize(): could not execute query [%s]\n",
	     query);
      
      return -1;
    }
  
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_check_mailboxsize(): mysql_store_result failed: %s\n",
	     mysql_error(&conn));
      
      return -1;
    }

  if (mysql_num_rows(res)<1)
    {
      trace (TRACE_ERROR,"db_check_mailboxsize(): weird, cannot execute SUM query\n");
      
      return 0;
    }

  row = mysql_fetch_row(res);

  size = (row && row[0]) ? strtoul(row[0], NULL, 10) : 0;
  mysql_free_result(res);
  

  return size;
}


unsigned long db_check_sizelimit (unsigned long addblocksize, unsigned long messageidnr, 
				  unsigned long *useridnr)
{
  /* returns -1 when a block cannot be inserted due to dbase failure
   *          1 when a block cannot be inserted due to quotum exceed
   *         -2 when a block cannot be inserted due to quotum exceed and a dbase failure occured
   * also does a complete rollback when this occurs 
   * returns 0 when situation is ok 
   */

  
  unsigned long mailboxidnr;
  unsigned long currmail_size = 0, maxmail_size = 0;

  *useridnr = db_get_useridnr (messageidnr);
	
  /* looking up messageidnr */
  mailboxidnr = db_get_message_mailboxid (&messageidnr);
	
  /* checking current size */
  
  snprintf (query, DEF_QUERYSIZE,"SELECT mailboxidnr FROM mailbox WHERE owneridnr = %lu",
	   *useridnr);


  if (db_query(query) != 0)
    {
      trace (TRACE_ERROR,"db_check_sizelimit(): could not execute query [%s]\n",
	     query);
      
      return -1;
    }
  
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_check_sizelimit(): mysql_store_result failed: %s\n",
	     mysql_error(&conn));
      
      return -1;
    }


  if (mysql_num_rows(res)<1)
    {
      trace (TRACE_ERROR,"db_check_sizelimit(): user has NO mailboxes\n");
      
      mysql_free_result(res);
      return 0;
    }

  while ((row = mysql_fetch_row(res))!=NULL)
    {
      trace (TRACE_DEBUG,"db_check_sizelimit(): checking mailbox [%s]\n",row[0]);
      currmail_size += db_check_mailboxsize(atol(row[0]));
    }

  /* current mailsize from INBOX is now known, now check the maxsize for this user */
  snprintf (query, DEF_QUERYSIZE,"SELECT maxmail_size FROM user WHERE useridnr = %lu", *useridnr);
  trace (TRACE_DEBUG,"db_check_sizelimit(): executing query: %s\n", query);

  if (db_query(query) != 0)
    {
      trace (TRACE_ERROR,"db_check_sizelimit(): could not execute query [%s]\n",
	     query);
      
      mysql_free_result(res);
      return -1;
    }
  
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_check_sizelimit(): mysql_store_result failed: %s\n",
	     mysql_error(&conn));
      
      mysql_free_result(res);
      return -1;
    }

  if (mysql_num_rows(res)<1)
    {
      trace (TRACE_ERROR,"db_check_sizelimit(): weird, this user does not seem to exist!\n");
      
      mysql_free_result(res);
      return 0;
    }
	
  row = mysql_fetch_row(res);
  maxmail_size = row[0] ? atol(row[0]) : 0;

  trace (TRACE_DEBUG, "db_check_sizelimit(): comparing currsize + blocksize  [%d], maxsize [%d]\n",
	 currmail_size, maxmail_size);
	

  /* currmail already represents the current size of messages from this user */
	
  if (((currmail_size) > maxmail_size) && (maxmail_size != 0))
    {
      trace (TRACE_INFO,"db_check_sizelimit(): mailboxsize of useridnr %lu exceed with %lu bytes\n", 
	     useridnr, (currmail_size)-maxmail_size);

      /* user is exceeding, we're going to execute a rollback now */
      snprintf (query,DEF_QUERYSIZE,"DELETE FROM messageblk WHERE messageidnr = %lu", 
	       messageidnr);
      if (db_query(query) != 0)
	{
	  trace (TRACE_ERROR,"db_check_sizelimit(): rollback of mailbox add failed\n");
	  
	  mysql_free_result(res);
	  return -2;
	}

      snprintf (query,DEF_QUERYSIZE,"DELETE FROM message WHERE messageidnr = %lu",
	       messageidnr);

      if (db_query(query) != 0)
	{
	  trace (TRACE_ERROR,"db_check_sizelimit(): rollblock of mailbox add failed."
		 " DB might be inconsistent."
		 " run dbmail-maintenance\n");
	  
	  mysql_free_result(res);
	  return -2;
	}

      mysql_free_result(res);
      
      return 1;
    }

  mysql_free_result(res);
  
  return 0;
}


/* purges all the messages with a deleted status */
unsigned long db_deleted_purge()
{
  
  unsigned long affected_rows=0;

  
	
  /* first we're deleting all the messageblks */
  snprintf (query,DEF_QUERYSIZE,"SELECT messageidnr FROM message WHERE status=003");
  trace (TRACE_DEBUG,"db_deleted_purge(): executing query [%s]",query);
	
  if (db_query(query)==-1)
    {
      trace(TRACE_ERROR,"db_deleted_purge(): Cound not execute query [%s]",query);
      
      return -1;
    }
  
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_deleted_purge(): mysql_store_result failed:  %s",mysql_error(&conn));
      
      return -1;
    }
  
  if (mysql_num_rows(res)<1)
    {
      
      mysql_free_result(res);
      return 0;
    }
	
  while ((row = mysql_fetch_row(res))!=NULL)
    {
      snprintf (query,DEF_QUERYSIZE,"DELETE FROM messageblk WHERE messageidnr=%s",row[0]);
      trace (TRACE_DEBUG,"db_deleted_purge(): executing query [%s]",query);
      if (db_query(query)==-1)
	{
	  
	  mysql_free_result(res);
	  return -1;
	}
    }

  /* messageblks are deleted. Now delete the messages */
  snprintf (query,DEF_QUERYSIZE,"DELETE FROM message WHERE status=003");
  trace (TRACE_DEBUG,"db_deleted_purge(): executing query [%s]",query);
  if (db_query(query)==-1)
    {
      
      mysql_free_result(res);
      return -1;
    }
	
  affected_rows = mysql_affected_rows(&conn);
  mysql_free_result(res);
  

  return affected_rows;
}


/* sets al messages with status 002 to status 003 for final
 * deletion 
 */
unsigned long db_set_deleted ()
{
  /* first we're deleting all the messageblks */
  snprintf (query,DEF_QUERYSIZE,"UPDATE message SET status=003 WHERE status=002");
  trace (TRACE_DEBUG,"db_set_deleted(): executing query [%s]",query);

  if (db_query(query)==-1)
    {
      trace(TRACE_ERROR,"db_set_deleted(): Could not execute query [%s]",query);
      return -1;
    }
 
  
  return mysql_affected_rows(&conn);
}

int db_icheck_messageblks(struct list *lost_messageblks)
{
    /* 
     * will check for messageblks that are not
     * connected to messages 
     */


    snprintf (query,DEF_QUERYSIZE,"SELECT messageblknr, messageidnr FROM messageblk");
    trace (TRACE_DEBUG,"db_icheck_messageblks(): executing query [%s]",query);

    if (db_query(query)==-1)
    {
        trace (TRACE_ERROR,"db_icheck_messageblks(): Could not execute query [%s]",query);
        return -1;
    }
  
    if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace (TRACE_ERROR,"db_icheck_messageblks(): mysql_store_result failed:  %s",mysql_error(&conn));
      return -1;
    }
  
  if (mysql_num_rows(res)<1)
    {
      
      mysql_free_result(res);
      return 0;
    }
	
  while ((row = mysql_fetch_row(res))!=NULL)
    {
      snprintf (query,DEF_QUERYSIZE,"SELECT messageidnr FROM message "
              "WHERE messageidnr=%s",row[1]);
    
      trace (TRACE_DEBUG,"db_icheck_messageblks(): executing query [%s]",query);
      
      if (db_query(query)==-1)
      {
	  mysql_free_result(res);
	  return -1;
      }

      if ((checkres = mysql_store_result(&conn)) == NULL)
      {
          trace (TRACE_ERROR,"db_icheck_messageblks(): mysql_store_result failed: %s",
                  mysql_error(&conn));
          mysql_free_result(res);
          return -1;
      }

      if (mysql_num_rows(checkres)<1)
      {
          /* this one is not connected to a message
           * add to the list 
           */

          list_nodeadd (lost_messageblks, row[1], strlen(row[1])+1);
      }
      /*
       * else do nothing, just check the next one */
    }
  return 0;
}


int db_disconnect()
{
  mysql_close(&conn);
  return 0;
}


/*
 * db_imap_append_msg()
 *
 * inserts a message
 *
 * returns: 
 * -1 serious dbase/memory error
 *  0 ok
 *  1 invalid msg
 *  2 mail quotum exceeded
 *
 */
int db_imap_append_msg(char *msgdata, unsigned long datalen, unsigned long mboxid, unsigned long uid)
{
  char timestr[30];
  time_t td;
  struct tm tm;
  unsigned long msgid,cnt;
  int result;
  char savechar;
  char unique_id[UID_SIZE]; /* unique id */
  
  time(&td);              /* get time */
  tm = *localtime(&td);   /* get components */
  strftime(timestr, sizeof(timestr), "%G-%m-%d %H:%M:%S", &tm);

  /* create a msg 
   * status and seen_flag are set to 001, which means the message has been read 
   */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO message "
	   "(mailboxidnr,messagesize,unique_id,internal_date,status,"
       " seen_flag) VALUES (%lu, 0, \"\", \"%s\",001,1)",
	   mboxid, timestr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_imap_append_msg(): could not create message\n");
      my_free(query);
      return -1;
    }

  /* fetch the id of the new message */
  msgid = mysql_insert_id(&conn);
  
  result = db_check_sizelimit(datalen, msgid, &uid);
  if (result == -1 || result == -2)
    {     
      trace(TRACE_ERROR, "db_imap_append_msg(): dbase error checking size limit\n");
      
      return -1;
    }

  if (result)
    {     
      trace(TRACE_INFO, "db_imap_append_msg(): user %lu would exceed quotum\n",uid);
      
      return 2;
    }


  /* ok insert blocks */
  /* first the header: scan until double newline */
  for (cnt=1; cnt<datalen; cnt++)
    if (msgdata[cnt-1] == '\n' && msgdata[cnt] == '\n')
      break;

  if (cnt == datalen)
    {
      trace(TRACE_INFO, "db_imap_append_msg(): no double newline found [invalid msg]\n");
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %lu", msgid);
      if (db_query(query) == -1)
	trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%lu], "
	      "dbase invalid now..\n", msgid);

      
      return 1;
    }

  if (cnt == datalen-1)
    {
      /* msg consists of a single header */
      trace(TRACE_INFO, "db_imap_append_msg(): msg only contains a header\n");

      if (db_insert_message_block(msgdata, msgid) == -1 || 
	  db_insert_message_block(" \n", msgid)   == -1)
	{
	  trace(TRACE_ERROR, "db_imap_append_msg(): could not insert msg block\n");

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %lu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%lu], "
		  "dbase could be invalid now..\n", msgid);

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblk WHERE messageidnr = %lu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete messageblks for msg id [%lu], "
		  "dbase could be invalid now..\n", msgid);

	  
	  return -1;
	}

    }
  else
    {
      /* output header */
      cnt++;
      savechar = msgdata[cnt];                        /* remember char */
      msgdata[cnt] = 0;                               /* terminate string */
      if (db_insert_message_block(msgdata, msgid) == -1)
	{
	  trace(TRACE_ERROR, "db_imap_append_msg(): could not insert msg block\n");

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %lu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%lu], "
		  "dbase invalid now..\n", msgid);

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblk WHERE messageidnr = %lu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete messageblks for msg id [%lu], "
		  "dbase could be invalid now..\n", msgid);

	  
	  return -1;
	}

      msgdata[cnt] = savechar;                        /* restore */

      /* output message */
      while ((datalen - cnt) > READ_BLOCK_SIZE)
	{
	  savechar = msgdata[cnt + READ_BLOCK_SIZE];        /* remember char */
	  msgdata[cnt + READ_BLOCK_SIZE] = 0;               /* terminate string */

	  if (db_insert_message_block(&msgdata[cnt], msgid) == -1)
	    {
	      trace(TRACE_ERROR, "db_imap_append_msg(): could not insert msg block\n");

	      snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %lu", msgid);
	      if (db_query(query) == -1)
		trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%lu], "
		      "dbase invalid now..\n", msgid);

	      snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblk WHERE messageidnr = %lu", msgid);
	      if (db_query(query) == -1)
		trace(TRACE_ERROR, "db_imap_append_msg(): could not delete messageblks "
		      "for msg id [%lu], dbase could be invalid now..\n", msgid);

	      
	      return -1;
	    }

	  msgdata[cnt + READ_BLOCK_SIZE] = savechar;        /* restore */

	  cnt += READ_BLOCK_SIZE;
	}


      if (db_insert_message_block(&msgdata[cnt], msgid) == -1)
	{
	  trace(TRACE_ERROR, "db_imap_append_msg(): could not insert msg block\n");

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %lu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete message id [%lu], "
		  "dbase invalid now..\n", msgid);

	  snprintf(query, DEF_QUERYSIZE, "DELETE FROM messageblk WHERE messageidnr = %lu", msgid);
	  if (db_query(query) == -1)
	    trace(TRACE_ERROR, "db_imap_append_msg(): could not delete messageblks "
		  "for msg id [%lu], dbase could be invalid now..\n", msgid);

	  
	  return -1;
	}

    }  
  
  /* create a unique id */
  snprintf (unique_id,UID_SIZE,"%luA%lu",msgid,td);
  /* set info on message */
  db_update_message (&msgid, unique_id, datalen);
  
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
 * db_findmailbox_by_regex()
 *
 * finds all the mailboxes owned by ownerid who match the regex pattern pattern.
 */
int db_findmailbox_by_regex(unsigned long ownerid, const char *pattern, 
			    unsigned long **children, unsigned *nchildren, int only_subscribed)
{
  
  int result;
  unsigned long *tmp = NULL;
  regex_t preg;
  *children = NULL;

  if ((result = regcomp(&preg, pattern, REG_ICASE|REG_NOSUB)) != 0)
    {
      trace(TRACE_ERROR, "db_findmailbox_by_regex(): error compiling regex pattern: %d\n",
	    result);
      
      return 1;
    }

  if (only_subscribed)
    snprintf(query, DEF_QUERYSIZE, "SELECT name, mailboxidnr FROM mailbox WHERE "
	     "owneridnr=%lu AND is_subscribed != 0", ownerid);
  else
    snprintf(query, DEF_QUERYSIZE, "SELECT name, mailboxidnr FROM mailbox WHERE "
	     "owneridnr=%lu", ownerid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_findmailbox_by_regex(): error during mailbox query\r\n");
      
      return (-1);
    }

  
  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_findmailbox_by_regex(): mysql_store_result failed:  %s\n",
	    mysql_error(&conn));
      return (-1);
    }
  
  /* alloc mem */
  tmp = (unsigned long *)my_malloc(sizeof(unsigned long) * mysql_num_rows(res));
  if (!tmp)
    {
      trace(TRACE_ERROR,"db_findmailbox_by_regex(): not enough memory\n");
      return (-1);
    }
     
  /* store matches */
  *nchildren = 0;
  while ((row = mysql_fetch_row(res)))
    {
      if (regexec(&preg, row[0], 0, NULL, 0) == 0)
	tmp[(*nchildren)++] = strtoul(row[1], NULL, 10);
    }

  mysql_free_result(res);

  if (*nchildren == 0)
    {
      my_free(tmp);
      return 0;
    }

  /* realloc mem */
  *children = (unsigned long *)realloc(tmp, sizeof(unsigned long) * *nchildren);
  if (!(*children))
    {
      trace(TRACE_ERROR,"db_findmailbox_by_regex(): realloc failed\n");
      my_free(tmp);
      return -1;
    }

  return 0;
}


/*
 * db_getmailbox()
 * 
 * gets mailbox info from dbase, builds the message sequence number list
 *
 * returns 
 *  -1  error
 *   0  success
 */
int db_getmailbox(mailbox_t *mb, unsigned long userid)
{
  unsigned long i;

  /* free existing MSN list */
  if (mb->seq_list)
    {
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

  if (row[1]) mb->flags |= IMAPFLAG_SEEN;
  if (row[2]) mb->flags |= IMAPFLAG_ANSWERED;
  if (row[3]) mb->flags |= IMAPFLAG_DELETED;
  if (row[4]) mb->flags |= IMAPFLAG_FLAGGED;
  if (row[5]) mb->flags |= IMAPFLAG_RECENT;
  if (row[6]) mb->flags |= IMAPFLAG_DRAFT;

  mysql_free_result(res);

  /* select messages */
  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr, seen_flag, recent_flag "
	   "FROM message WHERE mailboxidnr = %lu "
	   "AND status<2 AND unique_id!=\"\" ORDER BY messageidnr ASC", mb->uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_getmailbox(): could not retrieve messages\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getmailbox(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return -1;
    }

  mb->exists = mysql_num_rows(res);

  /* alloc mem */
  mb->seq_list = (unsigned long*)my_malloc(sizeof(unsigned long) * mb->exists);
  if (!mb->seq_list)
    {
      /* out of mem */
      mysql_free_result(res);
      return -1;
    }

  i=0;
  while ((row = mysql_fetch_row(res)))
    {
      if (row[1][0] == '0') mb->unseen++;
      if (row[2][0] == '1') mb->recent++;

      mb->seq_list[i++] = strtoul(row[0],NULL,10);
    }
  
  mysql_free_result(res);

  
  /* now determine the next message UID */
  /*
   * NOTE expunged messages are selected as well in order to be able to restore them 
   */

  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message WHERE unique_id!=\"\""
	   "ORDER BY messageidnr DESC LIMIT 0,1");
  
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_getmailbox(): could not determine highest message ID\n");

      my_free(mb->seq_list);
      mb->seq_list = NULL;

      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getmailbox(): mysql_store_result failed: %s\n",mysql_error(&conn));
      
      my_free(mb->seq_list);
      mb->seq_list = NULL;

      return -1;
    }

  row = mysql_fetch_row(res);
  if (row)
    mb->msguidnext = atoi(row[0])+1;
  else
    mb->msguidnext = 1; /* empty set: no messages yet in dbase */

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
int db_listmailboxchildren(unsigned long uid, unsigned long useridnr, 
			   unsigned long **children, int *nchildren, 
			   const char *filter)
{
  
  int i;

  /* retrieve the name of this mailbox */
  snprintf(query, DEF_QUERYSIZE, "SELECT name FROM mailbox WHERE"
	   " mailboxidnr = %lu AND owneridnr = %lu", uid, useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_listmailboxchildren(): could not retrieve mailbox name\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_listmailboxchildren(): mysql_store_result failed: %s\n",
	    mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);
  if (row)
    snprintf(query, DEF_QUERYSIZE, "SELECT mailboxidnr FROM mailbox WHERE name LIKE '%s/%s'"
	     " AND owneridnr = %lu",
	     row[0],filter,useridnr);
  else
    snprintf(query, DEF_QUERYSIZE, "SELECT mailboxidnr FROM mailbox WHERE name LIKE '%s'"
	     " AND owneridnr = %lu",filter,useridnr);

  mysql_free_result(res);
  
  /* now find the children */
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_listmailboxchildren(): could not retrieve mailbox name\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_listmailboxchildren(): mysql_store_result failed: %s\n",
	    mysql_error(&conn));
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
  *children = (unsigned long*)my_malloc(sizeof(unsigned long) * (*nchildren));

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
	  my_free(*children);
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
  if (db_removemsg(uid) == -1) /* remove all msg */
    {
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


/*
 * db_isselectable()
 *
 * returns 1 if the specified mailbox is selectable, 0 if not and -1 on failure
 */  
int db_isselectable(unsigned long uid)
{
  

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
  
  int i;

  /* first select msg UIDs */
  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message WHERE"
	   " mailboxidnr = %lu AND deleted_flag=1 AND status<2 ORDER BY messageidnr DESC", uid);

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
  *msgids = (unsigned long *)my_malloc(sizeof(unsigned long) * (*nmsgs));
  if (!(*msgids))
    {
      /* out of mem */
      *nmsgs = 0;
      mysql_free_result(res);
      return -1;
    }

  /* save ID's in array */
  i = 0;
  while ((row = mysql_fetch_row(res)) && i<*nmsgs)
    {
      (*msgids)[i++] = strtoul(row[0], NULL, 10);
    }
  mysql_free_result(res);
  
  /* update messages belonging to this mailbox: mark as expunged (status 3) */
  snprintf(query, DEF_QUERYSIZE, "UPDATE message SET status=3 WHERE"
	   " mailboxidnr = %lu AND deleted_flag=1 AND status<2", uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_expunge(): could not update messages in mailbox\n");
      my_free(*msgids);
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
  unsigned long newid,tmpid;
  time_t td;

  time(&td);              /* get time */

  /* create temporary tables */
  if (db_query(create_tmp_tables_queries[0]) == -1 || db_query(create_tmp_tables_queries[1]) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not create temporary tables\n");
      return -1;
    }

  /* copy: */

  /* first to temporary table */
  /* copy message info */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO tmpmessage (mailboxidnr, messagesize, status, "
	   "deleted_flag, seen_flag, answered_flag, draft_flag, flagged_flag, recent_flag,"
       " unique_id, internal_date) "
	   "SELECT mailboxidnr, messagesize, status, deleted_flag, seen_flag, answered_flag, "
	   "draft_flag, flagged_flag, recent_flag, \"\", internal_date "
       "FROM message WHERE message.messageidnr = %lu",
	   msgid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not insert temporary message\n");

      /* drop temporary tables */
      if (db_query(drop_tmp_tables_queries[0]) == -1 || db_query(drop_tmp_tables_queries[1]) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not drop temporary tables\n");

      return -1;
    }

  tmpid = mysql_insert_id(&conn);

  /* copy message blocks */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO tmpmessageblk (messageidnr, messageblk, blocksize) "
	   "SELECT %lu, messageblk, blocksize FROM messageblk "
	   "WHERE messageblk.messageidnr = %lu ORDER BY messageblk.messageblknr", tmpid, msgid);
  
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not insert temporary message blocks\n");

      /* drop temporary tables */
      if (db_query(drop_tmp_tables_queries[0]) == -1 || db_query(drop_tmp_tables_queries[1]) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not drop temporary tables\n");

      return -1;
    }


  /* now to actual tables */
  /* copy message info */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO message (mailboxidnr, messagesize, status, "
	   "deleted_flag, seen_flag, answered_flag, draft_flag, flagged_flag, recent_flag,"
       " unique_id, internal_date) "
	   "SELECT %lu, messagesize, status, deleted_flag, seen_flag, answered_flag, "
	   "draft_flag, flagged_flag, recent_flag, \"\", internal_date "
       "FROM tmpmessage WHERE tmpmessage.messageidnr = %lu",
	   destmboxid, tmpid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not insert message\n");

      /* drop temporary tables */
      if (db_query(drop_tmp_tables_queries[0]) == -1 || db_query(drop_tmp_tables_queries[1]) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not drop temporary tables\n");

      return -1;
    }

  /* retrieve id of new message */
  newid = mysql_insert_id(&conn);

  /* copy message blocks */
  snprintf(query, DEF_QUERYSIZE, "INSERT INTO messageblk (messageidnr, messageblk, blocksize) "
	   "SELECT %lu, messageblk, blocksize FROM tmpmessageblk "
	   "WHERE tmpmessageblk.messageidnr = %lu ORDER BY tmpmessageblk.messageblknr", newid, tmpid);
  
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not insert message blocks\n");

      /* drop temporary tables */
      if (db_query(drop_tmp_tables_queries[0]) == -1 || db_query(drop_tmp_tables_queries[1]) == -1)
	trace(TRACE_ERROR, "db_copymsg(): could not drop temporary tables\n");

      /* delete inserted message */
      snprintf(query, DEF_QUERYSIZE, "DELETE FROM message WHERE messageidnr = %lu",newid);
      if (db_query(query) == -1)
	trace(TRACE_FATAL, "db_copymsg(): could not delete faulty message, dbase contains "
	      "invalid data now; msgid [%lu]\n",newid);
      
      return -1;
    }

  /* drop temporary tables */
  if (db_query(drop_tmp_tables_queries[0]) == -1 || db_query(drop_tmp_tables_queries[1]) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not drop temporary tables\n");
      return -1;
    }

  /* all done, validate new msg by creating a new unique id for the copied msg */
  snprintf(query, DEF_QUERYSIZE, "UPDATE message SET unique_id=\"%luA%lu\" "
	   "WHERE messageidnr=%lu", newid, td, newid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_copymsg(): could not set unique ID for copied msg\n");
      return -1;
    }

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
 * db_first_unseen()
 *
 * return the message UID of the first unseen msg or -1 on error
 */
unsigned long db_first_unseen(unsigned long uid)
{
  
  unsigned long id;

  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message WHERE mailboxidnr = %lu "
	   "AND status<2 AND seen_flag = 0 AND unique_id != \"\" "
	   "ORDER BY messageidnr ASC LIMIT 0,1", uid);

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
 * db_subscribe()
 *
 * subscribes to a certain mailbox
 */
int db_subscribe(unsigned long mboxid)
{
  

  snprintf(query, DEF_QUERYSIZE, "UPDATE mailbox SET is_subscribed = 1 WHERE mailboxidnr = %lu", 
	   mboxid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_subscribe(): could not update mailbox\n");
      return (-1);
    }

  return 0;
}
  

/*
 * db_unsubscribe()
 *
 * unsubscribes to a certain mailbox
 */
int db_unsubscribe(unsigned long mboxid)
{
  

  snprintf(query, DEF_QUERYSIZE, "UPDATE mailbox SET is_subscribed = 0 WHERE mailboxidnr = %lu", 
	   mboxid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_unsubscribe(): could not update mailbox\n");
      return (-1);
    }

  return 0;
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
	   "AND status<2 AND messageidnr = %lu AND unique_id != \"\"", flagname, mailboxuid, msguid);

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
	   "AND status<2 AND messageidnr = %lu", flagname, val, mailboxuid, msguid);

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
  snprintf(query, DEF_QUERYSIZE, "SELECT internal_date FROM message WHERE mailboxidnr = %lu "
	   "AND messageidnr = %lu AND unique_id!=\"\"", mailboxuid, msguid);

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
      /* no date ? let's say 1 jan 1970 */
      strncpy(date, "1970-01-01 00:00:01", IMAP_INTERNALDATE_LEN);
      date[IMAP_INTERNALDATE_LEN - 1] = '\0';
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
  int i;
  
  
  if (_msg_fetch_inited)
    return 0;

  snprintf(query, DEF_QUERYSIZE, "SELECT messageblk FROM messageblk WHERE "
	   "messageidnr = %lu ORDER BY messageblknr", uid);

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

  /* first determine block lengths */
  nblocks = mysql_num_rows(_msg_result);
  if (nblocks == 0)
    {
      trace(TRACE_ERROR, "db_init_msgfetch(): message has no blocks\n");
      mysql_free_result(_msg_result);
      return -1;                     /* msg should have 1 block at least */
    }
  
  if (!(blklengths = (unsigned long*)my_malloc(nblocks * sizeof(unsigned long))))
    {
      trace(TRACE_ERROR, "db_init_msgfetch(): out of memory\n");
      mysql_free_result(_msg_result);
      return (-1);
    }
     
  for (i=0; i<nblocks; i++)
    {
      _msgrow = mysql_fetch_row(_msg_result);
      blklengths[i] = (mysql_fetch_lengths(_msg_result))[0];
    }

  /* re-execute query */
  mysql_free_result(_msg_result);
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_init_msgfetch(): could not get message\n");
      my_free(blklengths);
      blklengths = NULL;
      return (-1);
    }

  if ((_msg_result = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_init_msgfetch(): mysql_store_result failed: %s\n",
	    mysql_error(&conn));
      my_free(blklengths);
      blklengths = NULL;
      return (-1);
    }

  _msg_fetch_inited = 1;
  msgidx = 0;

  /* save rows */
  _msgrow = mysql_fetch_row(_msg_result);

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
  if (!_msgrow)
    return 0; /* no more */

  if (msgidx > buflen)
    return -1;             /* error, msgidx should be within buf */

  if (minlen > 0 && (buflen-msgidx) > minlen)
    return 1;                                 /* ok, need no update */
      
  if (msgidx == 0)
    return 1;             /* update no use, buffer would not change */

  trace(TRACE_DEBUG,"update msgbuf updating %lu %lu %lu %lu\n",MSGBUF_WINDOWSIZE,
	buflen,rowlength,rowpos);

  /* move buf to make msgidx 0 */
  memmove(msgbuf, &msgbuf[msgidx], (buflen-msgidx));
  if (msgidx > ((buflen+1) - rowpos))
    {
      zeropos.block++;
      zeropos.pos = (msgidx - ((buflen) - rowpos));
    }
  else
    zeropos.pos += msgidx;

  buflen -= msgidx;
  msgidx = 0;

  if ((rowlength-rowpos) >= (MSGBUF_WINDOWSIZE - buflen))
    {
      trace(TRACE_DEBUG,"update msgbuf non-entire fit\n");

      /* rest of row does not fit entirely in buf */
      strncpy(&msgbuf[buflen], &_msgrow[0][rowpos], MSGBUF_WINDOWSIZE - buflen);
      rowpos += (MSGBUF_WINDOWSIZE - buflen - 1);

      buflen = MSGBUF_WINDOWSIZE-1;
      msgbuf[buflen] = '\0';

      return 1;
    }

  trace(TRACE_DEBUG,"update msgbuf: entire fit\n");

  strncpy(&msgbuf[buflen], &_msgrow[0][rowpos], (rowlength-rowpos));
  buflen += (rowlength-rowpos);
  msgbuf[buflen] = '\0';
  rowpos = rowlength;
  
  /* try to fetch a new row */
  _msgrow = mysql_fetch_row(_msg_result);
  if (!_msgrow)
    {
      trace(TRACE_DEBUG,"update msgbuf succes NOMORE\n");
      return 0;
    }

  rowlength = (mysql_fetch_lengths(_msg_result))[0];
  rowpos = 0;

  trace(TRACE_DEBUG,"update msgbuf, got new block, trying to place data\n");

  strncpy(&msgbuf[buflen], _msgrow[0], MSGBUF_WINDOWSIZE - buflen - 1);
  if (rowlength <= MSGBUF_WINDOWSIZE - buflen - 1)
    {
      /* 2nd block fits entirely */
      trace(TRACE_DEBUG,"update msgbuf: new block fits entirely\n");

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

  my_free(blklengths);
  blklengths = NULL;
  nblocks = 0;

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
 * NOTE: there are no checks performed to verify that the indicated msg isn't expunged 
 *       (status 002) or has been inserted completely. This should be done before calling
 *       this function (unless, of course, it is your intention to specificly parse an 
 *       incomplete message or an expunged one).
 *
 * returns:
 * -3 memory error
 * -2 dbase error
 * -1 parse error but msg is retrieved as plaintext
 *  0 success
 */
int db_fetch_headers(unsigned long msguid, mime_message_t *msg)
{
  int result,level=0,maxlevel=-1;

  if (db_init_msgfetch(msguid) != 1)
    {
      trace(TRACE_ERROR,"db_fetch_headers(): could not init msgfetch\n");
      return -2;
    }

  result = db_start_msg(msg, NULL, &level, maxlevel); /* fetch message */
  if (result < 0)
    {
      trace(TRACE_INFO, "db_fetch_headers(): error fetching message, ID: %lu\n",msguid);
      trace(TRACE_INFO, "db_fetch_headers(): got error at level %d\n",level);

      db_close_msgfetch();
      db_free_msg(msg);

      if (result < -1)
	return result; /* memory/dbase error */

      /* 
       * so an error occurred parsing the message. 
       * try to lower the maxlevel of recursion
       */

      for (maxlevel = level-1; maxlevel >= 0; maxlevel--)
	{
	  trace(TRACE_DEBUG, "db_fetch_headers(): trying to fetch at maxlevel %d...\n",maxlevel);

	  if (db_init_msgfetch(msguid) != 1)
	    {
	      trace(TRACE_ERROR,"db_fetch_headers(): could not init msgfetch\n");
	      return -2;
	    }

	  level = 0;
	  result = db_start_msg(msg, NULL, &level, maxlevel);

	  db_close_msgfetch();

	  if (result != -1)
	    break;

	  db_free_msg(msg);
	}

      if (result < -1)
	{
	  db_free_msg(msg);
	  return result;
	}

      if (result >= 0)
	{
	  trace(TRACE_ERROR,"db_fetch_headers(): succesfully recovered erroneous message %lu\n",
		msguid);
	  db_reverse_msg(msg);
	  return 0;
	}


      /* ok still problems... try to make a message */
      if (db_init_msgfetch(msguid) != 1)
	{
	  trace(TRACE_ERROR,"db_fetch_headers(): could not init msgfetch\n");
	  return -2;
	}

      result = db_parse_as_text(msg);
      if (result < 0)
	{
	  /* probably some serious dbase error */
	  trace(TRACE_ERROR,"db_fetch_headers(): could not recover message as plain text\n");
	  db_free_msg(msg);
	  return result;
	}

      trace(TRACE_WARNING, "db_fetch_headers(): message recovered as plain text\n");
      db_close_msgfetch();
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

  tmp = list_getstart(&msg->rfcheader);
  list_freelist(&tmp);

  memset(msg, 0, sizeof(*msg));
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
  msg->children.start = list_reverse(msg->children.start);

  /* reverse header items */
  msg->mimeheader.start = list_reverse(msg->mimeheader.start);
  msg->rfcheader.start  = list_reverse(msg->rfcheader.start);
}


void db_give_msgpos(db_pos_t *pos)
{
/*  trace(TRACE_DEBUG, "db_give_msgpos(): msgidx %lu, buflen %lu, rowpos %lu\n",
	msgidx,buflen,rowpos);
  trace(TRACE_DEBUG, "db_give_msgpos(): (buflen)-rowpos %lu\n",(buflen)-rowpos);
  */

  if (msgidx >= ((buflen)-rowpos))
    {
      pos->block = zeropos.block+1;
      pos->pos   = msgidx - ((buflen)-rowpos);
    }
  else
    {
      pos->block = zeropos.block;
      pos->pos = zeropos.pos + msgidx;
    }
}


/*
 * db_give_range_size()
 * 
 * determines the number of bytes between 2 db_pos_t's
 */
unsigned long db_give_range_size(db_pos_t *start, db_pos_t *end)
{
  int i;
  unsigned long size;

  if (start->block > end->block)
    return 0; /* bad range */

  if (start->block >= nblocks || end->block >= nblocks)
    return 0; /* bad range */

  if (start->block == end->block)
    return (start->pos > end->pos) ? 0 : (end->pos - start->pos+1);

  if (start->pos > blklengths[start->block] || end->pos > blklengths[end->block])
    return 0; /* bad range */

  size = blklengths[start->block] - start->pos;

  for (i = start->block+1; i<end->block; i++)
    size += blklengths[i];

  size += end->pos;
  size++;

  return size;
}


/*
 * db_start_msg()
 *
 * parses a msg; uses msgbuf[] as data
 *
 * level & maxlevel are used to determine the max level of recursion (error-recovery)
 * level is raised before calling add_mime_children() except when maxlevel and level
 * are both zero, in that case the message is split in header/rest, add_mime_children
 * will not be called at all.
 *
 * returns the number of lines parsed or -1 on parse error, -2 on dbase error, -3 on memory error
 */
int db_start_msg(mime_message_t *msg, char *stopbound, int *level, int maxlevel)
{
  int len,sblen,result,totallines=0,nlines,hdrlines;
  struct mime_record *mr;
  char *newbound,*bptr;
  int continue_recursion = (maxlevel==0 && *level == 0) ? 0 : 1;

  trace(TRACE_DEBUG,"db_start_msg(): starting, stopbound: '%s'\n",stopbound);

  list_init(&msg->children);
  msg->message_has_errors = (!continue_recursion);


  /* read header */
  if (db_update_msgbuf(MSGBUF_FORCE_UPDATE) == -1)
    return -2;

  if ((hdrlines = mime_readheader(&msgbuf[msgidx], &msgidx, 
				  &msg->rfcheader, &msg->rfcheadersize)) < 0)
    return hdrlines;   /* error reading header */

  db_give_msgpos(&msg->bodystart);
  msg->rfcheaderlines = hdrlines;

  mime_findfield("content-type", &msg->rfcheader, &mr);
  if (continue_recursion &&
      mr && strncasecmp(mr->value,"multipart", strlen("multipart")) == 0)
    {
      trace(TRACE_DEBUG,"db_start_msg(): found multipart msg\n");

      /* multipart msg, find new boundary */
      for (bptr = mr->value; *bptr; bptr++) 
	if (strncasecmp(bptr, "boundary=", sizeof("boundary=")-1) == 0)
	    break;

      if (!bptr)
	{
	  trace(TRACE_ERROR, "db_start_msg(): could not find a new msg-boundary\n");
	  return -1; /* no new boundary ??? */
	}

      bptr += sizeof("boundary=")-1;
      if (*bptr == '\"')
	{
	  bptr++;
	  newbound = bptr;
	  while (*newbound && *newbound != '\"') newbound++;
	}
      else
	{
	  newbound = bptr;
	  while (*newbound && !isspace(*newbound) && *newbound!=';') newbound++;
	}

      len = newbound - bptr;
      if (!(newbound = (char*)my_malloc(len+1)))
	{
	  trace(TRACE_ERROR, "db_start_msg(): out of memory\n");
	  return -3;
	}

      strncpy(newbound, bptr, len);
      newbound[len] = '\0';

      trace(TRACE_DEBUG,"db_start_msg(): found new boundary: [%s], msgidx %lu\n",newbound,msgidx);

      /* advance to first boundary */
      if (db_update_msgbuf(MSGBUF_FORCE_UPDATE) == -1)
	{
	  trace(TRACE_ERROR, "db_startmsg(): error updating msgbuf\n");
	  my_free(newbound);
	  return -2;
	}

      while (msgbuf[msgidx])
	{
	  if (strncmp(&msgbuf[msgidx], newbound, strlen(newbound)) == 0)
	    break;

	  if (msgbuf[msgidx] == '\n')
	    totallines++;

	  msgidx++;
	}

      if (!msgbuf[msgidx])
	{
	  trace(TRACE_ERROR, "db_start_msg(): unexpected end-of-data\n");
	  my_free(newbound);
	  return -1;
	}

      msgidx += strlen(newbound);   /* skip the boundary */
      msgidx++;                     /* skip \n */
      totallines++;                 /* and count it */

      /* find MIME-parts */
      (*level)++;
      if ((nlines = db_add_mime_children(&msg->children, newbound, level, maxlevel)) < 0)
	{
	  trace(TRACE_ERROR, "db_start_msg(): error adding MIME-children\n");
	  my_free(newbound);
	  return nlines;
	}
      (*level)--;
      totallines += nlines;

      /* skip stopbound if present */
      if (stopbound)
	{
	  sblen = strlen(stopbound);
	  msgidx += (2+sblen); /* double hyphen preceeds */
	}

      my_free(newbound);
      newbound = NULL;

      if (msgidx > 0)
	{
	  /* walk back because bodyend is inclusive */
	  msgidx--;
	  db_give_msgpos(&msg->bodyend);
	  msgidx++;
	}
      else
	db_give_msgpos(&msg->bodyend); /* this case should never happen... */


      msg->bodysize = db_give_range_size(&msg->bodystart, &msg->bodyend);
      msg->bodylines = totallines;

      return totallines+hdrlines;                        /* done */
    }
  else
    {
      /* single part msg, read untill stopbound OR end of buffer */
      trace(TRACE_DEBUG,"db_start_msg(): found singlepart msg\n");

      if (stopbound)
	{
	  sblen = strlen(stopbound);

	  while (msgbuf[msgidx])
	    {
	      if (db_update_msgbuf(sblen+3) == -1)
		return -2;

	      if (msgbuf[msgidx] == '\n')
		msg->bodylines++;

	      if (msgbuf[msgidx+1] == '-' && msgbuf[msgidx+2] == '-' && 
		  strncmp(&msgbuf[msgidx+3], stopbound, sblen) == 0)
		{
		  db_give_msgpos(&msg->bodyend);
		  msg->bodysize = db_give_range_size(&msg->bodystart, &msg->bodyend);

		  msgidx++; /* msgbuf[msgidx] == '-' now */
		  
		  /* advance to after stopbound */
		  msgidx += sblen+2; /* (add 2 cause double hyphen preceeds) */
		  while (isspace(msgbuf[msgidx]))
		    {
		      if (msgbuf[msgidx] == '\n') totallines++;
		      msgidx++;
		    }

		  trace(TRACE_DEBUG,"db_start_msg(): stopbound reached\n");
		  return (totallines+msg->bodylines+hdrlines);
		}

	      msgidx++;
	    }

	  /* end of buffer reached, invalid message encountered: there should be a stopbound! */
	  /* but lets pretend there's nothing wrong... */
	  db_give_msgpos(&msg->bodyend);
	  msg->bodysize = db_give_range_size(&msg->bodystart, &msg->bodyend);
	  totallines += msg->bodylines;
	  
	  trace(TRACE_WARNING, "db_start_msg(): no stopbound where expected...\n");

/*	  return -1;
*/
	}
      else
	{
	  /* walk on till end of buffer */
	  result = 1;
	  while (1)
	    {
	      for ( ; msgidx < buflen-1; msgidx++)
		if (msgbuf[msgidx] == '\n')
		  msg->bodylines++;
	      
	      if (result == 0)
		{
		  /* end of msg reached, one char left in msgbuf */
		  if (msgbuf[msgidx] == '\n')
		    msg->bodylines++;

		  break; 
		}

	      result = db_update_msgbuf(MSGBUF_FORCE_UPDATE);
	      if (result == -1)
		return -2;
	    } 

	  db_give_msgpos(&msg->bodyend);
	  msg->bodysize = db_give_range_size(&msg->bodystart, &msg->bodyend);
	  totallines += msg->bodylines;
	}
    }

  trace(TRACE_DEBUG,"db_start_msg(): exit\n");

  return totallines;
}



/*
 * assume to enter just after a splitbound 
 * returns -1 on parse error, -2 on dbase error, -3 on memory error
 */
int db_add_mime_children(struct list *brothers, char *splitbound, int *level, int maxlevel)
{
  mime_message_t part;
  struct mime_record *mr;
  int sblen,nlines,totallines = 0,len;
  unsigned long dummy;
  char *bptr,*newbound;
  int continue_recursion = (maxlevel < 0 || *level < maxlevel) ? 1 : 0;

  trace(TRACE_DEBUG,"db_add_mime_children(): starting, splitbound: '%s'\n",splitbound);
  sblen = strlen(splitbound);

  do
    {
      db_update_msgbuf(MSGBUF_FORCE_UPDATE);
      memset(&part, 0, sizeof(part));
      part.message_has_errors = (!continue_recursion);

      /* should have a MIME header right here */
      if ((nlines = mime_readheader(&msgbuf[msgidx], &msgidx, &part.mimeheader, &dummy)) < 0)
	{
	  trace(TRACE_ERROR,"db_add_mime_children(): error reading MIME-header\n");
	  db_free_msg(&part);
	  return nlines;   /* error reading header */
	}
      totallines += nlines;

      mime_findfield("content-type", &part.mimeheader, &mr);

      if (continue_recursion &&
	  mr && strncasecmp(mr->value, "message/rfc822", strlen("message/rfc822")) == 0)
	{
	  trace(TRACE_DEBUG,"db_add_mime_children(): found an RFC822 message\n");

	  /* a message will follow */
	  if ((nlines = db_start_msg(&part, splitbound, level, maxlevel)) < 0)
	    {
	      trace(TRACE_ERROR,"db_add_mime_children(): error retrieving message\n");
	      db_free_msg(&part);
	      return nlines;
	    }
	  trace(TRACE_DEBUG,"db_add_mime_children(): got %d newlines from start_msg()\n",nlines);
	  totallines += nlines;
	  part.mimerfclines = nlines;
	}
      else if (continue_recursion &&
	       mr && strncasecmp(mr->value, "multipart", strlen("multipart")) == 0)
	{
	  trace(TRACE_DEBUG,"db_add_mime_children(): found a MIME multipart sub message\n");

	  /* multipart msg, find new boundary */
	  for (bptr = mr->value; *bptr; bptr++) 
	    if (strncasecmp(bptr, "boundary=", sizeof("boundary=")-1) == 0)
	      break;

	  if (!bptr)
	    {
	      trace(TRACE_ERROR, "db_add_mime_children(): could not find a new msg-boundary\n");
	      db_free_msg(&part);
	      return -1; /* no new boundary ??? */
	    }

	  bptr += sizeof("boundary=")-1;
	  if (*bptr == '\"')
	    {
	      bptr++;
	      newbound = bptr;
	      while (*newbound && *newbound != '\"') newbound++;
	    }
	  else
	    {
	      newbound = bptr;
	      while (*newbound && !isspace(*newbound) && *newbound!=';') newbound++;
	    }

	  len = newbound - bptr;
	  if (!(newbound = (char*)my_malloc(len+1)))
	    {
	      trace(TRACE_ERROR, "db_add_mime_children(): out of memory\n");
	      db_free_msg(&part);
	      return -3;
	    }

	  strncpy(newbound, bptr, len);
	  newbound[len] = '\0';

	  trace(TRACE_DEBUG,"db_add_mime_children(): found new boundary: [%s], msgidx %lu\n",
		newbound,msgidx);


	  /* advance to first boundary */
	  if (db_update_msgbuf(MSGBUF_FORCE_UPDATE) == -1)
	    {
	      trace(TRACE_ERROR, "db_add_mime_children(): error updating msgbuf\n");
	      db_free_msg(&part);
	      my_free(newbound);
	      return -2;
	    }

	  while (msgbuf[msgidx])
	    {
	      if (strncmp(&msgbuf[msgidx], newbound, strlen(newbound)) == 0)
		break;

	      if (msgbuf[msgidx] == '\n')
		{
		  totallines++;
		  part.bodylines++;
		}

	      msgidx++;
	    }

	  if (!msgbuf[msgidx])
	    {
	      trace(TRACE_ERROR, "db_add_mime_children(): unexpected end-of-data\n");
	      my_free(newbound);
	      db_free_msg(&part);
	      return -1;
	    }

	  msgidx += strlen(newbound);   /* skip the boundary */
	  msgidx++;                     /* skip \n */
	  totallines++;                 /* and count it */
	  part.bodylines++;
	  db_give_msgpos(&part.bodystart); /* remember position */

	  (*level)++;
	  if ((nlines = db_add_mime_children(&part.children, newbound, level, maxlevel)) < 0)
	    {
	      trace(TRACE_ERROR, "db_add_mime_children(): error adding mime children\n");
	      my_free(newbound);
	      db_free_msg(&part);
	      return nlines;
	    }
	  (*level)--;
	  
	  my_free(newbound);
	  newbound = NULL;
	  msgidx += sblen+2; /* skip splitbound */

	  if (msgidx > 0)
	    {
	      /* walk back because bodyend is inclusive */
	      msgidx--;
	      db_give_msgpos(&part.bodyend);
	      msgidx++;
	    }
	  else
	    db_give_msgpos(&part.bodyend); /* this case should never happen... */


	  part.bodysize = db_give_range_size(&part.bodystart, &part.bodyend);
	  part.bodylines += nlines;
	  totallines += nlines;
	}
      else
	{
	  trace(TRACE_DEBUG,"db_add_mime_children(): expecting body data...\n");

	  /* just body data follows, advance to splitbound */
	  db_give_msgpos(&part.bodystart);

	  while (msgbuf[msgidx])
	    {
	      if (db_update_msgbuf(sblen+3) == -1)
		{
		  db_free_msg(&part);
		  return -2;
		}

	      if (msgbuf[msgidx] == '\n')
		part.bodylines++;

	      if (msgbuf[msgidx+1] == '-' && msgbuf[msgidx+2] == '-' &&
		  strncmp(&msgbuf[msgidx+3], splitbound, sblen) == 0)
		break;

	      msgidx++;
	    }

	  /* at this point msgbuf[msgidx] is either
	   * 0 (end of data) -- invalid message!
	   * or the character right before '--<splitbound>'
	   */

	  totallines += part.bodylines;

	  if (!msgbuf[msgidx])
	    {
	      trace(TRACE_ERROR,"db_add_mime_children(): unexpected end of data\n");
	      db_free_msg(&part);
	      return -1; /* ?? splitbound should follow */
	    }

	  db_give_msgpos(&part.bodyend);
	  part.bodysize = db_give_range_size(&part.bodystart, &part.bodyend);

	  msgidx++; /* msgbuf[msgidx] == '-' after this statement */

	  msgidx += sblen+2;   /* skip the boundary & double hypen */
	}

      /* add this part to brother list */
      if (list_nodeadd(brothers, &part, sizeof(part)) == NULL)
	{
	  trace(TRACE_ERROR,"db_add_mime_children(): could not add node\n");
	  db_free_msg(&part);
	  return -3;
	}

      /* if double hyphen ('--') follows we're done */
      if (msgbuf[msgidx] == '-' && msgbuf[msgidx+1] == '-')
	{
	  trace(TRACE_DEBUG,"db_add_mime_children(): found end after boundary [%s],\n",splitbound);
	  trace(TRACE_DEBUG,"                        followed by [%.*s],\n",
		48,&msgbuf[msgidx]);

	  msgidx += 2; /* skip hyphens */

	  /* probably some newlines will follow (not specified but often there) */
	  while (msgbuf[msgidx] == '\n') 
	    {
	      totallines++;
	      msgidx++;
	    }

	  return totallines;
	}

      if (msgbuf[msgidx] == '\n')
	{
	  totallines++;
	  msgidx++;    /* skip the newline itself */
	}
    }
  while (msgbuf[msgidx]) ;

  trace(TRACE_WARNING,"db_add_mime_children(): sudden end of message\n");
  return totallines;

/*  trace(TRACE_ERROR,"db_add_mime_children(): invalid message (no ending boundary found)\n");
  return -1;
*/
}


/*
 * db_parse_as_text()
 * 
 * parses a message as a block of plain text; an explaining header is created
 * note that this will disturb the length calculations...
 * this function is called when normal parsing fails.
 * 
 * returns -1 on dbase failure, -2 on memory error
 */
int db_parse_as_text(mime_message_t *msg)
{
  int result;
  struct mime_record mr;
  struct element *el = NULL;   

  memset(msg, 0, sizeof(*msg));
  
  strcpy(mr.field, "subject");
  strcpy(mr.value, "dbmail IMAP server info: this message could not be parsed");
  el = list_nodeadd(&msg->rfcheader, &mr, sizeof(mr));
  if (!el)
    return -3;

  strcpy(mr.field, "from");
  strcpy(mr.value, "imapserver@dbmail.org");
  el = list_nodeadd(&msg->rfcheader, &mr, sizeof(mr));
  if (!el)
    return -3;

  msg->rfcheadersize = strlen("subject: dbmail IMAP server info: this message could not be parsed\r\n")
    + strlen("from: imapserver@dbmail.org\r\n");
  msg->rfcheaderlines = 4;

  db_give_msgpos(&msg->bodystart);

  /* walk on till end of buffer */
  result = 1;
  while (1)
    {
      for ( ; msgidx < buflen-1; msgidx++)
	if (msgbuf[msgidx] == '\n')
	  msg->bodylines++;
	      
      if (result == 0)
	{
	  /* end of msg reached, one char left in msgbuf */
	  if (msgbuf[msgidx] == '\n')
	    msg->bodylines++;

	  break; 
	}

      result = db_update_msgbuf(MSGBUF_FORCE_UPDATE);
      if (result == -1)
	return -2;
    } 

  db_give_msgpos(&msg->bodyend);
  msg->bodysize = db_give_range_size(&msg->bodystart, &msg->bodyend);

  return 0;
}
  


/*
 * db_msgdump()
 *
 * dumps a message to stderr
 * returns the size (in bytes) that the message occupies in memory
 */
int db_msgdump(mime_message_t *msg, unsigned long msguid, int level)
{
  struct element *curr;
  struct mime_record *mr;
  char *spaces;
  int size = sizeof(mime_message_t);

  if (level < 0)
    return 0;

  if (!msg)
    {
      trace(TRACE_DEBUG,"db_msgdump: got null\n");
      return 0;
    }

  spaces = (char*)my_malloc(3*level + 1);
  if (!spaces)
    return 0;

  memset(spaces, ' ', 3*level);
  spaces[3*level] = 0;


  trace(TRACE_DEBUG,"%sMIME-header: \n",spaces);
  curr = list_getstart(&msg->mimeheader);
  if (!curr)
    trace(TRACE_DEBUG,"%s%snull\n",spaces,spaces);
  else
    {
      while (curr)
	{
	  mr = (struct mime_record *)curr->data;
	  trace(TRACE_DEBUG,"%s%s[%s] : [%s]\n",spaces,spaces,mr->field, mr->value);
	  curr = curr->nextnode;
	  size += sizeof(struct mime_record);
	}
    }
  trace(TRACE_DEBUG,"%s*** MIME-header end\n",spaces);
     
  trace(TRACE_DEBUG,"%sRFC822-header: \n",spaces);
  curr = list_getstart(&msg->rfcheader);
  if (!curr)
    trace(TRACE_DEBUG,"%s%snull\n",spaces,spaces);
  else
    {
      while (curr)
	{
	  mr = (struct mime_record *)curr->data;
	  trace(TRACE_DEBUG,"%s%s[%s] : [%s]\n",spaces,spaces,mr->field, mr->value);
	  curr = curr->nextnode;
	  size += sizeof(struct mime_record);
	}
    }
  trace(TRACE_DEBUG,"%s*** RFC822-header end\n",spaces);

  trace(TRACE_DEBUG,"%s*** Body range:\n",spaces);
  trace(TRACE_DEBUG,"%s%s(%lu, %lu) - (%lu, %lu), size: %lu, newlines: %lu\n",
	spaces,spaces,
	msg->bodystart.block, msg->bodystart.pos,
	msg->bodyend.block, msg->bodyend.pos,
	msg->bodysize, msg->bodylines);
	

/*  trace(TRACE_DEBUG,"body: \n");
  db_dump_range(msg->bodystart, msg->bodyend, msguid);
  trace(TRACE_DEBUG,"*** body end\n");
*/
  trace(TRACE_DEBUG,"%sChildren of this msg:\n",spaces);
  
  curr = list_getstart(&msg->children);
  while (curr)
    {
      size += db_msgdump((mime_message_t*)curr->data,msguid,level+1);
      curr = curr->nextnode;
    }
  trace(TRACE_DEBUG,"%s*** child list end\n",spaces);

  my_free(spaces);
  return size;
}


/*
 * db_dump_range()
 *
 * dumps a range specified by start,end for the msg with ID msguid
 *
 * returns -1 on error or the number of output bytes otherwise
 */
long db_dump_range(FILE *outstream, db_pos_t start, db_pos_t end, unsigned long msguid)
{
  
  int i,startpos,endpos,j;
  long outcnt;
  int distance;

  trace(TRACE_DEBUG,"Dumping range: (%lu,%lu) - (%lu,%lu)\n",
	start.block, start.pos, end.block, end.pos);

  if (start.block > end.block)
    {
      trace(TRACE_ERROR,"db_dump_range(): bad range specified\n");
      return -1;
    }

  if (start.block == end.block && start.pos > end.pos)
    {
      trace(TRACE_ERROR,"db_dump_range(): bad range specified\n");
      return -1;
    }

  snprintf(query, DEF_QUERYSIZE, "SELECT messageblk FROM messageblk WHERE messageidnr = %lu"
	   " ORDER BY messageblknr", 
	   msguid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_dump_range(): could not get message\n");
      return (-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_dump_range(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (-1);
    }

  for (row = mysql_fetch_row(res), i=0; row && i < start.block; i++, row = mysql_fetch_row(res)) ;
      
  if (!row)
    {
      trace(TRACE_ERROR,"db_dump_range(): bad range specified\n");
      mysql_free_result(res);
      return -1;
    }

  outcnt = 0;

  /* just one block? */
  if (start.block == end.block)
    {
      /* dump everything */
      for (i=start.pos; i<=end.pos; i++)
	{
	  if (row[0][i] == '\n')
	    outcnt += fprintf(outstream,"\r\n");
	  else
	    outcnt += fprintf(outstream,"%c",row[0][i]);
	}

      fflush(outstream);
      mysql_free_result(res);
      return outcnt;
    }


  /* 
   * multiple block range specified
   */
  
  for (i=start.block, outcnt=0; i<=end.block; i++)
    {
      if (!row)
	{
	  trace(TRACE_ERROR,"db_dump_range(): bad range specified\n");
	  mysql_free_result(res);
	  return -1;
	}

      startpos = (i == start.block) ? start.pos : 0;
      endpos   = (i == end.block) ? end.pos+1 : (mysql_fetch_lengths(res))[0];

      distance = endpos - startpos;

      /* output */
      for (j=0; j<distance; j++)
	{
	  if (row[0][startpos+j] == '\n')
	    outcnt += fprintf(outstream,"\r\n");
	  else if (row[0][startpos+j])
	    outcnt += fprintf(outstream,"%c", row[0][startpos+j]);
	}
	
      row = mysql_fetch_row(res); /* fetch next row */
    }

  mysql_free_result(res);

  fflush(outstream);
  return outcnt;
}


/*
 * searches the given range within a msg for key
 */
int db_search_range(db_pos_t start, db_pos_t end, const char *key, unsigned long msguid)
{
  int i,startpos,endpos,j;
  int distance;

  if (start.block > end.block)
    {
      trace(TRACE_ERROR,"db_search_range(): bad range specified\n");
      return 0;
    }

  if (start.block == end.block && start.pos > end.pos)
    {
      trace(TRACE_ERROR,"db_search_range(): bad range specified\n");
      return 0;
    }

  snprintf(query, DEF_QUERYSIZE, "SELECT messageblk FROM messageblk WHERE messageidnr = %lu"
	   " ORDER BY messageblknr", 
	   msguid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_search_range(): could not get message\n");
      return 0;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_search_range(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return 0;
    }

  for (row = mysql_fetch_row(res), i=0; row && i < start.block; i++, row = mysql_fetch_row(res)) ;
      
  if (!row)
    {
      trace(TRACE_ERROR,"db_search_range(): bad range specified\n");
      mysql_free_result(res);
      return 0;
    }

  /* just one block? */
  if (start.block == end.block)
    {
      for (i=start.pos; i<=end.pos-strlen(key); i++)
	{
	  if (strncasecmp(&row[0][i], key, strlen(key)) == 0)
	    {
	      mysql_free_result(res);
	      return 1;
	    }
	}

      mysql_free_result(res);
      return 0;
    }


  /* 
   * multiple block range specified
   */
  
  for (i=start.block; i<=end.block; i++)
    {
      if (!row)
	{
	  trace(TRACE_ERROR,"db_search_range(): bad range specified\n");
	  mysql_free_result(res);
	  return 0;
	}

      startpos = (i == start.block) ? start.pos : 0;
      endpos   = (i == end.block) ? end.pos+1 : (mysql_fetch_lengths(res))[0];

      distance = endpos - startpos;

      for (j=0; j<distance-strlen(key); j++)
	{
	  if (strncasecmp(&row[0][i], key, strlen(key)) == 0)
	    {
	      mysql_free_result(res);
	      return 1;
	    }
	}
	
      row = mysql_fetch_row(res); /* fetch next row */
    }

  mysql_free_result(res);

  return 0;
}


/*
 * db_mailbox_msg_match()
 *
 * checks if a msg belongs to a mailbox 
 */
int db_mailbox_msg_match(unsigned long mailboxuid, unsigned long msguid)
{
  
  int val;

  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message WHERE messageidnr = %lu AND "
	   "mailboxidnr = %lu AND status<002 AND unique_id!=\"\"", msguid, mailboxuid); 

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_mailbox_msg_match(): could not get message\n");
      return (-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_mailbox_msg_match(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (-1);
    }

  val = mysql_num_rows(res);
  mysql_free_result(res);

  return val;
}


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
  unsigned long uid;
  int msn;

  if (!key)
    return -2;

  memset(rset, 0, setlen * sizeof(int));

  snprintf(query, DEF_QUERYSIZE, "SELECT messageidnr FROM message WHERE mailboxidnr = %lu "
	   "AND status<2 AND unique_id!=\"\" AND %s", mb->uid, key);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_search(): could not execute query\n");
      return (-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_search(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (-1);
    }

  while ((row = mysql_fetch_row(res)))
    {
      uid = strtoul(row[0], NULL, 10);
      msn = db_binary_search(mb->seq_list, mb->exists, uid);

      if (msn == -1 || msn >= setlen)
	{
	  mysql_free_result(res);
	  return 1;
	}

      rset[msn] = 1;
    }
	  
  mysql_free_result(res);
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
int db_exec_search(mime_message_t *msg, search_key_t *sk, unsigned long msguid)
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
int db_search_messages(char **search_keys, unsigned long **search_results, int *nsresults,
		       unsigned long mboxid)
{
  int i,qidx=0;

  trace(TRACE_WARNING, "db_search_messages(): SEARCH requested, arguments: ");
  for (i=0; search_keys[i]; i++)
    trace(TRACE_WARNING, "%s ", search_keys[i]);
  trace(TRACE_WARNING,"\n");

  qidx = snprintf(query, DEF_QUERYSIZE,
		  "SELECT messageidnr FROM message WHERE mailboxidnr = %lu AND status<2 "
		  "AND unique_id!=\"\"",
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

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_search_messages(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (-1);
    }

  *nsresults = mysql_num_rows(res);
  if (*nsresults == 0)
    {
      *search_results = NULL;
      mysql_free_result(res);
      return 0;
    }

  *search_results = (unsigned long*)my_malloc(sizeof(unsigned long) * *nsresults);
  if (!*search_results)
    {
      trace(TRACE_ERROR, "db_search_messages(): out of memory\n");
      mysql_free_result(res);
      return -1;
    }

  i=0;
  while ((row = mysql_fetch_row(res)) && i<*nsresults)
    {
      (*search_results)[i++] = strtoul(row[0],NULL,10);
    }
      

  mysql_free_result(res);

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
int db_binary_search(const unsigned long *array, int arraysize, unsigned long key)
{
  int low,high,mid;

  low = 0;
  high = arraysize-1;

  while (low <= high)
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
  
