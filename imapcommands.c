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
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef MAX_LINESIZE
#define MAX_LINESIZE 1024
#endif

#define MAX_RETRIES 20
#define IMAP_NFLAGS 6
const char *imap_flag_desc[IMAP_NFLAGS] = 
{ 
  "Seen", "Answered", "Deleted", "Flagged", "Draft", "Recent"
};

const char *imap_flag_desc_escaped[IMAP_NFLAGS] = 
{ 
  "\\Seen", "\\Answered", "\\Deleted", "\\Flagged", "\\Draft", "\\Recent"
};

int imapcommands_use_uid = 0;
int list_is_lsub = 0;

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
  
  fprintf(ci->tx,"* CAPABILITY %s\r\n",IMAP_CAPABILITY_STRING);
  fprintf(ci->tx,"%s OK CAPABILITY completed\r\n",tag);

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
    
  fprintf(ci->tx,"%s OK NOOP completed\r\n",tag);
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

  fprintf(ci->tx,"* BYE dbmail imap server kisses you goodbye\r\n");

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
  trace(TRACE_MESSAGE, "IMAPD: user (id:%d) %s tries login\r\n",userid,args[0]);

  if (userid == -1)
    {
      /* a db-error occurred */
      fprintf(ci->tx,"* BYE internal db error validating user\r\n");
      trace(TRACE_ERROR,"IMAPD: login(): db-validate error while validating user %s (pass %s).",
	    args[0],args[1]);
      return -1;
    }

  if (userid == 0)
    {
      /* validation failed: invalid user/pass combination */
      fprintf(ci->tx, "%s NO login rejected\r\n",tag);
      return 1;
    }

  /* login ok */
  /* update client info */
  ud->userid = userid;
  ud->state = IMAPCS_AUTHENTICATED;

  fprintf(ci->tx,"%s OK LOGIN completed\r\n",tag);
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
      fprintf(ci->tx,"%s NO Invalid authentication mechanism specified\r\n",tag);
      return 1;
    }

  /* ask for username (base64 encoded) */
  base64encode("username\r\n\r",buf);
  fprintf(ci->tx,"+ %s\r\n",buf);
  fflush(ci->tx);
  fgets(buf, MAX_LINESIZE, ci->rx);
  base64decode(buf, username);

  /* ask for password */
  base64encode("password\r\n\r",buf);
  fprintf(ci->tx,"+ %s\r\n",buf);
  fflush(ci->tx);
  fgets(buf, MAX_LINESIZE, ci->rx);
  base64decode(buf,pass);
  

  /* try to validate user */
  userid = db_validate(username, pass);

  if (userid == -1)
    {
      /* a db-error occurred */
      fprintf(ci->tx,"* BYE internal db error validating user\r\n");
      trace(TRACE_ERROR,"IMAPD: authenticate(): db-validate error while validating user %s (pass %s).",
	    username,pass);
      return -1;
    }

  if (userid == 0)
    {
      /* validation failed: invalid user/pass combination */
      fprintf(ci->tx, "%s NO login rejected\r\n",tag);
      trace(TRACE_MESSAGE, "IMAPD: user %s rejected\r\n",username);
      return 1;
    }

  /* login ok */
  /* update client info */
  ud->userid = userid;
  ud->state = IMAPCS_AUTHENTICATED;

  trace(TRACE_MESSAGE, "IMAPD: user (id:%d) %s logged in\r\n",userid,username);
  
  fprintf(ci->tx,"%s OK AUTHENTICATE completed\r\n",tag);
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
  unsigned long mboxid,key;
  int result,i,idx;
  char permstring[80];

  if (!check_state_and_args("SELECT", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  mboxid = db_findmailbox(args[0],ud->userid);
  if (mboxid == 0)
    {
      fprintf(ci->tx, "%s NO Could not find specified mailbox\r\n", tag);
      return 1;
    }
  if (mboxid == (unsigned long)(-1))
    {
      fprintf(ci->tx, "* BYE internal dbase error\r\n");
      return -1;
    }

  /* check if mailbox is selectable */
  result = db_isselectable(mboxid);
  if (result == 0)
    {
      /* error: cannot select mailbox */
      fprintf(ci->tx, "%s NO specified mailbox is not selectable\r\n",tag);
      return 1;
    }
  if (result == -1)
    {
      fprintf(ci->tx, "* BYE internal dbase error\r\n");
      return -1; /* fatal */
    }

  ud->mailbox.uid = mboxid;

  /* read info from mailbox */
  i = 0;
  do
    {
      result = db_getmailbox(&ud->mailbox, ud->userid);
    } while (result == 1 && i++<MAX_RETRIES);

  if (result == 1)
    {
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\r\n");
      return -1;
    }

  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1; /* fatal  */
    }

  /* show mailbox info */
  /* msg counts */
  fprintf(ci->tx, "* %d EXISTS\r\n",ud->mailbox.exists);
  fprintf(ci->tx, "* %d RECENT\r\n",ud->mailbox.recent);

  /* flags */
  fprintf(ci->tx, "* FLAGS (");
  
  if (ud->mailbox.flags & IMAPFLAG_SEEN) fprintf(ci->tx, "\\Seen ");
  if (ud->mailbox.flags & IMAPFLAG_ANSWERED) fprintf(ci->tx, "\\Answered ");
  if (ud->mailbox.flags & IMAPFLAG_DELETED) fprintf(ci->tx, "\\Deleted ");
  if (ud->mailbox.flags & IMAPFLAG_FLAGGED) fprintf(ci->tx, "\\Flagged ");
  if (ud->mailbox.flags & IMAPFLAG_DRAFT) fprintf(ci->tx, "\\Draft ");
  if (ud->mailbox.flags & IMAPFLAG_RECENT) fprintf(ci->tx, "\\Recent ");

  fprintf(ci->tx,")\r\n");

  /* permanent flags */
  fprintf(ci->tx, "* OK [PERMANENTFLAGS (");
  if (ud->mailbox.flags & IMAPFLAG_SEEN) fprintf(ci->tx, "\\Seen ");
  if (ud->mailbox.flags & IMAPFLAG_ANSWERED) fprintf(ci->tx, "\\Answered ");
  if (ud->mailbox.flags & IMAPFLAG_DELETED) fprintf(ci->tx, "\\Deleted ");
  if (ud->mailbox.flags & IMAPFLAG_FLAGGED) fprintf(ci->tx, "\\Flagged ");
  if (ud->mailbox.flags & IMAPFLAG_DRAFT) fprintf(ci->tx, "\\Draft ");
  if (ud->mailbox.flags & IMAPFLAG_RECENT) fprintf(ci->tx, "\\Recent ");

  fprintf(ci->tx,")]\r\n");
  
  /* UID */
  fprintf(ci->tx,"* OK [UIDVALIDITY %lu] UID value\r\n",ud->mailbox.uid);

  /* show idx of first unseen msg (if present) */
  key = db_first_unseen(ud->mailbox.uid);
  if (key == (unsigned long)(-1))
    {
      fprintf(ci->tx, "* BYE internal dbase error\r\n");
      return -1;
    }
  idx = binary_search(ud->mailbox.seq_list, ud->mailbox.exists, key);

  if (idx >= 0)
    fprintf(ci->tx,"* OK [UNSEEN %d] first unseen message\r\n",idx+1);

  /* permission */
  switch (ud->mailbox.permission)
    {
    case IMAPPERM_READ: sprintf(permstring, "READ-ONLY"); break;
    case IMAPPERM_READWRITE: sprintf(permstring, "READ-WRITE"); break;
    default: 
      /* invalid permission --> fatal */
      trace(TRACE_ERROR,"IMAPD: select(): detected invalid permission mode for mailbox %lu ('%s')\r\n",
	    ud->mailbox.uid, args[0]);

      fprintf(ci->tx, "* BYE fatal: detected invalid mailbox settings\r\n");
      return -1;
    }

  /* ok, update state */
  ud->state = IMAPCS_SELECTED;

  fprintf(ci->tx,"%s OK [%s] SELECT completed\r\n",tag,permstring);
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
  int result,i;

  if (!check_state_and_args("EXAMINE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */


  mboxid = db_findmailbox(args[0],ud->userid);
  if (mboxid == 0)
    {
      fprintf(ci->tx, "%s NO Could not find specified mailbox\r\n", tag);
      return 1;
    }
  if (mboxid == (unsigned long)(-1))
    {
      fprintf(ci->tx, "* BYE internal dbase error\r\n");
      return -1;
    }

  /* check if mailbox is selectable */
  result = db_isselectable(mboxid);
  if (result == 0)
    {
      /* error: cannot select mailbox */
      fprintf(ci->tx, "%s NO specified mailbox is not selectable\r\n",tag);
      return 1; /* fatal */
    }
  if (result == -1)
    {
      fprintf(ci->tx, "* BYE internal dbase error\r\n");
      return -1; /* fatal */
    }

  ud->mailbox.uid = mboxid;

  /* read info from mailbox */
  i = 0;
  do
    {
      result = db_getmailbox(&ud->mailbox, ud->userid);
    } while (result == 1 && i++<MAX_RETRIES);

  if (result == 1)
    {
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\r\n");
      return -1;
    }

  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1; /* fatal */
    }

  /* show mailbox info */
  /* msg counts */
  fprintf(ci->tx, "* %d EXISTS\r\n",ud->mailbox.exists);
  fprintf(ci->tx, "* %d RECENT\r\n",ud->mailbox.recent);

  /* flags */
  fprintf(ci->tx, "* FLAGS (");
  
  if (ud->mailbox.flags & IMAPFLAG_SEEN) fprintf(ci->tx, "\\Seen ");
  if (ud->mailbox.flags & IMAPFLAG_ANSWERED) fprintf(ci->tx, "\\Answered ");
  if (ud->mailbox.flags & IMAPFLAG_DELETED) fprintf(ci->tx, "\\Deleted ");
  if (ud->mailbox.flags & IMAPFLAG_FLAGGED) fprintf(ci->tx, "\\Flagged ");
  if (ud->mailbox.flags & IMAPFLAG_DRAFT) fprintf(ci->tx, "\\Draft ");
  if (ud->mailbox.flags & IMAPFLAG_RECENT) fprintf(ci->tx, "\\Recent ");

  fprintf(ci->tx,")\r\n");

  /* permanent flags */
  fprintf(ci->tx, "* OK [PERMANENTFLAGS (");
  if (ud->mailbox.flags & IMAPFLAG_SEEN) fprintf(ci->tx, "\\Seen ");
  if (ud->mailbox.flags & IMAPFLAG_ANSWERED) fprintf(ci->tx, "\\Answered ");
  if (ud->mailbox.flags & IMAPFLAG_DELETED) fprintf(ci->tx, "\\Deleted ");
  if (ud->mailbox.flags & IMAPFLAG_FLAGGED) fprintf(ci->tx, "\\Flagged ");
  if (ud->mailbox.flags & IMAPFLAG_DRAFT) fprintf(ci->tx, "\\Draft ");
  if (ud->mailbox.flags & IMAPFLAG_RECENT) fprintf(ci->tx, "\\Recent ");

  fprintf(ci->tx,")]\r\n");
  
  /* UID */
  fprintf(ci->tx,"* OK [UIDVALIDITY %lu] UID value\r\n",ud->mailbox.uid);

  /* update permission: examine forces read-only */
  ud->mailbox.permission = IMAPPERM_READ;

  /* ok, update state */
  ud->state = IMAPCS_SELECTED;

  fprintf(ci->tx,"%s OK [READ-ONLY] EXAMINE completed\r\n",tag);
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
  unsigned long mboxid;
  char **chunks,*cpy;

  if (!check_state_and_args("CREATE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */
  
  /* remove trailing '/' if present */
  while (strlen(args[0]) > 0 && args[0][strlen(args[0]) - 1] == '/')
    args[0][strlen(args[0]) - 1] = '\0';

  /* remove leading '/' if present */
  for (i=0; args[0][i] && args[0][i] == '/'; i++) ;
  memmove(&args[0][0],&args[0][i], (strlen(args[0]) - i) * sizeof(char));

  /* check if this mailbox already exists */
  mboxid = db_findmailbox(args[0], ud->userid);
  if (mboxid == (unsigned long)(-1))
    {
      /* dbase failure */
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1; /* fatal */
    }

  if (mboxid != 0)
    {
      /* mailbox already exists */
      fprintf(ci->tx,"%s NO mailbox already exists\r\n",tag);
      return 1;
    }

  /* check if new name is valid */
  if (!checkmailboxname(args[0]))
    {
      fprintf(ci->tx,"%s NO new mailbox name contains invalid characters\r\n",tag);
      return 1;
    }

  /* alloc a ptr which can contain up to the full name */
  cpy = (char*)malloc(sizeof(char) * (strlen(args[0]) + 1));
  if (!cpy)
    {
      /* out of mem */
      trace(TRACE_ERROR, "IMAPD: create(): not enough memory\r\n");
      fprintf(ci->tx, "%s BYE server ran out of memory\r\n",tag);
      return -1;
    }

  /* split up the name & create parent folders as necessary */
  chunks = give_chunks(args[0], '/');

  if (chunks == NULL)
    {
      /* serious error while making chunks */
      trace(TRACE_ERROR, "IMAPD: create(): could not create chunks\r\n");
      fprintf(ci->tx, "%s BYE server ran out of memory\r\n",tag);
      free(cpy);
      return -1;
    }
  
  if (chunks[0] == NULL)
    {
      /* wrong argument */
      fprintf(ci->tx,"%s NO invalid mailbox name specified\r\n",tag);
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
	  fprintf(ci->tx, "%s NO invalid mailbox name specified\r\n",tag);
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

      trace(TRACE_MESSAGE,"checking for '%s'...\r\n",cpy);

      /* check if this mailbox already exists */
      mboxid = db_findmailbox(cpy, ud->userid);
      
      if (mboxid == (unsigned long)(-1))
	{
	  /* dbase failure */
	  fprintf(ci->tx,"* BYE internal dbase error\r\n");
	  free_chunks(chunks);
	  free(cpy);
	  return -1; /* fatal */
	}

      if (mboxid == 0)
	{
	  /* mailbox does not exist */
	  result = db_createmailbox(cpy, ud->userid);

	  if (result == -1)
	    {
	      fprintf(ci->tx,"* BYE internal dbase error\r\n");
	      return -1; /* fatal */
	    }
	}
      else
	{
	  /* mailbox does exist, failure if no_inferiors flag set */
	  result = db_noinferiors(mboxid);
	  if (result == 1)
	    {
	      fprintf(ci->tx, "%s NO mailbox cannot have inferior names\r\n",tag);
	      free_chunks(chunks);
	      free(cpy);
	      return 1;
	    }

	  if (result == -1)
	    {
	      /* dbase failure */
	      fprintf(ci->tx,"* BYE internal dbase error\r\n");
	      free_chunks(chunks);
	      free(cpy);
	      return -1; /* fatal */
	    }
	}
    }

  /* creation complete */
  free_chunks(chunks);
  free(cpy);

  fprintf(ci->tx,"%s OK CREATE completed\r\n",tag);
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
  while (strlen(args[0]) > 0 && args[0][strlen(args[0]) - 1] == '/')
    args[0][strlen(args[0]) - 1] = '\0';

  /* check if this mailbox exists */
  mboxid = db_findmailbox(args[0], ud->userid);
  if (mboxid == (unsigned long)(-1))
    {
      /* dbase failure */
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1; /* fatal */
    }
  if (mboxid == 0)
    {
      /* mailbox does not exist */
      fprintf(ci->tx,"%s NO mailbox does not exist\r\n",tag);
      return 1;
    }

  /* check if there is an attempt to delete inbox */
  if (strcasecmp(args[0],"inbox") == 0)
    {
      fprintf(ci->tx,"%s NO cannot delete special mailbox INBOX\r\n",tag);
      return 1;
    }

  /* check for children of this mailbox */
  result = db_listmailboxchildren(mboxid, &children, &nchildren, "%");
  if (result == -1)
    {
      /* error */
      trace(TRACE_ERROR, "IMAPD: delete(): cannot retrieve list of mailbox children\r\n");
      fprintf(ci->tx, "%s BYE dbase/memory error\r\n",tag);
      return -1;
    }

  if (nchildren != 0)
    {
      /* mailbox has inferior names; error if \r\noselect specified */
      result = db_isselectable(mboxid);
      if (result == 0)
	{
	  fprintf(ci->tx,"%s NO mailbox is non-selectable\r\n",tag);
	  free(children);
	  return 1;
	}
      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\r\n");
	  free(children);
	  return -1; /* fatal */
	}

      /* mailbox has inferior names; remove all msgs and set noselect flag */
      result = db_removemsg(mboxid);
      if (result != -1)
	result = db_setselectable(mboxid, 0); /* set non-selectable flag */

      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\r\n");
	  free(children);
	  return -1; /* fatal */
	}

      /* ok done */
      fprintf(ci->tx,"%s OK DELETE completed\r\n",tag);
      free(children);
      return 0;
    }
      
  /* ok remove mailbox */
  db_removemailbox(mboxid, ud->userid);

  fprintf(ci->tx,"%s OK DELETE completed\r\n",tag);
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
  unsigned long mboxid,newmboxid,*children,oldnamelen,parentmboxid;
  int nchildren,i,result;
  char newname[IMAP_MAX_MAILBOX_NAMELEN],name[IMAP_MAX_MAILBOX_NAMELEN];

  if (!check_state_and_args("RENAME", tag, args, 2, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  /* remove trailing '/' if present */
  while (strlen(args[0]) > 0 && args[0][strlen(args[0]) - 1] == '/') args[0][strlen(args[0]) - 1] = '\0';
  while (strlen(args[1]) > 0 && args[1][strlen(args[1]) - 1] == '/') args[1][strlen(args[1]) - 1] = '\0';

  /* remove leading '/' if present for new name */
  for (i=0; args[1][i] && args[1][i] == '/'; i++) ;
  memmove(&args[1][0],&args[1][i], (strlen(args[1]) - i) * sizeof(char));


  /* check if new mailbox exists */
  mboxid = db_findmailbox(args[1], ud->userid);
  if (mboxid == (unsigned long)-1)
    {
      /* dbase failure */
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1; /* fatal */
    }
  if (mboxid != 0)
    {
      /* mailbox exists */
      fprintf(ci->tx,"%s NO new mailbox already exists\r\n",tag);
      return 1;
    }

  /* check if original mailbox exists */
  mboxid = db_findmailbox(args[0], ud->userid);
  if (mboxid == (unsigned long)-1)
    {
      /* dbase failure */
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1; /* fatal */
    }
  if (mboxid == 0)
    {
      /* mailbox does not exist */
      fprintf(ci->tx,"%s NO mailbox does not exist\r\n",tag);
      return 1;
    }

  /* check if new name is valid */
  if (!checkmailboxname(args[1]))
    {
      fprintf(ci->tx,"%s NO new mailbox name contains invalid characters\r\n",tag);
      return 1;
    }

  /* check if structure of new name is valid */
  /* i.e. only last part (after last '/' can be nonexistent) */
  for (i=strlen(args[1])-1; i>=0 && args[1][i] != '/'; i--) ;
  if (i >= 0)
    {
      args[1][i] = '\0'; /* note: original char was '/' */

      parentmboxid = db_findmailbox(args[1], ud->userid);
      if (parentmboxid == (unsigned long)-1)
	{
	  /* dbase failure */
	  fprintf(ci->tx,"* BYE internal dbase error\r\n");
	  return -1; /* fatal */
	}
      if (parentmboxid == 0)
	{
	  /* parent mailbox does not exist */
	  fprintf(ci->tx,"%s NO new mailbox would invade mailbox structure\r\n",tag);
	  return 1;
	}

      /* ok, reset arg */
      args[1][i] = '/'; 
    }

  /* check if it is INBOX to be renamed */
  if (strcasecmp(args[0],"inbox") == 0)
    {
      /* ok, renaming inbox */
      /* this means creating a new mailbox and moving all the INBOX msgs to the new mailbox */
      /* inferior names of INBOX are left unchanged */
      result = db_createmailbox(args[1], ud->userid);
      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\r\n");
	  return -1;
	}
      
      /* retrieve uid of newly created mailbox */
      newmboxid = db_findmailbox(args[1], ud->userid);
      if (newmboxid == (unsigned long)(-1))
	{
	  fprintf(ci->tx,"* BYE internal dbase error\r\n");
	  return -1;
	}

      result = db_movemsg(newmboxid, mboxid);
      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\r\n");
	  return -1;
	}
      
      /* ok done */
      fprintf(ci->tx,"%s OK RENAME completed\r\n",tag);
      return 0;
    }

  /* check for inferior names */
  result = db_listmailboxchildren(mboxid, &children, &nchildren, "%");
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1;
    }

  /* replace name for each child */
  oldnamelen = strlen(args[0]);
  for (i=0; i<nchildren; i++)
    {
      result = db_getmailboxname(children[i], name);
      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\r\n");
	  free(children);
	  return -1;
	}

      if (oldnamelen >= strlen(name))
	{
	  /* strange error, let's say its fatal */
	  trace(TRACE_ERROR,"IMAPD: rename(): mailbox names are fucked up\r\n");
	  fprintf(ci->tx,"* BYE internal error regarding mailbox names\r\n");
	  free(children);
	  return -1;
	}
	  
      snprintf(newname, IMAP_MAX_MAILBOX_NAMELEN, "%s%s",args[1],&name[oldnamelen]);

      result = db_setmailboxname(children[i], newname);
      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\r\n");
	  free(children);
	  return -1;
	}
    }
  if (children)
    free(children);

  /* now replace name */
  result = db_setmailboxname(mboxid, args[1]);
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1;
    }

  fprintf(ci->tx,"%s OK RENAME completed\r\n",tag);
  return 0;
}


