/*
 * $Id$
 *
 * implementation of authentication functions &
 * user management for PostgreSQL dbases
 * (c) 2000-2002 IC&S, The Netherlands (http://www.ic-s.nl)
 */

#include "../auth.h"
#include "/usr/local/pgsql/include/libpq-fe.h"
/*#include "/usr/include/postgresql/libpq-fe.h"*/
#include "../list.h"
#include "../debug.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../dbmd5.h"
#include <crypt.h>
#include "../config.h"
#include <time.h>

#define AUTH_QUERY_SIZE 1024

/* 
 * var's for dbase connection/query
 */

PGconn *__auth_conn = NULL;  
PGresult *__auth_res;
char __auth_query_data[AUTH_QUERY_SIZE];


/*
 * auth_connect()
 *
 * initializes the connection for authentication.
 * 
 * returns 0 on success, -1 on failure.
 */
int auth_connect ()
{
  char connectionstring[255];

  /* connecting */
  snprintf (connectionstring, 255, "host=%s user=%s password=%s dbname=%s",
	   AUTH_HOST, AUTH_USER, AUTH_PASS, USERDATABASE);

  __auth_conn = PQconnectdb(connectionstring);

  if (PQstatus(__auth_conn) == CONNECTION_BAD) 
    {
      trace(TRACE_ERROR,"auth_connect(): PQconnectdb failed: %s",PQerrorMessage(__auth_conn));
      return -1;
    }

  /* database connection OK */  
  return 0;
}


int auth_disconnect()
{
  PQfinish(__auth_conn);
  return 0;
}


/* 
 * __auth_query()
 * 
 * Internal function for the authentication mechanism.
 * Executes the query q on the (global) connection __auth_conn.
 *
 * returns -1 on error, 0 on success.
 *
 * On success, the result is stored in __auth_res, data should be freed
 * by the caller when the query consists of a SELECT command. In all other
 * queries the result set is freed automatically by this function.
 */
int __auth_query(const char *q)
{
  int result;

  if (!q)
    {
      trace(TRACE_ERROR, "__auth_query(): got NULL query");
      return -1;
    }

  if (PQstatus(__auth_conn) != CONNECTION_OK)
    {
      trace(TRACE_DEBUG,"__auth_query(): connection lost, trying to reset...");
      PQreset(__auth_conn);

      if (PQstatus(__auth_conn) != CONNECTION_OK) 
	{
	  trace(TRACE_ERROR,"__auth_query(): Connection failed: [%s]",PQerrorMessage(__auth_conn));
	  trace(TRACE_ERROR,"__auth_query(): Could not establish dbase connection");
	  __auth_conn = NULL;
	  return -1;
	}
    }

  trace(TRACE_DEBUG,"__auth_query(): connection ok");
  trace(TRACE_DEBUG,"__auth_query(): executing query [%s]", q);

  __auth_res = PQexec(__auth_conn, q);
  if (!__auth_res)
    {
      trace(TRACE_ERROR, "__auth_query(): NULL result from query");
      return -1;
    }

  result = PQresultStatus(__auth_res);

  if (result != PGRES_COMMAND_OK && result != PGRES_TUPLES_OK)
    {
      trace(TRACE_ERROR,"__auth_query(): error executing query: [%s]",
	    PQresultErrorMessage(__auth_res));
      PQclear(__auth_res);
      return -1;
    }

  /* only save the result set for SELECT queries */
  if (strncasecmp(q, "SELECT", 6) != 0)
    {
      PQclear(__auth_res);
      __auth_res = NULL;
    }
  
  return 0;
}
      

/* 
 * __auth_insert_result()
 *
 * Internal function to retrieve the result of the last insertion.
 */
u64_t __auth_insert_result (const char *sequence_identifier)
{
  u64_t insert_result;

  /* postgres uses the currval call on a sequence to
     determine the result value of an insert query */

  snprintf (__auth_query_data, AUTH_QUERY_SIZE,"SELECT currval('%s_seq');",sequence_identifier);

  __auth_query(__auth_query_data);

  if (PQntuples(__auth_res)==0)
    {
      PQclear(__auth_res);
      return 0;
    }

  insert_result = strtoull(PQgetvalue(__auth_res, 0, 0), NULL, 10); 
  /* should only be one result value */

  PQclear(__auth_res);
  return insert_result;
}


