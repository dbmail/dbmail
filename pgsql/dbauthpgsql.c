/*
 * $Id$
 *
 * implementation of authentication functions &
 * user management for PostgreSQL dbases
 * (c) 2000-2002 IC&S, The Netherlands (http://www.ic-s.nl)
 */

#include "../auth.h"
#include "/usr/local/pgsql/include/libpq-fe.h"
#include "../list.h"
#include "../debug.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../db.h"
#include "../dbmd5.h"
#include <crypt.h>

/* 
 * var's from dbpgsql.c: 
 */

extern PGconn *conn;  
extern PGresult *res;
extern PGresult *checkres;
extern char *query;
extern char *value; /* used for PQgetvalue */
extern unsigned long PQcounter; /* used for PQgetvalue loops */


/* string to be returned by db_getencryption() */
#define _DESCSTRLEN 50
char __db_encryption_desc_string[_DESCSTRLEN];

u64_t db_user_exists(const char *username)
{
  u64_t uid;
  char *row;

  if (!username)
    {
      trace(TRACE_ERROR,"db_user_exists(): got NULL as username\n");
      return 0;
    }

  snprintf(query, DEF_QUERYSIZE, "SELECT user_idnr FROM users WHERE userid='%s'",username);

  if (db_query(query)==-1)
    {
      trace(TRACE_ERROR, "db_user_exists(): could not execute query\n");
      return -1;
    }

  if (PQntuples(res) == 0)
    {
      PQclear (res);
      return 0;
    }

  row = PQgetvalue(res, 0, 0);
  
  uid = (row) ? strtoull(row, 0, 0) : 0;

  PQclear(res);

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
  snprintf(query, DEF_QUERYSIZE, "SELECT userid FROM users ORDER BY userid DESC");

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_get_known_users(): could not retrieve user list\n");
      return -1;
    }

  if (PQntuples(res)>0)
    {
      for (PQcounter = 0; PQcounter < PQntuples(res); PQcounter++)
       {
        value = PQgetvalue(res, PQcounter, 0);

          if (!list_nodeadd(users, value, strlen(value)+1))
	     {
	    list_freelist(&users->start);
	    return -2;
	     }
        }
    }
      
  PQclear(res);
  return 0;
}

u64_t db_getclientid(u64_t useridnr)
{
  u64_t cid;
  char *row;

  snprintf(query, DEF_QUERYSIZE, "SELECT client_idnr FROM users WHERE user_idnr = %llu",useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_getclientid(): could not retrieve client id for user [%llu]\n",useridnr);
      return -1;
    }

  if (PQntuples (res)==0)
    {
        PQclear (res);
        return 0;
    }

  row = PQgetvalue (res, 0, 0);
  cid = (row) ? strtoull(row, 0, 10) : 0;

  PQclear(res);
  return cid;
}


u64_t db_getmaxmailsize(u64_t useridnr)
{
  u64_t maxmailsize;
  char *row;

  snprintf(query, DEF_QUERYSIZE, "SELECT maxmail_size FROM users WHERE user_idnr = %llu",useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_getmaxmailsize(): could not retrieve client id for user [%llu]\n",useridnr);
      return -1;
    }

  if (PQntuples (res)==0)
    {
        PQclear (res);
        return 0;
    }

  row = PQgetvalue(res, 0, 0);
  maxmailsize = (row) ? strtoull(row, 0, 10) : -1;

  PQclear(res);
  return maxmailsize;
}



/*
 * db_getencryption()
 *
 * returns a string describing the encryption used for the passwd storage
 * for this user.
 * The string is valid until the next function call; in absence of any 
 * encryption the string will be empty (not null).
 *
 * If the specified user does not exist an empty string will be returned.
 */
char* db_getencryption(u64_t useridnr)
{
  char *row;
  __db_encryption_desc_string[0] = 0;

  if (useridnr == -1 || useridnr == 0)
    {
      /* assume another function returned an error code (-1) 
       * or this user does not exist (0)
       */
      trace(TRACE_ERROR,"db_getencryption(): got (%lld) as userid", useridnr);
      return __db_encryption_desc_string; /* return empty */
    }
      
  snprintf(query, DEF_QUERYSIZE, "SELECT encryption_type FROM users WHERE user_idnr = %llu",
	   useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_getencryption(): could not retrieve encryption type for user [%llu]\n",
	    useridnr);
      return __db_encryption_desc_string; /* return empty */
    }

  if (PQntuples(res)==0)
    {
      PQclear(res);
      return __db_encryption_desc_string;  /* return empty */
    }

  row = PQgetvalue(res, 0, 0);
  strncpy(__db_encryption_desc_string, row, _DESCSTRLEN);

  PQclear(res);
  return __db_encryption_desc_string;
}



