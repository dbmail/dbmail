/*
 * $Id$
 * (c) 2000-2002 IC&S, The Netherlands (http://www.ic-s.nl)
 * implementation of authentication functions &
 * user management for mySQL dbases
 */

#include "../auth.h"
#include "/usr/include/mysql/mysql.h"
#include "../list.h"
#include "../debug.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../db.h"
#include "../dbmd5.h"


/* 
 * var's from dbmysql.c: 
 */
extern char *query; 
extern MYSQL conn;
extern MYSQL_RES *res;
extern MYSQL_ROW row;


u64_t db_user_exists(const char *username)
{
  u64_t uid;

  snprintf(query, DEF_QUERYSIZE, "SELECT useridnr FROM user WHERE userid='%s'",username);

  if (db_query(query)==-1)
    {
      trace(TRACE_ERROR, "db_user_exists(): could not execute query\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_user_exists: mysql_store_result failed: %s",mysql_error(&conn));
      return -1;
    }
  
  row = mysql_fetch_row(res);
  
  uid = (row && row[0]) ? strtoull(row[0], 0, 0) : 0;

  mysql_free_result(res);

  return uid;
}


/* return a list of existing users. -2 on mem error, -1 on db-error, 0 on succes */
int db_get_known_users(struct list *users)
{
  if (!users)
    {
      trace(TRACE_ERROR,"db_get_known_users(): got a NULL pointer as argument\n");
      return -2;
    }

  list_init(users);

  /* do a inverted (DESC) query because adding the names to the final list inverts again */
  snprintf(query, DEF_QUERYSIZE, "SELECT userid FROM user ORDER BY userid DESC");

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_get_known_users(): could not retrieve user list\n");
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_get_known_users(): mysql_store_result failed: %s",mysql_error(&conn));
      return -1;
    }
  
  while ((row = mysql_fetch_row(res)))
    {
      if (!list_nodeadd(users, row[0], strlen(row[0])+1))
	{
	  list_freelist(&users->start);
	  return -2;
	}
    }
      
  mysql_free_result(res);
  return 0;
}

u64_t db_getclientid(u64_t useridnr)
{
  u64_t cid;

  snprintf(query, DEF_QUERYSIZE, "SELECT clientid FROM user WHERE useridnr = %llu",useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_getclientid(): could not retrieve client id for user [%llu]\n",useridnr);
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getclientid(): could not store query result: [%s]\n",mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);
  cid = (row && row[0]) ? strtoull(row[0], 0, 10) : -1;

  mysql_free_result(res);
  return cid;
}


u64_t db_getmaxmailsize(u64_t useridnr)
{
  u64_t maxmailsize;

  snprintf(query, DEF_QUERYSIZE, "SELECT maxmail_size FROM user WHERE useridnr = %llu",useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_getmaxmailsize(): could not retrieve client id for user [%llu]\n",useridnr);
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_getmaxmailsize(): could not store query result: [%s]\n",
	    mysql_error(&conn));
      return -1;
    }

  row = mysql_fetch_row(res);
  maxmailsize = (row && row[0]) ? strtoull(row[0], 0, 10) : -1;

  mysql_free_result(res);
  return maxmailsize;
}