/* string to be returned by auth_getencryption() */
#define _DESCSTRLEN 50
char __auth_encryption_desc_string[_DESCSTRLEN];

u64_t auth_user_exists(const char *username)
{
  u64_t uid;
  char *row;

  if (!username)
    {
      trace(TRACE_ERROR,"auth_user_exists(): got NULL as username\n");
      return 0;
    }

  snprintf(__auth_query_data, AUTH_QUERY_SIZE, "SELECT user_idnr FROM users WHERE userid='%s'",username);

  if (__auth_query(__auth_query_data)==-1)
    {
      trace(TRACE_ERROR, "auth_user_exists(): could not execute query\n");
      return -1;
    }

  if (PQntuples(__auth_res) == 0)
    {
      PQclear (__auth_res);
      return 0;
    }

  row = PQgetvalue(__auth_res, 0, 0);
  
  uid = (row) ? strtoull(row, 0, 0) : 0;

  PQclear(__auth_res);

  return uid;
}


/* return a list of existing users. -2 on mem error, -1 on db-error, 0 on succes */
int auth_get_known_users(struct list *users)
{
  u64_t PQcounter;
  char *value;

  if (!users)
    {
      trace(TRACE_ERROR,"auth_get_known_users(): got a NULL pointer as argument\n");
      return -2;
    }

  list_init(users);

  /* do a inverted (DESC) query because adding the names to the final list inverts again */
  snprintf(__auth_query_data, AUTH_QUERY_SIZE, "SELECT userid FROM users ORDER BY userid DESC");

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_get_known_users(): could not retrieve user list\n");
      return -1;
    }

  if (PQntuples(__auth_res)>0)
    {
      for (PQcounter = 0; PQcounter < PQntuples(__auth_res); PQcounter++)
       {
        value = PQgetvalue(__auth_res, PQcounter, 0);

          if (!list_nodeadd(users, value, strlen(value)+1))
	     {
	    list_freelist(&users->start);
	    return -2;
	     }
        }
    }
      
  PQclear(__auth_res);
  return 0;
}

u64_t auth_getclientid(u64_t useridnr)
{
  u64_t cid;
  char *row;

  snprintf(__auth_query_data, AUTH_QUERY_SIZE, "SELECT client_idnr FROM users WHERE user_idnr = %llu::bigint",useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_getclientid(): could not retrieve client id for user [%llu]\n",useridnr);
      return -1;
    }

  if (PQntuples (__auth_res)==0)
    {
        PQclear (__auth_res);
        return 0;
    }

  row = PQgetvalue (__auth_res, 0, 0);
  cid = (row) ? strtoull(row, 0, 10) : 0;

  PQclear(__auth_res);
  return cid;
}


u64_t auth_getmaxmailsize(u64_t useridnr)
{
  u64_t maxmailsize;
  char *row;

  snprintf(__auth_query_data, AUTH_QUERY_SIZE, "SELECT maxmail_size FROM users WHERE user_idnr = %llu::bigint",useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_getmaxmailsize(): could not retrieve client id for user [%llu::bigint]\n",useridnr);
      return -1;
    }

  if (PQntuples (__auth_res)==0)
    {
        PQclear (__auth_res);
        return 0;
    }

  row = PQgetvalue(__auth_res, 0, 0);
  maxmailsize = (row) ? strtoull(row, 0, 10) : -1;

  PQclear(__auth_res);
  return maxmailsize;
}



/*
 * auth_getencryption()
 *
 * returns a string describing the encryption used for the passwd storage
 * for this user.
 * The string is valid until the next function call; in absence of any 
 * encryption the string will be empty (not null).
 *
 * If the specified user does not exist an empty string will be returned.
 */