/*
 * _ic_subscribe()
 *
 * adds a mailbox to the users' subscription list
 */
int _ic_subscribe(char *tag, char **args, ClientInfo *ci)
{
/*  imap_userdata_t *ud = (imap_userdata_t*)ci->userData; */

  if (!check_state_and_args("SUBSCRIBE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK SUBSCRIBE completed\r\n",tag);
  return 0;
}


/*
 * _ic_unsubscribe()
 *
 * removes a mailbox from the users' subscription list
 */
int _ic_unsubscribe(char *tag, char **args, ClientInfo *ci)
{
/*  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;*/

  if (!check_state_and_args("UNSUBSCRIBE", tag, args, 1, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK UNSUBSCRIBE completed\r\n",tag);
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
  unsigned long *children=NULL,mboxid;
  int nchildren=0,result,i,j,cnt,percpresent,starpresent;
  char name[IMAP_MAX_MAILBOX_NAMELEN];
  char *thisname = list_is_lsub ? "LSUB" : "LIST";

  if (!check_state_and_args(thisname, tag, args, 2, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  /* remove trailing '/' if exist */
  while (strlen(args[0]) > 0 && args[0][strlen(args[0]) - 1] == '/') args[0][strlen(args[0]) - 1] = '\0';
  while (strlen(args[1]) > 0 && args[1][strlen(args[1]) - 1] == '/') args[1][strlen(args[1]) - 1] = '\0';

  /* check if args are both empty strings */
  if (strlen(args[0]) == 0 && strlen(args[1]) == 0)
    {
      /* this has special meaning, show root & delimiter */
      fprintf(ci->tx,"* %s (\\NoSelect) \"/\" \"\"\r\n",thisname);
      fprintf(ci->tx,"%s OK %s completed\r\n",tag,thisname);
      return 0;
    }

  /* show first arg info */
  mboxid = db_findmailbox(args[0], ud->userid);
  if (mboxid == (unsigned long)(-1))
    {
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1;
    }
  
  if (mboxid > 0)
    {
      /* mailbox exists */
      fprintf(ci->tx,"* %s (",thisname);
      
      /* show flags */
      result = db_isselectable(mboxid);
      if (result == -1)
	{
	  fprintf(ci->tx,"\r\n* BYE internal dbase error\r\n");
	  return -1;
	}

      if (result) fprintf(ci->tx,"\\r\noselect ");

      result = db_noinferiors(mboxid);
      if (result == -1)
	{
	  fprintf(ci->tx,"\r\n* BYE internal dbase error\r\n");
	  return -1;
	}

      if (result) fprintf(ci->tx,"\\r\noinferiors ");

      result = db_getmailboxname(mboxid, name);
      if (result == -1)
	{
	  fprintf(ci->tx,"\r\n* BYE internal dbase error\r\n");
	  return -1;
	}

      /* show delimiter & name */
      fprintf(ci->tx,") \"/\" %s\r\n",name);
    }

  /* now check the children */
  /* check if % wildcards are the only ones used */
  /* replace '*' wildcard by '%' */
  percpresent = 0;
  starpresent = 0;
  for (i=0; i<strlen(args[1]); i++)
    {
      if (args[1][i] == '%')
	percpresent = 1;

      if (args[1][i] == '*') 
	{
	  args[1][i] = '%';
	  starpresent = 1;
	}
    }

  result = db_listmailboxchildren(mboxid, &children, &nchildren, args[1]);
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1;
    }

  for (i=0; i<nchildren; i++)
    {
      /* get name */
      result = db_getmailboxname(children[i], name);
      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\r\n");
	  free(children);
	  return -1;
	}

      if (percpresent && !starpresent)
	{
	  /* number of '/' should be one more in match than in base */
	  for (j=0,cnt=0; j<strlen(name); j++)
	    if (name[j] == '/') cnt++;

	  for (j=0; j<strlen(args[0]); j++)
	    if (args[0][j] == '/') cnt--;

	  if (cnt != 1)
	    {
	      continue;
	    }
	}

      fprintf(ci->tx,"* %s (",thisname);

      /* show flags */
      result = db_isselectable(children[i]);
      if (result == -1)
	{
	  fprintf(ci->tx,"\r\n* BYE internal dbase error\r\n");
	  free(children);
	  return -1;
	}

      if (!result) fprintf(ci->tx,"\\r\noselect ");

      result = db_noinferiors(children[i]);
      if (result == -1)
	{
	  fprintf(ci->tx,"\r\n* BYE internal dbase error\r\n");
	  free(children);
	  return -1;
	}

      if (result) fprintf(ci->tx,"\\r\noinferiors ");

      /* show delimiter & name */
      fprintf(ci->tx,") \"/\" %s\r\n",name);
    }

  if (children)
    free(children);

  fprintf(ci->tx,"%s OK %s completed\r\n",tag,thisname);
  return 0;
}


/*
 * _ic_lsub()
 *
 * list subscribed mailboxes
 */
int _ic_lsub(char *tag, char **args, ClientInfo *ci)
{
  int result;

  list_is_lsub = 1;
  result = _ic_list(tag,args,ci);
  list_is_lsub = 0;
  return result;
}


/*
 * _ic_status()
 *
 * inquire the status of a mailbox
 */
int _ic_status(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;
  mailbox_t mb;
  int i,endfound,result;

  if (ud->state != IMAPCS_AUTHENTICATED && ud->state != IMAPCS_SELECTED)
    {
      fprintf(ci->tx,"%s BAD STATUS command received in invalid state\r\n", tag);
      return 1;
    }

  if (!args[0] || !args[1] || !args[2])
    {
      fprintf(ci->tx,"%s BAD missing argument(s) to STATUS\r\n", tag);
      return 1;
    }

  if (strcmp(args[1],"(") != 0)
    {
      fprintf(ci->tx,"%s BAD argument list should be parenthesed\r\n", tag);
      return 1;
    }

  /* check final arg: should be ')' and no new '(' in between */
  for (i=2,endfound=0; args[i]; i++)
    {
      if (strcmp(args[i], ")") == 0)
	{
	  endfound = i;
	  break;
	}

      if (strcmp(args[i], "(") == 0)
	{
	  fprintf(ci->tx,"%s BAD too many parentheses specified\r\n", tag);
	  return 1;
	}
    }
	
  if (endfound == 2)
    {
      fprintf(ci->tx,"%s BAD argument list empty\r\n", tag);
      return 1;
    }

  if (args[endfound+1])
    {
      fprintf(ci->tx,"%s BAD argument list too long\r\n", tag);
      return 1;
    }
    
  /* check if mailbox exists */
  mb.uid = db_findmailbox(args[0], ud->userid);
  if (mb.uid == (unsigned long)(-1))
    {
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1;
    }

  if (mb.uid == 0)
    {
      /* mailbox does not exist */
      fprintf(ci->tx,"%s NO specified mailbox does not exist\r\n",tag);
      return 1;
    }

  /* retrieve mailbox data */
  i = 0;
  do
    {
      result = db_getmailbox(&mb, ud->userid);
    } while (result == 1 && i++<MAX_RETRIES);

  if (result == 1)
    {
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\r\n");
      return -1;
    }
  
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1; /* fatal  */
    }

  fprintf(ci->tx, "* STATUS %s (", args[0]);

  for (i=2; args[i]; i++)
    {
      if (strcasecmp(args[i], "messages") == 0)
	fprintf(ci->tx, "MESSAGES %d ",mb.exists);
      else if (strcasecmp(args[i], "recent") == 0)
	fprintf(ci->tx, "RECENT %d ",mb.recent);
      else if (strcasecmp(args[i], "unseen") == 0)
	fprintf(ci->tx, "UNSEEN %d ",mb.unseen);
      else if (strcasecmp(args[i], "uidnext") == 0)
	fprintf(ci->tx, "UIDNEXT %lu ",mb.msguidnext);
      else if (strcasecmp(args[i], "uidvalidity") == 0)
	fprintf(ci->tx, "UIDVALIDITY %lu ",mb.uid);
      else if (strcasecmp(args[i], ")") == 0)
	break; /* done */
      else
	{
	  fprintf(ci->tx,"\r\n%s BAD unrecognized option '%s' specified\r\n",tag,args[i]);
	  return 1;
	}
    }

  fprintf(ci->tx,")\r\n");

  fprintf(ci->tx,"%s OK STATUS completed\r\n",tag);
  return 0;
}


/*
 * _ic_append()
 *
 * append a (literal) message to a mailbox
 */
int _ic_append(char *tag, char **args, ClientInfo *ci)
{
/*  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;*/

  if (!check_state_and_args("APPEND", tag, args, 2, IMAPCS_AUTHENTICATED, ci) &&
      !check_state_and_args("APPEND", tag, args, 3, IMAPCS_AUTHENTICATED, ci) && 
      !check_state_and_args("APPEND", tag, args, 4, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK APPEND completed\r\n",tag);
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
 * (equivalent to NOOP)
 */
int _ic_check(char *tag, char **args, ClientInfo *ci)
{
  if (!check_state_and_args("CHECK", tag, args, 0, IMAPCS_SELECTED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK CHECK completed\r\n",tag);
  return 0;
}


/*
 * _ic_close()
 *
 * expunge deleted messages from selected mailbox & return to AUTH state
 * do not show expunge-output
 */ 
int _ic_close(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  if (!check_state_and_args("CLOSE", tag, args, 0, IMAPCS_SELECTED, ci))
    return 1; /* error, return */

  if (ud->mailbox.permission == IMAPPERM_READWRITE)
    db_expunge(ud->mailbox.uid, NULL, NULL);

  /* ok, update state */
  ud->state = IMAPCS_AUTHENTICATED;

  fprintf(ci->tx,"%s OK CLOSE completed\r\n",tag);
  return 0;
}


/*
 * _ic_expunge()
 *
 * expunge deleted messages from selected mailbox
 * show expunge output per message
 */
int _ic_expunge(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;
  unsigned long *msgids;
  int nmsgs,i,idx,result,j;

  if (!check_state_and_args("EXPUNGE", tag, args, 0, IMAPCS_SELECTED, ci))
    return 1; /* error, return */

  if (ud->mailbox.permission != IMAPPERM_READWRITE)
    {
      fprintf(ci->tx,"%s NO you do not have write permission on this folder\r\n",tag);
      return 1;
    }

  /* delete messages */
  result = db_expunge(ud->mailbox.uid,&msgids,&nmsgs);
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE dbase/memory error\r\n");
      return -1;
    }
  
  /* show expunge info */
  for (i=0; i<nmsgs; i++)
    {
      /* find the message sequence number */
      idx = binary_search(ud->mailbox.seq_list, ud->mailbox.exists, msgids[i]);

      fprintf(ci->tx,"* %d EXPUNGE\r\n",idx+1); /* add one: IMAP MSN starts at 1 not zero */
    }


  /* update mailbox info */
  i = 0;
  do
    {
      result = db_getmailbox(&ud->mailbox, ud->userid);
    } while (result == 1 && i++<MAX_RETRIES);

  if (result == 1)
    {
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\r\n");
      return -1;
    }
  
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1; /* fatal  */
    }


  fprintf(ci->tx,"%s OK EXPUNGE completed\r\n",tag);
  return 0;
}


/*
 * _ic_search()
 *
 * search the selected mailbox for messages
 */
int _ic_search(char *tag, char **args, ClientInfo *ci)
{
/*  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;*/

  fprintf(ci->tx,"%s OK %s completed\r\n",tag, imapcommands_use_uid ? "UID" : "SEARCH");
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
  int i,fetch_start,fetch_end,result,setseen,j,k;
  int isfirstout,idx,headers_fetched,uid_will_be_fetched;
  int partspeclen,only_text_from_msgpart = 0;
  int expand_newlines=0;
  fetch_items_t *fi,fetchitem;
  mime_message_t msg,*msgpart;
  char date[IMAP_INTERNALDATE_LEN],*endptr;
  unsigned long thisnum;
  long dumpsize,cnt;
  struct list fetch_list;
  struct element *curr;
  char tmpname[] = "fetch.tmp.XXXXXX";
  FILE *tmpfile;

  memset(&msg, 0, sizeof(msg));
  memset(&fetch_list, 0, sizeof(fetch_list));

  if (ud->state != IMAPCS_SELECTED)
    {
      fprintf(ci->tx,"%s BAD FETCH command received in invalid state\r\n", tag);
      return 1;
    }

  if (!args[0] || !args[1])
    {
      fprintf(ci->tx,"%s BAD missing argument(s) to FETCH\r\n", tag);
      return 1;
    }

  tmpfile = fdopen(mkstemp(tmpname),"r+w");
  if (tmpfile == NULL)
    {
      fprintf(ci->tx,"* BYE could not create temporary file\r\n");
      return -1; /* failed opening temporary file */
    }
  
  /* update mailbox info */
  i = 0;
  do
    {
      result = db_getmailbox(&ud->mailbox, ud->userid);
    } while (result == 1 && i++<MAX_RETRIES);

  if (result == 1)
    {
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\r\n");
      fclose(tmpfile);
      unlink(tmpname);
      return -1;
    }
  
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      fclose(tmpfile);
      unlink(tmpname);
      return -1; /* fatal  */
    }

  /* fetch fetch items */
  list_init(&fetch_list);
  idx = 1;
  uid_will_be_fetched = 0;
  do
    {
      idx = next_fetch_item(args, idx, &fetchitem);
      if (idx == -2)
	{
	  list_freelist(&fetch_list.start);
	  fprintf(ci->tx,"%s BAD invalid argument list to fetch\r\n", tag);
	  fclose(tmpfile);
	  unlink(tmpname);
	  return 1;
	}

      if (idx > 0 && !list_nodeadd(&fetch_list, &fetchitem, sizeof(fetchitem)))
	{
	  list_freelist(&fetch_list.start);
	  fprintf(ci->tx,"* BYE out of memory\r\n");
	  fclose(tmpfile);
	  unlink(tmpname);
	  return 1;
	}

      if (fetchitem.getUID)
	uid_will_be_fetched = 1;

    } while (idx > 0);

  if (!uid_will_be_fetched && imapcommands_use_uid)
    {
      /* make sure UID will be on the fetch-item list */
      memset(&fetchitem, 0, sizeof(fetchitem));
      fetchitem.getUID = 1;

      if (!list_nodeadd(&fetch_list, &fetchitem, sizeof(fetchitem)))
	{
	  list_freelist(&fetch_list.start);
	  fprintf(ci->tx,"* BYE out of memory\r\n");
	  fclose(tmpfile);
	  unlink(tmpname);
	  return 1;
	}
    }

  fetch_list.start = list_reverse(fetch_list.start);

  /* now fetch results for each msg */
  endptr = args[0];
  while (*endptr)
    {
      if (endptr != args[0])
	endptr++; /* skip delimiter */

      fetch_start = strtoul(endptr, &endptr, 10);

      if (fetch_start == 0 || fetch_start > 
	  (imapcommands_use_uid ?  (ud->mailbox.msguidnext-1) : ud->mailbox.exists))
	{
	  if (imapcommands_use_uid)
	    fprintf(ci->tx,"%s OK FETCH completed\r\n",tag);
	  else
	    fprintf(ci->tx, "%s BAD invalid message range specified\r\n",tag);

	  fclose(tmpfile);
	  unlink(tmpname);
	  list_freelist(&fetch_list.start);
	  return !imapcommands_use_uid;
	}

      switch (*endptr)
	{
	case ':':
	  fetch_end = strtoul(++endptr, &endptr, 10);

	  if (*endptr == '*')
	    {
	      fetch_end = (imapcommands_use_uid ?  
			   (ud->mailbox.msguidnext-1) : ud->mailbox.exists);
	      endptr++;
	      break;
	    }

	  if (fetch_end == 0 || fetch_end > 
	      (imapcommands_use_uid ?  (ud->mailbox.msguidnext-1) : ud->mailbox.exists))
	    {
	      if (!imapcommands_use_uid)
		{
		  fprintf(ci->tx, "%s BAD invalid message range specified\r\n",tag);

		  fclose(tmpfile);
		  unlink(tmpname);
		  list_freelist(&fetch_list.start);
		  return 1;
		}
	    }

	  if (fetch_end < fetch_start)
	    {
	      i = fetch_start;
	      fetch_start = fetch_end;
	      fetch_end = i;
	    }
	  break;

	case ',':
	case 0:
	  fetch_end = fetch_start;
	  break;
	  
	default:
	  fprintf(ci->tx, "%s BAD invalid character in message range\r\n",tag);
	  fclose(tmpfile);
	  unlink(tmpname);
	  list_freelist(&fetch_list.start);
	  return 1;
	}
      
      if (!imapcommands_use_uid)
	{
	  fetch_start--;
	  fetch_end--;
	}

      for (i=fetch_start; i<=fetch_end; i++)
	{
	  thisnum = (imapcommands_use_uid ? i : ud->mailbox.seq_list[i]);

	  if (imapcommands_use_uid)
	    {
	      if (i > ud->mailbox.msguidnext-1)
		{
		  /* passed the last one */
		  fprintf(ci->tx,"%s OK FETCH completed\r\n",tag);
		  fclose(tmpfile);
		  unlink(tmpname);
		  list_freelist(&fetch_list.start);
		  return 0;
		}

	      /* check if the message with this UID belongs to this mailbox */
	      if (!db_mailbox_msg_match(ud->mailbox.uid, thisnum))
		continue;

	      fprintf(ci->tx,"* %d FETCH (", 
		      binary_search(ud->mailbox.seq_list, ud->mailbox.exists, i)+1);
	    }
	  else
	    fprintf(ci->tx,"* %d FETCH (",i+1);

	  trace(TRACE_DEBUG, "Fetching msgID %lu (fetch num %d)\r\n", thisnum, i+1);

	  curr = list_getstart(&fetch_list);
	  headers_fetched = 0;
	  setseen = 0;

	  while (curr)
	    {
	      fi = (fetch_items_t*)curr->data;
	      rewind(tmpfile);

	      if (!headers_fetched && fi->msgparse_needed)
		{
		  /* parse message structure */
		  if (db_fetch_headers(thisnum, &msg) == -1)
		    {
		      fprintf(ci->tx,"* BAD error fetching message %d\r\n",i+1);
		      curr = curr->nextnode;
		      continue;
		    }
		  db_msgdump(&msg, thisnum);
		  headers_fetched = 1;
		}

	      if (fi->getInternalDate)
		{
		  result = db_get_msgdate(ud->mailbox.uid, thisnum, date);
		  if (result == -1)
		    {
		      fprintf(ci->tx,"* BYE internal dbase error\r\n");
		      db_free_msg(&msg);
		      list_freelist(&fetch_list.start);
		      fclose(tmpfile);
		      unlink(tmpname);
		      return -1;
		    }

		  fprintf(ci->tx,"INTERNALDATE \"%s\" ", date_sql2imap(date));
		}

	      if (fi->getUID)
		fprintf(ci->tx,"UID %lu ",thisnum);
	      
	      if (fi->getMIME_IMB)
		{
		  fprintf(ci->tx,"BODYSTRUCTURE ");
		  result = retrieve_structure(ci->tx, &msg, 1);
		  fprintf(ci->tx," ");
		  if (result == -1)
		    {
		      fprintf(ci->tx,"* BYE error fetching body structure\r\n");
		      db_free_msg(&msg);
		      list_freelist(&fetch_list.start);
		      fclose(tmpfile);
		      unlink(tmpname);
		      return -1;
		    }
		}

	      if (fi->getMIME_IMB_noextension)
		{
		  fprintf(ci->tx,"BODY ");
		  result = retrieve_structure(ci->tx, &msg, 0);
		  fprintf(ci->tx," ");
		  if (result == -1)
		    {
		      fprintf(ci->tx,"* BYE error fetching body\r\n");
		      db_free_msg(&msg);
		      list_freelist(&fetch_list.start);
		      fclose(tmpfile);
		      unlink(tmpname);
		      return -1;
		    }
		}

	      if (fi->getEnvelope)
		{
		  fprintf(ci->tx,"ENVELOPE ");
		  result = retrieve_envelope(ci->tx, &msg.rfcheader);
		  if (result == -1)
		    {
		      fprintf(ci->tx,"* BYE error fetching envelope structure\r\n");
		      db_free_msg(&msg);
		      list_freelist(&fetch_list.start);
		      fclose(tmpfile);
		      unlink(tmpname);
		      return -1;
		    }
		}

	      if (fi->getRFC822)
		{
		  expand_newlines =
		    (is_textplain(&msg.rfcheader) || is_textplain(&msg.mimeheader));

		  dumpsize  = rfcheader_dump(tmpfile, &msg.rfcheader, args, 0, 0);
		  dumpsize += db_dump_range(tmpfile, msg.bodystart, msg.bodyend, thisnum,
					    expand_newlines);
		  
		  fseek(tmpfile, 0, SEEK_SET);

		  fprintf(ci->tx, "RFC822 {%ld}\r\n", dumpsize);
		  while (dumpsize--)
		    fputc(fgetc(tmpfile), ci->tx);

		  setseen = 1;
		}
		  
	      if (fi->getRFC822Peek)
		{
		  expand_newlines =
		    (is_textplain(&msg.rfcheader) || is_textplain(&msg.mimeheader));

		  dumpsize  = rfcheader_dump(tmpfile, &msg.rfcheader, args, 0, 0);
		  dumpsize += db_dump_range(tmpfile, msg.bodystart, msg.bodyend, thisnum,
					    expand_newlines);
		  
		  fseek(tmpfile, 0, SEEK_SET);

		  fprintf(ci->tx, "RFC822 {%ld}\r\n", dumpsize);
		  while (dumpsize--)
		    fputc(fgetc(tmpfile), ci->tx);
		}
		  
	      if (fi->getSize)
		{
		  /* add 2 for an extra \r\n */
		  fprintf(ci->tx,"RFC822.SIZE %lu ", msg.rfcheadersize + msg.bodysize + 
			  + msg.bodylines + 2);
		}

	      if (fi->getBodyTotal)
		{
		  expand_newlines =
		    (is_textplain(&msg.rfcheader) || is_textplain(&msg.mimeheader));

		  dumpsize  = rfcheader_dump(tmpfile, &msg.rfcheader, args, 0, 0);
		  dumpsize += db_dump_range(tmpfile, msg.bodystart, msg.bodyend, thisnum,
					    expand_newlines);
		  
		  fseek(tmpfile, 0, SEEK_SET);

		  fprintf(ci->tx, "BODY[] {%ld}\r\n", dumpsize);
		  while (dumpsize--)
		    fputc(fgetc(tmpfile), ci->tx);

		  setseen = 1;
		}

	      if (fi->getBodyTotalPeek)
		{
		  expand_newlines =
		    (is_textplain(&msg.rfcheader) || is_textplain(&msg.mimeheader));

		  dumpsize  = rfcheader_dump(tmpfile, &msg.rfcheader, args, 0, 0);
		  dumpsize += db_dump_range(tmpfile, msg.bodystart, msg.bodyend, thisnum,
					    expand_newlines);
		  
		  fseek(tmpfile, 0, SEEK_SET);

		  fprintf(ci->tx, "BODY[] {%ld}\r\n", dumpsize);
		  while (dumpsize--)
		    fputc(fgetc(tmpfile), ci->tx);
		}

	      if (fi->getRFC822Header)
		{
		  dumpsize = rfcheader_dump(tmpfile, &msg.rfcheader, args, 0, 0);

		  fseek(tmpfile, 0, SEEK_SET);

		  fprintf(ci->tx, "RFC822.HEADER {%ld}\r\n",dumpsize);
		  while (dumpsize--)
		    fputc(fgetc(tmpfile), ci->tx);
		}
	      
	      if (fi->getRFC822Text)
		{
		  expand_newlines =
		    (is_textplain(&msg.rfcheader) || is_textplain(&msg.mimeheader));

		  dumpsize = db_dump_range(tmpfile, msg.bodystart, msg.bodyend, thisnum,
					    expand_newlines);

		  fprintf(ci->tx, "RFC822.TEXT {%ld}\r\n",dumpsize);
		  while (dumpsize--)
		    fputc(fgetc(tmpfile), ci->tx);

		  setseen = 1;
		}

	      if (fi->bodyfetch.itemtype >= 0)
		{
		  if (fi->bodyfetch.partspec[0])
		    {
		      msgpart = get_part_by_num(&msg, fi->bodyfetch.partspec);

		      if (!msgpart)
			{
			  /* if the partspec ends on "1" or "1." the msg body
			   * of the parent message is to be retrieved
			   */
			  
			  partspeclen = strlen(fi->bodyfetch.partspec);
			  
			  if ((fi->bodyfetch.partspec[partspeclen-1] == '1'  &&
			       (partspeclen == 1 || fi->bodyfetch.partspec[partspeclen-2] == '.'))
			      ||
			      ((fi->bodyfetch.partspec[partspeclen-1] == '.' &&
			       fi->bodyfetch.partspec[partspeclen-2] == '1') &&
			       (partspeclen == 2 || fi->bodyfetch.partspec[partspeclen-3] == '.'))
			      )
			    {
			      /* ok find the parent of this message */
			      /* start value of k is partspeclen-2 'cause we could
				 have partspec[partspeclen-1] == '.' right at the start
			      */

			      for (k = partspeclen-2; k>=0; k--)
				if (fi->bodyfetch.partspec[k] == '.')
				  break;

			      if (k>0)
				{
				  fi->bodyfetch.partspec[k] = '\0';
				  msgpart = get_part_by_num(&msg, fi->bodyfetch.partspec);
				  fi->bodyfetch.partspec[k] = '.';
				}
			      else
				msgpart = &msg;

			      only_text_from_msgpart = 1;
			    }
			}
		      else
			{
			  only_text_from_msgpart = 0;
			}
		    }
		  else
		    msgpart = &msg;

		  if (fi->bodyfetch.noseen)
		    fprintf(ci->tx, "BODY[%s", fi->bodyfetch.partspec);
		  else
		    {
		      fprintf(ci->tx, "BODY[%s", fi->bodyfetch.partspec);
		      setseen = 1;
		    }
			  
		  switch (fi->bodyfetch.itemtype)
		    {
		    case BFIT_TEXT:
		      fprintf(ci->tx, "TEXT");

		    case BFIT_TEXT_SILENT:
		      /* dump body text */
		      if (!msgpart)
			fprintf(ci->tx, "] NIL ");
		      else
			{
			  /* check if newlines should be expanded to CRLF
			   * (only for text/plain)
			   */
			  expand_newlines =
			    (is_textplain(&msgpart->rfcheader) || is_textplain(&msgpart->mimeheader));
			    
			      
			  dumpsize = db_dump_range(tmpfile, msgpart->bodystart, msgpart->bodyend,
						   thisnum, expand_newlines);

			  if (fi->bodyfetch.octetstart >= 0)
			    {
			      cnt = dumpsize - fi->bodyfetch.octetstart;
			      if (cnt<0) cnt = 0;
			      if (cnt > fi->bodyfetch.octetcnt) cnt = fi->bodyfetch.octetcnt;
 
			      fprintf(ci->tx, "]<%lu> {%ld}\r\n",
				      fi->bodyfetch.octetstart, cnt);
			      
			      fseek(tmpfile, fi->bodyfetch.octetstart, SEEK_SET);
			    }
			  else
			    {
			      cnt = dumpsize;
			      fprintf(ci->tx, "] {%ld}\r\n", dumpsize);
			      fseek(tmpfile, 0, SEEK_SET);
			    }

			  /* output data */
			  while (cnt--)
			    fputc(fgetc(tmpfile), ci->tx);

			}
		      break;

		    case BFIT_HEADER:
		      fprintf(ci->tx, "HEADER] ");
		      if (!msgpart || only_text_from_msgpart)
			fprintf(ci->tx, "NIL\r\n");
		      else
			{
			  dumpsize = rfcheader_dump(tmpfile, &msgpart->rfcheader, 
					 args, 0, 0);

			  if (fi->bodyfetch.octetstart >= 0)
			    {
			      cnt = dumpsize - fi->bodyfetch.octetstart;
			      if (cnt<0) cnt = 0;
			      if (cnt > fi->bodyfetch.octetcnt) cnt = fi->bodyfetch.octetcnt;
 
			      fprintf(ci->tx, "]<%lu> {%ld}\r\n",
				      fi->bodyfetch.octetstart, cnt);
			      
			      fseek(tmpfile, fi->bodyfetch.octetstart, SEEK_SET);
			    }
			  else
			    {
			      cnt = dumpsize;
			      fprintf(ci->tx, "] {%ld}\r\n", dumpsize);
			      fseek(tmpfile, 0, SEEK_SET);
			    }

			  /* output data */
			  while (cnt--)
			    fputc(fgetc(tmpfile), ci->tx);

			}
		      break;

		    case BFIT_HEADER_FIELDS:
		      fprintf(ci->tx, "HEADER.FIELDS (");

		      isfirstout = 1;
		      for (k=0; k<fi->bodyfetch.argcnt; k++)
			{
			  if (isfirstout)
			    {
			      fprintf(ci->tx, "%s",args[k + fi->bodyfetch.argstart]);
			      isfirstout = 0;
			    }
			  else
			    fprintf(ci->tx, " %s",args[k + fi->bodyfetch.argstart]);
			}

		      fprintf(ci->tx,")] ");

		      if (!msgpart || only_text_from_msgpart)
			fprintf(ci->tx, "NIL\r\n");
		      else
			{
			  dumpsize = rfcheader_dump(tmpfile, &msgpart->rfcheader, 
						    &args[fi->bodyfetch.argstart],
						    fi->bodyfetch.argcnt, 1);
						    
			  if (fi->bodyfetch.octetstart >= 0)
			    {
			      cnt = dumpsize - fi->bodyfetch.octetstart;
			      if (cnt<0) cnt = 0;
			      if (cnt > fi->bodyfetch.octetcnt) cnt = fi->bodyfetch.octetcnt;
 
			      fprintf(ci->tx, "]<%lu> {%ld}\r\n",
				      fi->bodyfetch.octetstart, cnt);
			      
			      fseek(tmpfile, fi->bodyfetch.octetstart, SEEK_SET);
			    }
			  else
			    {
			      cnt = dumpsize;
			      fprintf(ci->tx, "] {%ld}\r\n", dumpsize);
			      fseek(tmpfile, 0, SEEK_SET);
			    }

			  /* output data */
			  while (cnt--)
			    fputc(fgetc(tmpfile), ci->tx);

			}
		      break;
		    case BFIT_HEADER_FIELDS_NOT:
		      fprintf(ci->tx, "HEADER.FIELDS.NOT (");

		      isfirstout = 1;
		      for (k=0; k<fi->bodyfetch.argcnt; k++)
			{
			  if (isfirstout)
			    {
			      fprintf(ci->tx, "%s",args[k + fi->bodyfetch.argstart]);
			      isfirstout = 0;
			    }
			  else
			    fprintf(ci->tx, " %s",args[k + fi->bodyfetch.argstart]);
			}

		      fprintf(ci->tx,")] ");

		      if (!msgpart || only_text_from_msgpart)
			fprintf(ci->tx, "NIL\r\n");
		      else
			{
			  dumpsize = rfcheader_dump(tmpfile, &msgpart->rfcheader, 
						    &args[fi->bodyfetch.argstart],
						    fi->bodyfetch.argcnt, 0);
						    
			  if (fi->bodyfetch.octetstart >= 0)
			    {
			      cnt = dumpsize - fi->bodyfetch.octetstart;
			      if (cnt<0) cnt = 0;
			      if (cnt > fi->bodyfetch.octetcnt) cnt = fi->bodyfetch.octetcnt;
 
			      fprintf(ci->tx, "]<%lu> {%ld}\r\n",
				      fi->bodyfetch.octetstart, cnt);
			      
			      fseek(tmpfile, fi->bodyfetch.octetstart, SEEK_SET);
			    }
			  else
			    {
			      cnt = dumpsize;
			      fprintf(ci->tx, "] {%ld}\r\n", dumpsize);
			      fseek(tmpfile, 0, SEEK_SET);
			    }

			  /* output data */
			  while (cnt--)
			    fputc(fgetc(tmpfile), ci->tx);
			}
		      break;
		    case BFIT_MIME:
		      fprintf(ci->tx, "MIME] \r\n");

		      if (!msgpart)
			fprintf(ci->tx, "NIL\r\n");
		      else
			{
			  dumpsize = mimeheader_dump(tmpfile, &msgpart->mimeheader);

			  if (fi->bodyfetch.octetstart >= 0)
			    {
			      cnt = dumpsize - fi->bodyfetch.octetstart;
			      if (cnt<0) cnt = 0;
			      if (cnt > fi->bodyfetch.octetcnt) cnt = fi->bodyfetch.octetcnt;
 
			      fprintf(ci->tx, "]<%lu> {%ld}\r\n",
				      fi->bodyfetch.octetstart, cnt);
			      
			      fseek(tmpfile, fi->bodyfetch.octetstart, SEEK_SET);
			    }
			  else
			    {
			      cnt = dumpsize;
			      fprintf(ci->tx, "] {%ld}\r\n", dumpsize);
			      fseek(tmpfile, 0, SEEK_SET);
			    }

			  /* output data */
			  while (cnt--)
			    fputc(fgetc(tmpfile), ci->tx);

			}
		  
		      break;
		    default:
		      fprintf(ci->tx, "* BYE internal server error\r\n");
		      list_freelist(&fetch_list.start);
		      db_free_msg(&msg);
		      fclose(tmpfile);
		      unlink(tmpname);
		      return -1;
		    }
		}


	      /* set \Seen flag if necessary; note the absence of an error-check 
	       * for db_get_msgflag()!
	       */
	      if (setseen && db_get_msgflag("seen", ud->mailbox.uid, thisnum) != 1)
		{
		  result = db_set_msgflag("seen", ud->mailbox.uid, thisnum, 1);
		  if (result == -1)
		    {
		      fprintf(ci->tx,"* BYE internal dbase error\r\n");
		      db_free_msg(&msg);
		      list_freelist(&fetch_list.start);
		      fclose(tmpfile);
		      unlink(tmpname);
		      return -1;
		    }

		  fi->getFlags = 1;
		}

	      /* FLAGS ? */
	      if (fi->getFlags) 
		{
		  fprintf(ci->tx,"FLAGS (");

		  isfirstout = 1;
		  for (j=0; j<IMAP_NFLAGS; j++)
		    {
		      result = db_get_msgflag(imap_flag_desc[j], ud->mailbox.uid, 
					      thisnum);

		      if (result == -1)
			{
			  fprintf(ci->tx,"* BYE internal dbase error\r\n");
			  list_freelist(&fetch_list.start);
			  db_free_msg(&msg);
			  fclose(tmpfile);
			  unlink(tmpname);
			  return -1;
			}
		      else if (result == 1)
			{
			  if (isfirstout)
			    {
			      fprintf(ci->tx,"\\%s",imap_flag_desc[j]);
			      isfirstout = 0;
			    }
			  else
			    fprintf(ci->tx," \\%s",imap_flag_desc[j]);
			}

		    }
		  fprintf(ci->tx,") ");
		}

	      curr = curr->nextnode;
	    }

	  fprintf(ci->tx,")\r\n");

	  if (headers_fetched)
	    db_free_msg(&msg);
	}
    }
      
  list_freelist(&fetch_list.start);
  fclose(tmpfile);
  unlink(tmpname);

  fprintf(ci->tx,"%s OK FETCH completed\r\n",tag);
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
  char *endptr;
  int i,store_start,store_end,result,j;
  int be_silent=0,action=IMAPFA_NONE;
  int flaglist[IMAP_NFLAGS];
  unsigned long thisnum;

  memset(flaglist, 0, sizeof(int) * IMAP_NFLAGS);

  if (ud->state != IMAPCS_SELECTED)
    {
      fprintf(ci->tx,"%s BAD STORE command received in invalid state\r\n", tag);
      return 1;
    }

  if (!args[0] || !args[1] || !args[2])
    {
      fprintf(ci->tx,"%s BAD missing argument(s) to STORE\r\n", tag);
      return 1;
    }

  if (strcmp(args[2],"(") != 0)
    {
      fprintf(ci->tx,"%s BAD invalid argument(s) to STORE\r\n", tag);
      return 1;
    }


  /* update mailbox info */
  i = 0;
  do
    {
      result = db_getmailbox(&ud->mailbox, ud->userid);
    } while (result == 1 && i++<MAX_RETRIES);

  if (result == 1)
    {
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\r\n");
      return -1;
    }
  
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1; /* fatal  */
    }

  /* retrieve action type */

  if (strcasecmp(args[1], "flags") == 0)
    action = IMAPFA_REPLACE;
  else if (strcasecmp(args[1], "flags.silent") == 0)
    {
      action = IMAPFA_REPLACE;
      be_silent = 1;
    }
  else if (strcasecmp(args[1], "+flags") == 0)
    action = IMAPFA_ADD;
  else if (strcasecmp(args[1], "+flags.silent") == 0)
    {
      action = IMAPFA_ADD;
      be_silent = 1;
    }
  else if (strcasecmp(args[1], "-flags") == 0)
    action = IMAPFA_REMOVE;
  else if (strcasecmp(args[1], "-flags.silent") == 0)
    {
      action = IMAPFA_REMOVE;
      be_silent = 1;
    }

  if (action == IMAPFA_NONE)
    {
      fprintf(ci->tx,"%s BAD invalid STORE action specified\r\n",tag);
      return 1;
    }

  /* now fetch flag list */
  /* remember: args[2] == "(" */
  for (i=3; args[i] && strcmp(args[i] ,")") != 0; i++)
    {
      for (j=0; j<IMAP_NFLAGS; j++)
	if (strcasecmp(args[i],imap_flag_desc_escaped[j]) == 0)
	  {
	    flaglist[j] = 1;
	    break;
	  }
      
      if (j == IMAP_NFLAGS)
	{
	  fprintf(ci->tx,"%s BAD invalid flag list to STORE command\r\n",tag);
	  return 1;
	}
    }

  /* set flags & show if needed */
  endptr = args[0];
  while (*endptr)
    {
      if (endptr != args[0])
	endptr++; /* skip delimiter */

      store_start = strtoul(endptr, &endptr, 10);

      if (store_start == 0 || store_start > 
	  (imapcommands_use_uid ? (ud->mailbox.msguidnext-1) : ud->mailbox.exists))
	{
	  fprintf(ci->tx, "%s BAD invalid message range specified\r\n",tag);
	  return 1;
	}

      switch (*endptr)
	{
	case ':':
	  store_end = strtoul(++endptr, &endptr, 10);

	  if (*endptr == '*')
	    {
	      store_end = (imapcommands_use_uid ?  
			   (ud->mailbox.msguidnext-1) : ud->mailbox.exists);
	      endptr++;
	      break;
	    }

	  if (store_end == 0 || store_end > 
	      (imapcommands_use_uid ? (ud->mailbox.msguidnext-1) : ud->mailbox.exists))
	    {
	      fprintf(ci->tx, "%s BAD invalid message range specified\r\n",tag);
	      return 1;
	    }

	  if (store_end < store_start)
	    {
	      i = store_start;
	      store_start = store_end;
	      store_end = i;
	    }
	  break;

	case ',':
	case 0:
	  store_end = store_start;
	  break;
	  
	default:
	  fprintf(ci->tx, "%s BAD invalid character in message range\r\n",tag);
	  return 1;
	}
      
      if (!imapcommands_use_uid)
	{
	  store_start--;
	  store_end--;
	}

      for (i=store_start; i<=store_end; i++)
	{
	  thisnum = (imapcommands_use_uid ? i : ud->mailbox.seq_list[i]);

	  if (imapcommands_use_uid)
	    {
	      /* check if the message with this UID belongs to this mailbox */
	      if (!db_mailbox_msg_match(ud->mailbox.uid, thisnum))
		continue;

	      if (!be_silent)
		fprintf(ci->tx,"* %d FETCH (FLAGS (", 
			binary_search(ud->mailbox.seq_list, ud->mailbox.exists, i)+1);
	    }
	  else if (!be_silent)
	      fprintf(ci->tx,"* %d FETCH (FLAGS (",i+1);

	  switch (action)
	    {
	    case IMAPFA_REPLACE:
	      for (j=0; j<IMAP_NFLAGS; j++)
		{
		  result = db_set_msgflag(imap_flag_desc[j], ud->mailbox.uid, 
					  thisnum,
					  flaglist[j]);

		  if (result == -1)
		    {
		      fprintf(ci->tx,"\r\n* BYE internal dbase error\r\n");
		      return -1;
		    }

		  if (!be_silent && flaglist[j])
		    fprintf(ci->tx,"%s ",imap_flag_desc_escaped[j]);
		}
	      break;

	    case IMAPFA_ADD:
	    case IMAPFA_REMOVE:
	      for (j=0; j<IMAP_NFLAGS; j++)
		{
		  if (flaglist[j])
		    {
		      result = db_set_msgflag(imap_flag_desc[j], ud->mailbox.uid, 
					      thisnum,
					      (action==IMAPFA_ADD));

		      if (result == -1)
			{
			  fprintf(ci->tx,"\r\n* BYE internal dbase error\r\n");
			  return -1;
			}
		    }
		  if (!be_silent)
		    {
		      result = db_get_msgflag(imap_flag_desc[j], ud->mailbox.uid, 
					      thisnum);

		      if (result == -1)
			{
			  fprintf(ci->tx,"\r\n* BYE internal dbase error\r\n");
			  return -1;
			}
		      if (result == 1)
			fprintf(ci->tx,"%s ",imap_flag_desc_escaped[j]);
		    }
		}
	      break;
	    }

	  if (!be_silent)
	    fprintf(ci->tx,"))\r\n");
	}
    }
  fprintf(ci->tx,"%s OK STORE completed\r\n",tag);
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
  int i,copy_start,copy_end,result;
  unsigned long destmboxid,thisnum;
  char *endptr;

  if (!check_state_and_args("COPY", tag, args, 2, IMAPCS_SELECTED, ci))
    return 1; /* error, return */

  /* update mailbox info */
  i = 0;
  do
    {
      result = db_getmailbox(&ud->mailbox, ud->userid);
    } while (result == 1 && i++<MAX_RETRIES);

  if (result == 1)
    {
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\r\n");
      return -1;
    }
  
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\r\n");
      return -1; /* fatal  */
    }

  /* check if destination mailbox exists */
  destmboxid = db_findmailbox(args[1], ud->userid);
  if (destmboxid == 0)
    {
      /* error: cannot select mailbox */
      fprintf(ci->tx, "%s NO [TRYCREATE] specified mailbox does not exist\r\n",tag);
      return 1;
    }
  if (destmboxid == (unsigned long)(-1))
    {
      fprintf(ci->tx, "* BYE internal dbase error\r\n");
      return -1; /* fatal */
    }

  /* ok copy msgs */
  endptr = args[0];
  while (*endptr)
    {
      if (endptr != args[0])
	endptr++; /* skip delimiter */

      copy_start = strtoul(endptr, &endptr, 10);

      if (copy_start == 0 || copy_start >
	  (imapcommands_use_uid ? (ud->mailbox.msguidnext-1) : ud->mailbox.exists))
	{
	  fprintf(ci->tx, "%s BAD invalid message range specified\r\n",tag);
	  return 1;
	}

      switch (*endptr)
	{
	case ':':
	  copy_end = strtoul(++endptr, &endptr, 10);

	  if (*endptr == '*')
	    {
	      copy_end = (imapcommands_use_uid ?  
			  (ud->mailbox.msguidnext-1) : ud->mailbox.exists);
	      endptr++;
	      break;
	    }

	  if (copy_end == 0 || copy_end >
	      (imapcommands_use_uid ? (ud->mailbox.msguidnext-1) : ud->mailbox.exists))
	    {
	      fprintf(ci->tx, "%s BAD invalid message range specified\r\n",tag);
	      return 1;
	    }

	  if (copy_end < copy_start)
	    {
	      i = copy_start;
	      copy_start = copy_end;
	      copy_end = i;
	    }
	  break;

	case ',':
	case 0:
	  copy_end = copy_start;
	  break;
	  
	default:
	  fprintf(ci->tx, "%s BAD invalid character in message range\r\n",tag);
	  return 1;
	}
      
      if (!imapcommands_use_uid)
	{
	  copy_start--;
	  copy_end--;
	}

      for (i=copy_start; i<=copy_end; i++)
	{
	  thisnum = (imapcommands_use_uid ? i : ud->mailbox.seq_list[i]);

	  if (imapcommands_use_uid)
	    {
	      /* check if the message with this UID belongs to this mailbox */
	      if (!db_mailbox_msg_match(ud->mailbox.uid, thisnum))
		continue;

	    }

	  result = db_copymsg(thisnum, destmboxid);
	  if (result == -1)
	    {
	      fprintf(ci->tx,"* BYE internal dbase error\r\n");
	      return -1;
	    }
	}
    }

  fprintf(ci->tx,"%s OK COPY completed\r\n", tag);
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
  int result;

  if (ud->state != IMAPCS_SELECTED)
    {
      fprintf(ci->tx,"%s BAD UID command received in invalid state\r\n",tag);
      return 1;
    }
  
  if (!args[0])
    {
      fprintf(ci->tx,"%s BAD missing argument(s) to UID\r\n",tag);
      return 1;
    }

  imapcommands_use_uid = 1; /* set global var to make clear we will be using UID's */

  if (strcasecmp(args[0], "fetch") == 0)
    result = _ic_fetch(tag, &args[1], ci);
  else if (strcasecmp(args[0], "copy") == 0)
    result = _ic_copy(tag, &args[1], ci);
  else if (strcasecmp(args[0], "store") == 0)
    result = _ic_store(tag, &args[1], ci);
  else if (strcasecmp(args[0], "search") == 0)
    result = _ic_search(tag, &args[1], ci);
  else
    {
      fprintf(ci->tx,"%s BAD invalid UID command\r\n",tag);
      result = 1;
    }
    
  imapcommands_use_uid = 0; /* set global var to make clear we will be using UID's */

  return result;
}






