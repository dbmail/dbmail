/* 
 * imapcommands.c
 * 
 * IMAP server command implementations
 */

#include "imapcommands.h"
#include "debug.h"
#include "imaputil.h"
#include "dbmysql.h"
#include <string.h>

#ifndef MAX_LINESIZE
#define MAX_LINESIZE 1024
#endif

/*
 * RETURN VALUES _ic_ functions:
 *
 * -1 Fatal error, close connection to user
 *  0 Succes
 *  1 Non-fatal error, connection stays alive
 */


/* 
 * ANY-STATE COMMANDS: capability, noop, logout
 */

/*
 * _ic_capability()
 *
 * returns a string to the client containing the server capabilities
 */
int _ic_capability(char *tag, char **args, ClientInfo *ci)
{
  if (!check_state_and_args("CAPABILITY", tag, args, 0, -1, ci))
    return 1; /* error, return */
  
  fprintf(ci->tx,"* CAPABILITY %s\n",IMAP_CAPABILITY_STRING);
  fprintf(ci->tx,"%s OK CAPABILITY completed\n",tag);

  return 0;
}


/*
 * _ic_noop()
 *
 * performs No operation
 */
int _ic_noop(char *tag, char **args, ClientInfo *ci)
{
  if (!check_state_and_args("NOOP", tag, args, 0, -1, ci))
    return 1; /* error, return */
    
  fprintf(ci->tx,"%s OK NOOP completed\n",tag);
  return 0;
}


/*
 * _ic_logout()
 *
 * prepares logout from IMAP-server
 */
int _ic_logout(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("LOGOUT", tag, args, 0, -1, ci))
    return 1; /* error, return */

  /* change status */
  ud->state = IMAPCS_LOGOUT;

  fprintf(ci->tx,"* BYE dbmail imap server kisses you goodbye\n");

  return 0;
}

/*
 * PRE-AUTHENTICATED STATE COMMANDS
 * login, authenticate
 */
/*
 * _ic_login()
 *
 * Performs login-request handling.
 */
int _ic_login(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;
  unsigned long userid;

  if (!check_state_and_args("LOGIN", tag, args, 2, IMAPCS_NON_AUTHENTICATED, ci))
    return 1; /* error, return */

  userid = db_validate(args[0], args[1]);
  trace(TRACE_MESSAGE, "IMAPD: user (id:%d) %s logged in\n",userid,args[0]);

  if (userid == -1)
    {
      /* a db-error occurred */
      fprintf(ci->tx,"* BAD internal db error validating user\n");
      trace(TRACE_MESSAGE,"IMAPD: db-validate error while validating user %s (pass %s).",args[0],args[1]);
      return -1;
    }

  if (userid == 0)
    {
      /* validation failed: invalid user/pass combination */
      fprintf(ci->tx, "%s NO login rejected\n",tag);
      return 1;
    }

  /* login ok */
  /* update client info */
  ud->userid = userid;
  ud->state = IMAPCS_AUTHENTICATED;

  fprintf(ci->tx,"%s OK LOGIN completed\n",tag);
  return 0;
}


/*
 * _ic_authenticate()
 * 
 * performs authentication using LOGIN mechanism:
 *
 *
 */
int _ic_authenticate(char *tag, char **args, ClientInfo *ci)
{
  unsigned long userid;
  char username[MAX_LINESIZE],buf[MAX_LINESIZE],pass[MAX_LINESIZE];
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("AUTHENTICATE", tag, args, 1, IMAPCS_NON_AUTHENTICATED, ci))
    return 1; /* error, return */

  /* check authentication method */
  if (strcasecmp(args[0], "login") != 0)
    {
      fprintf(ci->tx,"%s NO Invalid authentication mechanism specified\n",tag);
      return 1;
    }

  /* ask for username (base64 encoded) */
  base64encode("username\n\r",buf);
  fprintf(ci->tx,"+ %s\n",buf);
  fflush(ci->tx);
  fgets(buf, MAX_LINESIZE, ci->rx);
  base64decode(buf, username);

  /* ask for password */
  base64encode("password\n\r",buf);
  fprintf(ci->tx,"+ %s\n",buf);
  fflush(ci->tx);
  fgets(buf, MAX_LINESIZE, ci->rx);
  base64decode(buf,pass);
  

  /* try to validate user */
  userid = db_validate(username, pass);
  trace(TRACE_MESSAGE, "IMAPD: user (id:%d) %s logged in\n",userid,username);

  if (userid == -1)
    {
      /* a db-error occurred */
      fprintf(ci->tx,"* BAD internal db error validating user\n");
      trace(TRACE_MESSAGE,"IMAPD: db-validate error while validating user %s (pass %s).",username,pass);
      return -1;
    }

  if (userid == 0)
    {
      /* validation failed: invalid user/pass combination */
      fprintf(ci->tx, "%s NO login rejected\n",tag);
      return 1;
    }

  /* login ok */
  /* update client info */
  ud->userid = userid;
  ud->state = IMAPCS_AUTHENTICATED;
  
  fprintf(ci->tx,"%s OK AUTHENTICATE completed\n",tag);
  return 0;
}