char* auth_getencryption(u64_t useridnr)
{
  char *row;
  __auth_encryption_desc_string[0] = 0;

  if (useridnr == -1 || useridnr == 0)
    {
      /* assume another function returned an error code (-1) 
       * or this user does not exist (0)
       */
      trace(TRACE_ERROR,"auth_getencryption(): got (%lld) as userid", useridnr);
      return __auth_encryption_desc_string; /* return empty */
    }
      
  snprintf(__auth_query_data, AUTH_QUERY_SIZE, "SELECT encryption_type FROM users WHERE user_idnr = %llu::bigint",
	   useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_getencryption(): could not retrieve encryption type for user [%llu::bigint]\n",
	    useridnr);
      return __auth_encryption_desc_string; /* return empty */
    }

  if (PQntuples(__auth_res)==0)
    {
      PQclear(__auth_res);
      return __auth_encryption_desc_string;  /* return empty */
    }

  row = PQgetvalue(__auth_res, 0, 0);
  strncpy(__auth_encryption_desc_string, row, _DESCSTRLEN);

  PQclear(__auth_res);
  return __auth_encryption_desc_string;
}


/* recursive function, should be called with checks == -1 from main routine */
int auth_check_user (const char *username, struct list *userids, int checks) 
{
  int occurences=0, r;
  PGresult *saveres = __auth_res;
  u64_t PQcounter;
  char *value;

  trace(TRACE_DEBUG,"auth_check_user(): checking user [%s] in alias table",username);
  
  if (checks > MAX_CHECKS_DEPTH)
    {
      trace(TRACE_ERROR, "auth_check_user(): maximum checking depth reached, there probably is a loop in "
	    "your alias table");
      return -1;
    }

  snprintf (__auth_query_data, AUTH_QUERY_SIZE,  "SELECT deliver_to FROM aliases WHERE "
	    "lower(alias) = lower('%s')", username);
  trace(TRACE_DEBUG,"auth_check_user(): checks [%d]", checks);

  if (__auth_query(__auth_query_data)==-1)
    {
      __auth_res = saveres;
      return 0;
    }
  
  if (PQntuples(__auth_res)<1) 
  {
      if (checks>0)
      {
          /* found the last one, this is the deliver to
           * but checks needs to be bigger then 0 because
           * else it could be the first query failure */

          list_nodeadd(userids, username, strlen(username)+1);
          trace (TRACE_DEBUG,"auth_check_user(): adding [%s] to deliver_to address",username);

          PQclear(__auth_res);
	  __auth_res = saveres;

          return 1;
      }
      else
      {
	trace (TRACE_DEBUG,"auth_check_user(): user %s not in aliases table", username);
	PQclear(__auth_res);
	__auth_res = saveres;

	return 0; 
      }
  }
      
  trace (TRACE_DEBUG,"auth_check_user(): into checking loop");

  if (PQntuples(__auth_res)>0)
    {
      for (PQcounter=0; PQcounter < PQntuples(__auth_res); PQcounter++)
        {
	  /* do a recursive search for deliver_to */
	  value = PQgetvalue(__auth_res, PQcounter, 0);
	  trace (TRACE_DEBUG,"auth_check_user(): checking user %s to %s",username, value);

	  r = auth_check_user (value, userids, (checks < 0) ? 1 : checks+1);
	  if (r < 0)
	    {
	      /* loop detected */
	      PQclear(__auth_res);
	      __auth_res = saveres;

	      if (checks > 0)
		return -1; /* still in recursive call */

	      if (userids->start)
		{
		  list_freelist(&userids->start);
		  userids->total_nodes = 0;
		}

	      return 0; /* report to calling routine: no results */
	    }

	  occurences += r;
        }
    }   
  
  /* trace(TRACE_INFO,"auth_check_user(): user [%s] has [%d] entries",username,occurences); */
  PQclear(__auth_res);
  __auth_res = saveres;

  return occurences;
}


/*
 * auth_check_user_ext()
 * 
 * As auth_check_user() but adds the numeric ID of the user found
 * to userids or the forward to the fwds.
 * 
 * returns the number of occurences. 
 */