int db_check_user (char *username, struct list *userids, int checks) 
{
  
  int occurences=0;
  PGresult *saveres = res;
  
  trace(TRACE_DEBUG,"db_check_user(): checking user [%s] in alias table",username);
  
  snprintf (query, DEF_QUERYSIZE,  "SELECT * FROM aliases WHERE alias=\'%s\'",username);
  trace(TRACE_DEBUG,"db_check_user(): executing query : [%s] checks [%d]",query, checks);

  if (db_query(query)==-1)
    {
      res = saveres;
      return 0;
    }
  
  if (PQntuples(res)<1) 
  {
      if (checks>0)
      {
          /* found the last one, this is the deliver to
           * but checks needs to be bigger then 0 because
           * else it could be the first query failure */

          list_nodeadd(userids, username, strlen(username)+1);
          trace (TRACE_DEBUG,"db_check_user(): adding [%s] to deliver_to address",username);

          PQclear(res);
	  res = saveres;

          return 1;
      }
      else
      {
	trace (TRACE_DEBUG,"db_check_user(): user %s not in aliases table", username);
	PQclear(res);
	res = saveres;

	return 0; 
      }
  }
      
  trace (TRACE_DEBUG,"db_check_user(): into checking loop");

  if (PQntuples(res)>0)
    {
  /* field nr. 2 of res is the deliver_to field */
    for (PQcounter=0; PQcounter < PQntuples(res); PQcounter++)
        {
        /* do a recursive search for res[2] */
        value = PQgetvalue(res, PQcounter, 2);
        trace (TRACE_DEBUG,"db_check_user(): checking user %s to %s",username, value);
        occurences += db_check_user (value, userids, 1);
        }
    }   
  
  /* trace(TRACE_INFO,"db_check_user(): user [%s] has [%d] entries",username,occurences); */
  PQclear(res);
  res = saveres;

  return occurences;
}

	
/* 
 * db_adduser()
 *
 * adds a new user to the database 
 * and adds a INBOX 
 * returns a useridnr on succes, -1 on failure 
 */
u64_t db_adduser (char *username, char *password, char *enctype, char *clientid, char *maxmail)
{
  u64_t useridnr;
  char *tst;
  u64_t size;

  /* first check to see if this user already exists */
  snprintf(query, DEF_QUERYSIZE, "SELECT * FROM users WHERE userid = '%s'", username);

  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_adduser(): query [%s] failed\n", query);
      return -1;
    }

  if (PQntuples(res) > 0)
    {
      /* this username already exists */
      trace(TRACE_ERROR,"db_adduser(): user already exists\n");
      return -1;
    }

  PQclear(res);

  size = strtoull(maxmail,&tst,10);
  if (tst)
    {
      if (tst[0] == 'M' || tst[0] == 'm')
	size *= 1000000;

      if (tst[0] == 'K' || tst[0] == 'k')
	size *= 1000;
    }
      
  snprintf (query, DEF_QUERYSIZE,"INSERT INTO users "
	    "(userid,passwd,client_idnr,maxmail_size,encryption_type) VALUES "
	    "('%s','%s',%s,%llu,%s)",
	    username,password,clientid, size, enctype ? enctype : "");
	
  if (db_query(query) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "db_adduser(): query for adding user failed : [%s]", query);
      return -1;
    }

  useridnr = db_insert_result ("user_idnr");
	
  /* creating query for adding mailbox */
  snprintf (query, DEF_QUERYSIZE,"INSERT INTO mailboxes (owner_idnr, name) VALUES (%llu,'INBOX')",
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
  snprintf (query, DEF_QUERYSIZE, "DELETE FROM users WHERE userid = '%s'",username);

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
  snprintf(query, DEF_QUERYSIZE, "UPDATE users SET userid = '%s' WHERE user_idnr=%llu", 
	   newname, useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_change_username(): could not change name for user [%llu]\n",useridnr);
      return -1;
    }

  return 0;
}


int db_change_password(u64_t useridnr, const char *newpass, const char *enctype)
{
  snprintf(query, DEF_QUERYSIZE, "UPDATE users SET passwd = '%s', encryption_type = '%s' "
	   " WHERE user_idnr=%llu", 
	   newpass, enctype ? enctype : "", useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_change_password(): could not change passwd for user [%llu]\n",useridnr);
      return -1;
    }

  return 0;
}


int db_change_clientid(u64_t useridnr, u64_t newcid)
{
  snprintf(query, DEF_QUERYSIZE, "UPDATE users SET client_idnr = %llu WHERE user_idnr=%llu", 
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
  snprintf(query, DEF_QUERYSIZE, "UPDATE users SET maxmail_size = %llu WHERE user_idnr=%llu", 
	   newsize, useridnr);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR,"db_change_password(): could not change maxmailsize for user [%llu]\n",
	    useridnr);
      return -1;
    }

  return 0;
}


