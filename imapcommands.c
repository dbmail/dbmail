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
#include <stdlib.h>

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
  trace(TRACE_MESSAGE, "IMAPD: user (id:%d) %s tries login\n",userid,args[0]);

  if (userid == -1)
    {
      /* a db-error occurred */
      fprintf(ci->tx,"* BYE internal db error validating user\n");
      trace(TRACE_ERROR,"IMAPD: login(): db-validate error while validating user %s (pass %s).",
	    args[0],args[1]);
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
      fprintf(ci->tx,"* BYE internal db error validating user\n");
      trace(TRACE_ERROR,"IMAPD: authenticate(): db-validate error while validating user %s (pass %s).",
	    username,pass);
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
  unsigned long mboxid;
  char permstring[80];

  if (!check_state_and_args("SELECT", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  mboxid = db_findmailbox(args[0],ud->userid);
  if (mboxid == 0)
    {
      fprintf(ci->tx, "%s NO Could not find specified mailbox\n", tag);
      return 1;
    }
  if (mboxid == (unsigned long)(-1))
    {
      fprintf(ci->tx, "* BYE internal dbase error\n");
      return -1;
    }

  ud->mailbox.uid = mboxid;

  /* read info from mailbox */
  if (db_getmailbox(&ud->mailbox, ud->userid) == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1; /* fatal  */
    }

  /* show mailbox info */
  /* msg counts */
  fprintf(ci->tx, "* %d EXISTS\n",ud->mailbox.exists);
  fprintf(ci->tx, "* %d RECENT\n",ud->mailbox.recent);

  if (ud->mailbox.unseen > 0)
    fprintf(ci->tx, "* %d UNSEEN\n",ud->mailbox.unseen);

  /* flags */
  fprintf(ci->tx, "* FLAGS (");
  
  if (ud->mailbox.flags & IMAPFLAG_SEEN) fprintf(ci->tx, "\\Seen ");
  if (ud->mailbox.flags & IMAPFLAG_ANSWERED) fprintf(ci->tx, "\\Answered ");
  if (ud->mailbox.flags & IMAPFLAG_DELETED) fprintf(ci->tx, "\\Deleted ");
  if (ud->mailbox.flags & IMAPFLAG_FLAGGED) fprintf(ci->tx, "\\Flagged ");
  if (ud->mailbox.flags & IMAPFLAG_DRAFT) fprintf(ci->tx, "\\Draft ");
  if (ud->mailbox.flags & IMAPFLAG_RECENT) fprintf(ci->tx, "\\Recent ");

  fprintf(ci->tx,")\n");

  /* permanent flags */
  fprintf(ci->tx, "* OK [PERMANENTFLAGS (");
  if (ud->mailbox.flags & IMAPFLAG_SEEN) fprintf(ci->tx, "\\Seen ");
  if (ud->mailbox.flags & IMAPFLAG_ANSWERED) fprintf(ci->tx, "\\Answered ");
  if (ud->mailbox.flags & IMAPFLAG_DELETED) fprintf(ci->tx, "\\Deleted ");
  if (ud->mailbox.flags & IMAPFLAG_FLAGGED) fprintf(ci->tx, "\\Flagged ");
  if (ud->mailbox.flags & IMAPFLAG_DRAFT) fprintf(ci->tx, "\\Draft ");
  if (ud->mailbox.flags & IMAPFLAG_RECENT) fprintf(ci->tx, "\\Recent ");

  fprintf(ci->tx,")]\n");
  
  /* UID */
  fprintf(ci->tx,"* OK [UIDVALIDITY %lu] UID value\n",ud->mailbox.uid);

  /* permission */
  switch (ud->mailbox.permission)
    {
    case IMAPPERM_READ: sprintf(permstring, "READ-ONLY"); break;
    case IMAPPERM_READWRITE: sprintf(permstring, "READ-WRITE"); break;
    default: 
      /* invalid permission --> fatal */
      trace(TRACE_ERROR,"IMAPD: select(): detected invalid permission mode for mailbox %lu ('%s')\n",
	    ud->mailbox.uid, args[0]);

      fprintf(ci->tx, "* BYE fatal: detected invalid mailbox settings\n");
      return -1;
    }


  /* ok, update state */
  ud->state = IMAPCS_SELECTED;

  fprintf(ci->tx,"%s OK [%s] SELECT completed\n",tag,permstring);
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
  unsigned long mboxid;

  if (!check_state_and_args("EXAMINE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */


  mboxid = db_findmailbox(args[0],ud->userid);
  if (mboxid == 0)
    {
      fprintf(ci->tx, "%s NO Could not find specified mailbox\n", tag);
      return 1;
    }
  if (mboxid == (unsigned long)(-1))
    {
      fprintf(ci->tx, "* BYE internal dbase error\n");
      return -1;
    }

  ud->mailbox.uid = mboxid;

  /* read info from mailbox */
  if (db_getmailbox(&ud->mailbox, ud->userid) == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1; /* fatal */
    }

  /* show mailbox info */
  /* msg counts */
  fprintf(ci->tx, "* %d EXISTS\n",ud->mailbox.exists);
  fprintf(ci->tx, "* %d RECENT\n",ud->mailbox.recent);

  if (ud->mailbox.unseen > 0)
    fprintf(ci->tx, "* %d UNSEEN\n",ud->mailbox.unseen);

  /* flags */
  fprintf(ci->tx, "* FLAGS (");
  
  if (ud->mailbox.flags & IMAPFLAG_SEEN) fprintf(ci->tx, "\\Seen ");
  if (ud->mailbox.flags & IMAPFLAG_ANSWERED) fprintf(ci->tx, "\\Answered ");
  if (ud->mailbox.flags & IMAPFLAG_DELETED) fprintf(ci->tx, "\\Deleted ");
  if (ud->mailbox.flags & IMAPFLAG_FLAGGED) fprintf(ci->tx, "\\Flagged ");
  if (ud->mailbox.flags & IMAPFLAG_DRAFT) fprintf(ci->tx, "\\Draft ");
  if (ud->mailbox.flags & IMAPFLAG_RECENT) fprintf(ci->tx, "\\Recent ");

  fprintf(ci->tx,")\n");

  /* permanent flags */
  fprintf(ci->tx, "* OK [PERMANENTFLAGS (");
  if (ud->mailbox.flags & IMAPFLAG_SEEN) fprintf(ci->tx, "\\Seen ");
  if (ud->mailbox.flags & IMAPFLAG_ANSWERED) fprintf(ci->tx, "\\Answered ");
  if (ud->mailbox.flags & IMAPFLAG_DELETED) fprintf(ci->tx, "\\Deleted ");
  if (ud->mailbox.flags & IMAPFLAG_FLAGGED) fprintf(ci->tx, "\\Flagged ");
  if (ud->mailbox.flags & IMAPFLAG_DRAFT) fprintf(ci->tx, "\\Draft ");
  if (ud->mailbox.flags & IMAPFLAG_RECENT) fprintf(ci->tx, "\\Recent ");

  fprintf(ci->tx,")]\n");
  
  /* UID */
  fprintf(ci->tx,"* OK [UIDVALIDITY %lu] UID value\n",ud->mailbox.uid);

  /* update permission: examine forces read-only */
  ud->mailbox.permission = IMAPPERM_READ;

  /* ok, update state */
  ud->state = IMAPCS_SELECTED;

  fprintf(ci->tx,"%s OK [READ-ONLY] EXAMINE completed\n",tag);
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
  int result,i;
  char **chunks,*cpy;

  if (!check_state_and_args("CREATE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */
  

  /* remove trailing '/' if present */
  if (args[0][strlen(args[0]) - 1] == '/')
    args[0][strlen(args[0]) - 1] = '\0';

  /* check if this mailbox already exists */
  result = db_findmailbox(args[0], ud->userid);

  if (result == -1)
    {
      /* dbase failure */
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1; /* fatal */
    }

  if (result != 0)
    {
      /* mailbox already exists */
      fprintf(ci->tx,"%s NO mailbox already exists\n",tag);
      return 1;
    }

  /* alloc a ptr which can contain up to the full name */
  cpy = (char*)malloc(sizeof(char) * (strlen(args[0]) + 1));
  if (!cpy)
    {
      /* out of mem */
      trace(TRACE_ERROR, "IMAPD: create(): not enough memory\n");
      fprintf(ci->tx, "%s BYE server ran out of memory\n",tag);
      return -1;
    }

  /* split up the name & create parent folders as necessary */
  chunks = give_chunks(args[0], '/');

  if (chunks == NULL)
    {
      /* serious error while making chunks */
      trace(TRACE_ERROR, "IMAPD: create(): could not create chunks\n");
      fprintf(ci->tx, "%s BYE server ran out of memory\n",tag);
      free(cpy);
      return -1;
    }
  
  if (chunks[0] == NULL)
    {
      /* wrong argument */
      fprintf(ci->tx,"%s NO invalid mailbox name specified\n",tag);
      free_chunks(chunks);
      free(cpy);
      return 1;
    }

  /* now go create */
  strcpy(cpy,"");

  for (i=0; chunks[i]; i++)
    {
      if (strlen(chunks[i]) == 0)
	{
	  /* no can do */
	  fprintf(ci->tx, "%s NO invalid mailbox name specified\n",tag);
	  free_chunks(chunks);
	  free(cpy);
	  return 1;
	}

      if (i == 0)
	strcat(cpy, chunks[i]);
      else
	{
	  strcat(cpy, "/");
	  strcat(cpy, chunks[i]);
	}

      trace(TRACE_MESSAGE,"checking for '%s'...\n",cpy);

      /* check if this mailbox already exists */
      result = db_findmailbox(cpy, ud->userid);
      
      if (result == -1)
	{
	  /* dbase failure */
	  fprintf(ci->tx,"* BYE internal dbase error\n");
	  free_chunks(chunks);
	  free(cpy);
	  return -1; /* fatal */
	}

      if (result == 0)
	{
	  /* mailbox does not exist */
	  result = db_createmailbox(cpy, ud->userid);

	  if (result == -1)
	    {
	      fprintf(ci->tx,"%s NO CREATE failed\n",tag);
	      free_chunks(chunks);
	      free(cpy);
	      return 1;
	    }
	}
    }

  /* creation complete */
  free_chunks(chunks);
  free(cpy);

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
  int result,nchildren = 0;
  unsigned long *children = NULL,mboxid;

  if (!check_state_and_args("DELETE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  /* remove trailing '/' if present */
  if (args[0][strlen(args[0]) - 1] == '/')
    args[0][strlen(args[0]) - 1] = '\0';

  /* check if this mailbox exists */
  result = db_findmailbox(args[0], ud->userid);
  if (result == 0)
    {
      /* mailbox does not exist */
      fprintf(ci->tx,"%s NO mailbox does not exist\n",tag);
      return 1;
    }

  mboxid = result; /* the mailbox to be deleted */

  /* check if there is an attempt to delete inbox */
  if (strcasecmp(args[0],"inbox") == 0)
    {
      fprintf(ci->tx,"%s NO cannot delete special mailbox INBOX\n",tag);
      return 1;
    }

  /* check for children of this mailbox */
  result = db_listmailboxchildren(mboxid, &children, &nchildren);
  if (result == -1)
    {
      /* error */
      trace(TRACE_ERROR, "IMAPD: delete(): cannot retrieve list of mailbox children\n");
      fprintf(ci->tx, "%s BYE dbase/memory error\n",tag);
      return -1;
    }

  if (nchildren != 0)
    {
      fprintf(ci->tx,"%s NO mailbox has children\n",tag);
      free(children);
      return 1;
    }
      
  /* ok remove mailbox */
  db_removemailbox(mboxid, ud->userid);

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