int auth_check_user_ext(const char *username, struct list *userids, struct list *fwds, int checks) 
{
  int occurences=0;
  PGresult *saveres = __auth_res;
  u64_t PQcounter;
  char *value,*endptr;
  u64_t id;

  trace(TRACE_DEBUG,"auth_check_user_ext(): checking user [%s] in alias table",username);
  
  snprintf (__auth_query_data, AUTH_QUERY_SIZE,  "SELECT deliver_to FROM aliases WHERE "
	    "alias=\'%s\'",username);
  trace(TRACE_DEBUG,"auth_check_user_ext(): checks [%d]", checks);

  if (__auth_query(__auth_query_data) == -1)
    {
      __auth_res = saveres;
      return 0;
    }
  
  if (PQntuples(__auth_res)<1) 
  {
    if (checks>0)
      {
	/* found the last one, this is the deliver to
	 * but checks needs to be bigger then 0 because
	 * else it could be the first query failure */

	id = strtoull(username, &endptr, 10);
	if (*endptr == 0)
	  list_nodeadd(userids, &id, sizeof(id)); /* numeric deliver-to --> this is a userid */
	else
	  list_nodeadd(fwds, username, strlen(username)+1);

	trace (TRACE_DEBUG,"auth_check_user_ext(): adding [%s] to deliver_to address",username);

	PQclear(__auth_res);
	__auth_res = saveres;

	return 1;
      }
    else
      {
	trace (TRACE_DEBUG,"auth_check_user_ext(): user %s not in aliases table", username);
	PQclear(__auth_res);
	__auth_res = saveres;

	return 0; 
      }
  }
      
  trace (TRACE_DEBUG,"auth_check_user_ext(): into checking loop");

  if (PQntuples(__auth_res)>0)
    {
      for (PQcounter=0; PQcounter < PQntuples(__auth_res); PQcounter++)
        {
	  /* do a recursive search for deliver_to */
	  value = PQgetvalue(__auth_res, PQcounter, 0);
	  trace (TRACE_DEBUG,"auth_check_user_ext(): checking user %s to %s",username, value);
	  occurences += auth_check_user_ext (value, userids, fwds, 1);
        }
    }   
  
  PQclear(__auth_res);
  __auth_res = saveres;

  return occurences;
}

	
/* 
 * auth_adduser()
 *
 * adds a new user to the database 
 * and adds a INBOX 
 * returns a useridnr on succes, -1 on failure 
 */
u64_t auth_adduser (char *username, char *password, char *enctype, char *clientid, char *maxmail)
{
  u64_t useridnr;
  char *tst;
  u64_t size;
  char escapedpass[AUTH_QUERY_SIZE];

#ifdef _DBAUTH_STRICT_USER_CHECK
  /* first check to see if this user already exists */
  snprintf(__auth_query_data, AUTH_QUERY_SIZE, "SELECT * FROM users WHERE userid = '%s'", username);

  if (__auth_query(__auth_query_data) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "auth_adduser(): query failed\n");
      return -1;
    }

  if (PQntuples(__auth_res) > 0)
    {
      /* this username already exists */
      trace(TRACE_ERROR,"auth_adduser(): user already exists\n");
      return -1;
    }

  PQclear(__auth_res);
#endif

  size = strtoull(maxmail,&tst,10);
  if (tst)
    {
      if (tst[0] == 'M' || tst[0] == 'm')
	size *= 1000000;

      if (tst[0] == 'K' || tst[0] == 'k')
	size *= 1000;
    }
      
  if (strlen(password) >= AUTH_QUERY_SIZE)
    {
      trace(TRACE_ERROR,"auth_adduser(): password length is insane");
      return -1;
    }

  PQescapeString(escapedpass, password, strlen(password));

  snprintf (__auth_query_data, AUTH_QUERY_SIZE,"INSERT INTO users "
	    "(userid,passwd,client_idnr,maxmail_size,encryption_type) VALUES "
	    "('%s','%s',%s,%llu::bigint,'%s')",
	    username,escapedpass,clientid, size, enctype ? enctype : "");
	
  if (__auth_query(__auth_query_data) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "auth_adduser(): query for adding user failed");
      return -1;
    }

  useridnr = __auth_insert_result ("user_idnr");
	
  /* creating query for adding mailbox */
  snprintf (__auth_query_data, AUTH_QUERY_SIZE,"INSERT INTO mailboxes (owner_idnr, name) "
	    "VALUES (%llu::bigint,'INBOX')",
	    useridnr);
	
  trace (TRACE_DEBUG,"auth_adduser(): executing query for mailbox");

	
  if (__auth_query(__auth_query_data))
    {
      trace (TRACE_ERROR,"auth_adduser(): query failed for adding mailbox");
      return -1;
    }

  
  return useridnr;
}