/* 
 * AUTHENTICATED STATE COMMANDS 
 * select, examine, create, delete, rename, subscribe, 
 * unsubscribe, list, lsub, status, append
 */

/*
 * _ic_select()
 * 
 * select a specified mailbox
 */
int _ic_select(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("SELECT", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  /* ok, update state */
  ud->state = IMAPCS_SELECTED;

  fprintf(ci->tx,"%s OK SELECT completed\n",tag);
  return 0;
}


/*
 * _ic_examine()
 * 
 * examines a specified mailbox 
 */
int _ic_examine(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("EXAMINE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK EXAMINE completed\n",tag);
  return 0;
}


/*
 * _ic_create()
 *
 * create a mailbox
 */
int _ic_create(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("CREATE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK CREATE completed\n",tag);
  return 0;
}


/*
 * _ic_delete()
 *
 * deletes a specified mailbox
 */
int _ic_delete(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("DELETE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK DELETE completed\n",tag);
  return 0;
}


/*
 * _ic_rename()
 *
 * renames a specified mailbox
 */
int _ic_rename(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("RENAME", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK RENAME completed\n",tag);
  return 0;
}


/*
 * _ic_subscribe()
 *
 * adds a mailbox to the users' subscription list
 */
int _ic_subscribe(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("SUBSCRIBE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK SUBSCRIBE completed\n",tag);
  return 0;
}


/*
 * _ic_unsubscribe()
 *
 * removes a mailbox from the users' subscription list
 */
int _ic_unsubscribe(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("UNSUBSCRIBE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK UNSUBSCRIBE completed\n",tag);
  return 0;
}


/*
 * _ic_list()
 *
 * executes a list command
 */
int _ic_list(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("LIST", tag, args, 2, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  /* query mailboxes from DB */


  fprintf(ci->tx,"%s OK LIST completed\n",tag);
  return 0;
}


/*
 * _ic_lsub()
 *
 * list subscribed mailboxes
 */
int _ic_lsub(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("LSUB", tag, args, 2, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK  completed\n",tag);
  return 0;
}


/*
 * _ic_status()
 *
 * inquire the status of a mailbox
 */
int _ic_status(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("STATUS", tag, args, 2, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK STATUS completed\n",tag);
  return 0;
}


/*
 * _ic_append()
 *
 * append a (literal) message to a mailbox
 */
int _ic_append(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("APPEND", tag, args, 2, IMAPCS_AUTHENTICATED, ci) &&
      !check_state_and_args("APPEND", tag, args, 3, IMAPCS_AUTHENTICATED, ci) && 
      !check_state_and_args("APPEND", tag, args, 4, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK APPEND completed\n",tag);
  return 0;
}



/* 
 * SELECTED-STATE COMMANDS 
 * check, close, expunge, search, fetch, store, copy, uid
 */

/*
 * _ic_check()
 * 
 * request a checkpoint for the selected mailbox
 */
int _ic_check(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("CHECK", tag, args, 0, IMAPCS_SELECTED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK CHECK completed\n",tag);
  return 0;
}


/*
 * _ic_close()
 *
 * expunge deleted messages from selected mailbox & return to AUTH state
 */ 
int _ic_close(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("CLOSE", tag, args, 0, IMAPCS_SELECTED, ci))
    return 1; /* error, return */


  /* ok, update state */
  ud->state = IMAPCS_AUTHENTICATED;

  fprintf(ci->tx,"%s OK CLOSE completed\n",tag);
  return 0;
}


/*
 * _ic_expunge()
 *
 * expunge deleted messages from selected mailbox
 */
int _ic_expunge(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("EXPUNGE", tag, args, 0, IMAPCS_SELECTED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK EXPUNGE completed\n",tag);
  return 0;
}


/*
 * _ic_search()
 *
 * search the selected mailbox for messages
 */
int _ic_search(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("SEARCH", tag, args, 1, IMAPCS_SELECTED, ci) &&
      !check_state_and_args("SEARCH", tag, args, 2, IMAPCS_SELECTED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK SEARCH completed\n",tag);
  return 0;
}


/*
 * _ic_fetch()
 *
 * fetch message(s) from the selected mailbox
 */
int _ic_fetch(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("FETCH", tag, args, 2, IMAPCS_SELECTED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK FETCH completed\n",tag);
  return 0;
}


/*
 * _ic_store()
 *
 * alter message-associated data in selected mailbox
 */
int _ic_store(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("STORE", tag, args, 2, IMAPCS_SELECTED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK STORE completed\n",tag);
  return 0;
}


/*
 * _ic_copy()
 *
 * copy a message to another mailbox
 */
int _ic_copy(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("COPY", tag, args, 2, IMAPCS_SELECTED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK COPY completed\n",tag);
  return 0;
}


/*
 * _ic_uid()
 *
 * fetch/store/copy/search message UID's
 */
int _ic_uid(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("UID", tag, args, 2, IMAPCS_SELECTED, ci) &&
      !check_state_and_args("UID", tag, args, 3, IMAPCS_SELECTED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK UID completed\n",tag);
  return 0;
}