int db_check_user (char *username, struct list *userids, int checks) 
{
  
  int occurences=0;
  MYSQL_RES *myres;
  MYSQL_ROW myrow;
  
  trace(TRACE_DEBUG,"db_check_user(): checking user [%s] in alias table",username);
  
  snprintf (query, DEF_QUERYSIZE,  "SELECT * FROM aliases WHERE alias=\"%s\"",username);
  trace(TRACE_DEBUG,"db_check_user(): executing query : [%s] checks [%d]",query, checks);
  if (db_query(query)==-1)
      return 0;
  
  if ((myres = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_check_user: mysql_store_result failed: %s",mysql_error(&conn));
      return 0;
    }

  if (mysql_num_rows(myres)<1) 
  {
      if (checks>0)
      {
          /* found the last one, this is the deliver to
           * but checks needs to be bigger then 0 because
           * else it could be the first query failure */

          list_nodeadd(userids, username, strlen(username)+1);
          trace (TRACE_DEBUG,"db_check_user(): adding [%s] to deliver_to address",username);
          mysql_free_result(myres);
          return 1;
      }
      else
      {
      trace (TRACE_DEBUG,"db_check_user(): user %s not in aliases table", username);
      mysql_free_result(myres);
      return 0; 
      }
  }
      
  trace (TRACE_DEBUG,"db_check_user(): into checking loop");
  /* myrow[2] is the deliver_to field */
  while ((myrow = mysql_fetch_row(myres))!=NULL)
  {
      /* do a recursive search for myrow[2] */
      trace (TRACE_DEBUG,"db_check_user(): checking user %s to %s",username, myrow[2]);
      occurences += db_check_user (myrow[2], userids, 1);
  }
  
  /* trace(TRACE_INFO,"db_check_user(): user [%s] has [%d] entries",username,occurences); */
  mysql_free_result(myres);

  return occurences;
}

	
u64_t db_adduser (char *username, char *password, char *clientid, char *maxmail)
{
  /* adds a new user to the database 
   * and adds a INBOX 
   * returns a useridnr on succes, -1 on failure */

  u64_t useridnr;
  char *tst;
  u64_t size;

  /* first check to see if this user already exists */
  snprintf(query, DEF_QUERYSIZE, "SELECT * FROM user WHERE userid = '%s'", username);

  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_adduser(): query [%s] failed\n", query);
      return -1;
    }

  if ((res = mysql_store_result(&conn)) == NULL) 
    {
      trace(TRACE_ERROR,"db_adduser(): mysql_store_result failed: %s",mysql_error(&conn));
      return 0;
    }

  if (mysql_num_rows(res) > 0)
    {
      /* this username already exists */
      trace(TRACE_ERROR,"db_adduser(): user already exists\n");
      return -1;
    }

  mysql_free_result(res);

  size = strtoull(maxmail,&tst,10);
  if (tst)
    {
      if (tst[0] == 'M' || tst[0] == 'm')
	size *= 1000000;

      if (tst[0] == 'K' || tst[0] == 'k')
	size *= 1000;
    }
      
  snprintf (query, DEF_QUERYSIZE,"INSERT INTO user (userid,passwd,clientid,maxmail_size) VALUES "
	   "('%s','%s',%s,%llu)",
	   username,password,clientid, size);
	
  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_adduser(): query for adding user failed : [%s]", query);
      return -1;
    }

  useridnr = db_insert_result ();
	
  /* creating query for adding mailbox */
  snprintf (query, DEF_QUERYSIZE,"INSERT INTO mailbox (owneridnr, name) VALUES (%llu,'INBOX')",
	   useridnr);
	
  trace (TRACE_DEBUG,"db_adduser(): executing query for mailbox: [%s]", query);

	
  if (db_query(query))
    {
      trace (TRACE_ERROR,"db_adduser(): query failed for adding mailbox: [%s]",query);
      return -1;
    }

  
  return useridnr;
}


int db_delete_user(const char *username)
{
  snprintf (query, DEF_QUERYSIZE, "DELETE FROM user WHERE userid = '%s'",username);

  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_delete_user(): query for removing user failed : [%s]", query);
      return -1;
    }

  return 0;
}
  
int db_change_username(u64_t useridnr, const char *newname)
{
  snprintf(query, DEF_QUERYSIZE, "UPDATE users SET userid = '%s' WHERE useridnr=%llu", 
	   newname, useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_change_username(): could not change name for user [%llu]\n",useridnr);
      return -1;
    }

  return 0;
}


int db_change_password(u64_t useridnr, const char *newpass)
{
  snprintf(query, DEF_QUERYSIZE, "UPDATE users SET passwd = '%s' WHERE useridnr=%llu", 
	   newpass, useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_change_password(): could not change passwd for user [%llu]\n",useridnr);
      return -1;
    }

  return 0;
}


int db_change_clientid(u64_t useridnr, u64_t newcid)
{
  snprintf(query, DEF_QUERYSIZE, "UPDATE users SET clientid = %llu WHERE useridnr=%llu", 
	   newcid, useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_change_password(): could not change client id for user [%llu]\n",useridnr);
      return -1;
    }

  return 0;
}

int db_change_mailboxsize(u64_t useridnr, u64_t newsize)
{
  snprintf(query, DEF_QUERYSIZE, "UPDATE users SET maxmail_size = %llu WHERE useridnr=%llu", 
	   newsize, useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_change_password(): could not change maxmailsize for user [%llu]\n",
	    useridnr);
      return -1;
    }

  return 0;
}

u64_t db_validate (char *user, char *password)
{
  /* returns useridnr on OK, 0 on validation failed, -1 on error */
  
  u64_t id;

  
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

  id = (row && row[0]) ? strtoull(row[0], NULL, 10) : 0;
  
  
  mysql_free_result(res);
  return id;
}

u64_t db_md5_validate (char *username,unsigned char *md5_apop_he, char *apop_stamp)
{
  /* returns useridnr on OK, 0 on validation failed, -1 on error */
  
  char *checkstring;
  unsigned char *md5_apop_we;
  u64_t useridnr;	

  
  
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
		
      useridnr = (row && row[1]) ? strtoull(row[1], NULL, 10) : 0;
	
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


char *db_get_userid (u64_t *useridnr)
{
  /* returns the mailbox id (of mailbox inbox) for a user or a 0 if no mailboxes were found */
  
  char *returnid = NULL;
  
  snprintf (query, DEF_QUERYSIZE,"SELECT userid FROM user WHERE useridnr = %llu",
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