int auth_delete_user(const char *username)
{
  snprintf (__auth_query_data, AUTH_QUERY_SIZE, "DELETE FROM users WHERE userid = '%s'",username);

  if (__auth_query(__auth_query_data) == -1)
    {
      /* query failed */
      trace (TRACE_ERROR, "auth_delete_user(): query for removing user failed");
      return -1;
    }

  return 0;
}
  
int auth_change_username(u64_t useridnr, const char *newname)
{
  snprintf(__auth_query_data, AUTH_QUERY_SIZE, "UPDATE users SET userid = '%s' WHERE user_idnr=%llu::bigint", 
	   newname, useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_change_username(): could not change name for user [%llu::bigint]\n",useridnr);
      return -1;
    }

  return 0;
}


int auth_change_password(u64_t useridnr, const char *newpass, const char *enctype)
{
  snprintf(__auth_query_data, AUTH_QUERY_SIZE, "UPDATE users SET passwd = '%s', encryption_type = '%s' "
	   " WHERE user_idnr=%llu::bigint", 
	   newpass, enctype ? enctype : "", useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_change_password(): could not change passwd for user [%llu::bigint]\n",useridnr);
      return -1;
    }

  return 0;
}


int auth_change_clientid(u64_t useridnr, u64_t newcid)
{
  snprintf(__auth_query_data, AUTH_QUERY_SIZE, "UPDATE users SET client_idnr = %llu::bigint WHERE user_idnr=%llu::bigint", 
	   newcid, useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_change_password(): could not change client id for user [%llu::bigint]\n",useridnr);
      return -1;
    }

  return 0;
}

int auth_change_mailboxsize(u64_t useridnr, u64_t newsize)
{
  snprintf(__auth_query_data, AUTH_QUERY_SIZE, "UPDATE users SET maxmail_size = %llu::bigint WHERE user_idnr=%llu::bigint", 
	   newsize, useridnr);

  if (__auth_query(__auth_query_data) == -1)
    {
      trace(TRACE_ERROR,"auth_change_password(): could not change maxmailsize for user [%llu::bigint]\n",
	    useridnr);
      return -1;
    }

  return 0;
}


/* 
 * auth_validate()
 *
 * tries to validate user 'user'
 *
 * returns useridnr on OK, 0 on validation failed, -1 on error 
 */
u64_t auth_validate (char *user, char *password)
{
  u64_t id;
  char *row;
  int is_validated = 0;
  char timestr[30];
  time_t td;
  struct tm tm;

  time(&td);              /* get time */
  tm = *localtime(&td);   /* get components */
  strftime(timestr, sizeof(timestr), "%G-%m-%d %H:%M:%S", &tm);

  snprintf(__auth_query_data, AUTH_QUERY_SIZE, "SELECT user_idnr, passwd, encryption_type FROM users "
	   "WHERE userid = '%s'", user);

  if (__auth_query(__auth_query_data)==-1)
    {
      trace(TRACE_ERROR, "auth_validate(): could not select user information");
      return -1;
    }

  if (PQntuples(__auth_res) == 0 ) 
    {
        PQclear (__auth_res);
        return 0;
    }

  row = PQgetvalue(__auth_res, 0, 2);

  if (!row || strcasecmp(row, "") == 0)
    {
      trace (TRACE_DEBUG,"auth_validate(): validating using cleartext passwords");
      row = PQgetvalue(__auth_res, 0, 1);
      is_validated = (strcmp(row, password) == 0) ? 1 : 0;
    }
  else if ( strcasecmp(row, "crypt") == 0)
    {
      trace (TRACE_DEBUG,"auth_validate(): validating using crypt() encryption");
      row = PQgetvalue(__auth_res, 0, 1);
      is_validated = (strcmp( crypt(password, row), row) == 0) ? 1 : 0;
    }

  if (is_validated)
    {
      row = PQgetvalue(__auth_res, 0, 0);
      id = (row) ? strtoull(row, NULL, 10) : 0;

      /* log login in the dbase */
      snprintf(__auth_query_data, AUTH_QUERY_SIZE, "UPDATE users SET last_login = '%s' "
	       "WHERE user_idnr = %llu::bigint", timestr, id);
      
      if (__auth_query(__auth_query_data)==-1)
	trace(TRACE_ERROR, "auth_validate(): could not update user login time");
    }
  else
    {
      id = 0;
    }

  PQclear(__auth_res);
  
  return (is_validated ? id : 0);
}