/* 
 * db_validate()
 *
 * tries to validate user 'user'
 *
 * returns useridnr on OK, 0 on validation failed, -1 on error 
 */
u64_t db_validate (char *user, char *password)
{
  u64_t id;
  char *row;
  int is_validated = 0;
  
  snprintf(query, DEF_QUERYSIZE, "SELECT user_idnr, passwd, encryption_type FROM users "
	   "WHERE userid = '%s'", user);

  if (db_query(query)==-1)
    {
      trace(TRACE_ERROR, "db_validate(): could not select user information");
      return -1;
    }

  if (PQntuples(res) == 0 ) 
    {
        PQclear (res);
        return 0;
    }

  row = PQgetvalue(res, 0, 2);

  if (!row || strcasecmp(row, "") == 0)
    {
      trace (TRACE_DEBUG,"db_validate(): validating using cleartext passwords");
      row = PQgetvalue(res, 0, 1);
      is_validated = (strcmp(row, password) == 0) ? 1 : 0;
    }
  else if ( strcasecmp(row, "crypt") == 0)
    {
      trace (TRACE_DEBUG,"db_validate(): validating using crypt() encryption");
      row = PQgetvalue(res, 0, 1);
      is_validated = (strcmp( crypt(password, row), row) == 0) ? 1 : 0;
    }

  if (is_validated)
    {
      row = PQgetvalue(res, 0, 0);
      id = (row) ? strtoull(row, NULL, 10) : 0;
    }
  else
    {
      id = 0;
    }

  PQclear(res);
  
  return (is_validated ? id : 0);
}


u64_t db_md5_validate (char *username,unsigned char *md5_apop_he, char *apop_stamp)
{
  /* returns useridnr on OK, 0 on validation failed, -1 on error */
  
  char *checkstring;
  unsigned char *md5_apop_we;
  u64_t useridnr;	
  
  
  snprintf (query, DEF_QUERYSIZE, "SELECT passwd,user_idnr FROM users WHERE userid=\'%s\'",username);
	
  if (db_query(query)==-1)
      return -1;

  if (PQntuples(res)<1)
    {
      PQclear(res);  
      /* no such user found */
      return 0;
    }
	
	/* now authenticate using MD5 hash comparisation  */
  value = PQgetvalue (res, 0, 0);

  /* value holds the password */

  trace (TRACE_DEBUG,"db_md5_validate(): apop_stamp=[%s], userpw=[%s]",apop_stamp,
                    value);
	
  memtst((checkstring=(char *)my_malloc(strlen(apop_stamp)+strlen(value)+2))==NULL);
  snprintf(checkstring, strlen(apop_stamp)+strlen(value)+2, "%s%s",apop_stamp,value);

  md5_apop_we=makemd5(checkstring);
	
  trace(TRACE_DEBUG,"db_md5_validate(): checkstring for md5 [%s] -> result [%s]",checkstring,
	md5_apop_we);
  trace(TRACE_DEBUG,"db_md5_validate(): validating md5_apop_we=[%s] md5_apop_he=[%s]",
	md5_apop_we, md5_apop_he);

  if (strcmp(md5_apop_he,makemd5(checkstring))==0)
    {
      trace(TRACE_MESSAGE,"db_md5_validate(): user [%s] is validated using APOP",username);
		
      value = PQgetvalue (res, 0, 1); 
      /* value contains useridnr */

      useridnr = (value) ? strtoull(value, NULL, 10) : 0;
	
      PQclear(res);
      
      my_free(checkstring);

      return useridnr;
    }
	
  trace(TRACE_MESSAGE,"db_md5_validate(): user [%s] could not be validated",username);

  if (res)
    PQclear(res);
  
  
  my_free(checkstring);
  
  return 0;
}


char *db_get_userid (u64_t *useridnr)
{
  /* returns the mailbox id (of mailbox inbox) for a user or a 0 if no mailboxes were found */
  
  char *returnid = NULL;
  
  snprintf (query, DEF_QUERYSIZE,"SELECT userid FROM users WHERE user_idnr = %llu",
	   *useridnr);

  trace(TRACE_DEBUG,"db_get_userid(): executing query : [%s]",query);
  if (db_query(query)==-1)
    {
      return 0;
    }

  if (PQntuples(res)<1) 
    {
      trace (TRACE_DEBUG,"db_get_userid(): user has no username?");
      PQclear(res);
      
      return 0; 
    } 

    value = PQgetvalue (res, 0, 0);
  if (value)
    {
      if (!(returnid = (char *)my_malloc(strlen(value)+1)))
	{
	  trace(TRACE_ERROR,"db_get_userid(): out of memory");
	  PQclear(res);
	  return NULL;
	}
	  
      strcpy (returnid, value);
    }
  
  PQclear(res);
  
  return returnid;
}