u64_t auth_md5_validate (char *username,unsigned char *md5_apop_he, char *apop_stamp)
{
  /* returns useridnr on OK, 0 on validation failed, -1 on error */
  
  char *checkstring;
  unsigned char *md5_apop_we;
  u64_t useridnr;	
  char *value;
  char timestr[30];
  time_t td;
  struct tm tm;

  time(&td);              /* get time */
  tm = *localtime(&td);   /* get components */
  strftime(timestr, sizeof(timestr), "%G-%m-%d %H:%M:%S", &tm);
  
  snprintf (__auth_query_data, AUTH_QUERY_SIZE, "SELECT passwd,user_idnr FROM users WHERE "
	    "userid=\'%s\'",username);
	
  if (__auth_query(__auth_query_data)==-1)
      return -1;

  if (PQntuples(__auth_res)<1)
    {
      PQclear(__auth_res);  
      /* no such user found */
      return 0;
    }
	
	/* now authenticate using MD5 hash comparisation  */
  value = PQgetvalue (__auth_res, 0, 0);

  /* value holds the password */

  trace (TRACE_DEBUG,"auth_md5_validate(): apop_stamp=[%s], userpw=[%s]",apop_stamp,
                    value);
	
  memtst((checkstring=(char *)my_malloc(strlen(apop_stamp)+strlen(value)+2))==NULL);
  snprintf(checkstring, strlen(apop_stamp)+strlen(value)+2, "%s%s",apop_stamp,value);

  md5_apop_we=makemd5(checkstring);
	
  trace(TRACE_DEBUG,"auth_md5_validate(): checkstring for md5 [%s] -> result [%s]",checkstring,
	md5_apop_we);
  trace(TRACE_DEBUG,"auth_md5_validate(): validating md5_apop_we=[%s] md5_apop_he=[%s]",
	md5_apop_we, md5_apop_he);

  if (strcmp(md5_apop_he,makemd5(checkstring))==0)
    {
      trace(TRACE_MESSAGE,"auth_md5_validate(): user [%s] is validated using APOP",username);
		
      value = PQgetvalue (__auth_res, 0, 1);     /* value contains useridnr */
      useridnr = (value) ? strtoull(value, NULL, 10) : 0;
      PQclear(__auth_res);
      my_free(checkstring);

       /* log login in the dbase */
      snprintf(__auth_query_data, AUTH_QUERY_SIZE, "UPDATE users SET last_login = '%s' "
	       "WHERE user_idnr = %llu::bigint", timestr, useridnr);
      
      if (__auth_query(__auth_query_data)==-1)
	trace(TRACE_ERROR, "auth_validate(): could not update user login time");

      return useridnr;
    }
	
  trace(TRACE_MESSAGE,"auth_md5_validate(): user [%s] could not be validated",username);

  if (__auth_res)
    PQclear(__auth_res);
  
  
  my_free(checkstring);
  
  return 0;
}


char *auth_get_userid (u64_t *useridnr)
{
  char *value;
  char *returnid = NULL;
  
  snprintf (__auth_query_data, AUTH_QUERY_SIZE,"SELECT userid FROM users WHERE user_idnr = %llu::bigint",
	   *useridnr);

  if (__auth_query(__auth_query_data)==-1)
    {
      trace(TRACE_ERROR,"auth_get_userid(): query failed");
      return 0;
    }

  if (PQntuples(__auth_res)<1) 
    {
      trace (TRACE_DEBUG,"auth_get_userid(): user has no username?");
      PQclear(__auth_res);
      
      return 0; 
    } 

  value = PQgetvalue (__auth_res, 0, 0);
  if (value)
    {
      if (!(returnid = (char *)my_malloc(strlen(value)+1)))
	{
	  trace(TRACE_ERROR,"auth_get_userid(): out of memory");
	  PQclear(__auth_res);
	  return NULL;
	}
	  
      strcpy (returnid, value);
    }
  
  PQclear(__auth_res);
  
  return returnid;
}



