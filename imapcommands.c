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
  unsigned long mboxid,key;
  int result,i,idx;
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

  /* check if mailbox is selectable */
  result = db_isselectable(mboxid);
  if (result == 0)
    {
      /* error: cannot select mailbox */
      fprintf(ci->tx, "%s NO specified mailbox is not selectable\n",tag);
      return 1;
    }
  if (result == -1)
    {
      fprintf(ci->tx, "* BYE internal dbase error\n");
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
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\n");
      return -1;
    }

  if (result == -1)
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

  /* show idx of first unseen msg (if present) */
  key = db_first_unseen(ud->mailbox.uid);
  if (key == (unsigned long)(-1))
    {
      fprintf(ci->tx, "* BYE internal dbase error\n");
      return -1;
    }
  idx = binary_search(ud->mailbox.seq_list, ud->mailbox.exists, key);

  if (idx >= 0)
    fprintf(ci->tx,"* OK [UNSEEN %d] first unseen message\n",idx+1);

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
  int result,i;

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

  /* check if mailbox is selectable */
  result = db_isselectable(mboxid);
  if (result == 0)
    {
      /* error: cannot select mailbox */
      fprintf(ci->tx, "%s NO specified mailbox is not selectable\n",tag);
      return 1; /* fatal */
    }
  if (result == -1)
    {
      fprintf(ci->tx, "* BYE internal dbase error\n");
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
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\n");
      return -1;
    }

  if (result == -1)
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
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1; /* fatal */
    }

  if (mboxid != 0)
    {
      /* mailbox already exists */
      fprintf(ci->tx,"%s NO mailbox already exists\n",tag);
      return 1;
    }

  /* check if new name is valid */
  if (!checkmailboxname(args[0]))
    {
      fprintf(ci->tx,"%s NO new mailbox name contains invalid characters\n",tag);
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
      mboxid = db_findmailbox(cpy, ud->userid);
      
      if (mboxid == (unsigned long)(-1))
	{
	  /* dbase failure */
	  fprintf(ci->tx,"* BYE internal dbase error\n");
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
	      fprintf(ci->tx,"* BYE internal dbase error\n");
	      return -1; /* fatal */
	    }
	}
      else
	{
	  /* mailbox does exist, failure if no_inferiors flag set */
	  result = db_noinferiors(mboxid);
	  if (result == 1)
	    {
	      fprintf(ci->tx, "%s NO mailbox cannot have inferior names\n",tag);
	      free_chunks(chunks);
	      free(cpy);
	      return 1;
	    }

	  if (result == -1)
	    {
	      /* dbase failure */
	      fprintf(ci->tx,"* BYE internal dbase error\n");
	      free_chunks(chunks);
	      free(cpy);
	      return -1; /* fatal */
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
  while (strlen(args[0]) > 0 && args[0][strlen(args[0]) - 1] == '/')
    args[0][strlen(args[0]) - 1] = '\0';

  /* check if this mailbox exists */
  mboxid = db_findmailbox(args[0], ud->userid);
  if (mboxid == (unsigned long)(-1))
    {
      /* dbase failure */
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1; /* fatal */
    }
  if (mboxid == 0)
    {
      /* mailbox does not exist */
      fprintf(ci->tx,"%s NO mailbox does not exist\n",tag);
      return 1;
    }

  /* check if there is an attempt to delete inbox */
  if (strcasecmp(args[0],"inbox") == 0)
    {
      fprintf(ci->tx,"%s NO cannot delete special mailbox INBOX\n",tag);
      return 1;
    }

  /* check for children of this mailbox */
  result = db_listmailboxchildren(mboxid, &children, &nchildren, "%");
  if (result == -1)
    {
      /* error */
      trace(TRACE_ERROR, "IMAPD: delete(): cannot retrieve list of mailbox children\n");
      fprintf(ci->tx, "%s BYE dbase/memory error\n",tag);
      return -1;
    }

  if (nchildren != 0)
    {
      /* mailbox has inferior names; error if \Noselect specified */
      result = db_isselectable(mboxid);
      if (result == 0)
	{
	  fprintf(ci->tx,"%s NO mailbox is non-selectable\n",tag);
	  free(children);
	  return 1;
	}
      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\n");
	  free(children);
	  return -1; /* fatal */
	}

      /* mailbox has inferior names; remove all msgs and set noselect flag */
      result = db_removemsg(mboxid);
      if (result != -1)
	result = db_setselectable(mboxid, 0); /* set non-selectable flag */

      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\n");
	  free(children);
	  return -1; /* fatal */
	}

      /* ok done */
      fprintf(ci->tx,"%s OK DELETE completed\n",tag);
      free(children);
      return 0;
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
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1; /* fatal */
    }
  if (mboxid != 0)
    {
      /* mailbox exists */
      fprintf(ci->tx,"%s NO new mailbox already exists\n",tag);
      return 1;
    }

  /* check if original mailbox exists */
  mboxid = db_findmailbox(args[0], ud->userid);
  if (mboxid == (unsigned long)-1)
    {
      /* dbase failure */
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1; /* fatal */
    }
  if (mboxid == 0)
    {
      /* mailbox does not exist */
      fprintf(ci->tx,"%s NO mailbox does not exist\n",tag);
      return 1;
    }

  /* check if new name is valid */
  if (!checkmailboxname(args[1]))
    {
      fprintf(ci->tx,"%s NO new mailbox name contains invalid characters\n",tag);
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
	  fprintf(ci->tx,"* BYE internal dbase error\n");
	  return -1; /* fatal */
	}
      if (parentmboxid == 0)
	{
	  /* parent mailbox does not exist */
	  fprintf(ci->tx,"%s NO new mailbox would invade mailbox structure\n",tag);
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
	  fprintf(ci->tx,"* BYE internal dbase error\n");
	  return -1;
	}
      
      /* retrieve uid of newly created mailbox */
      newmboxid = db_findmailbox(args[1], ud->userid);
      if (newmboxid == (unsigned long)(-1))
	{
	  fprintf(ci->tx,"* BYE internal dbase error\n");
	  return -1;
	}

      result = db_movemsg(newmboxid, mboxid);
      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\n");
	  return -1;
	}
      
      /* ok done */
      fprintf(ci->tx,"%s OK RENAME completed\n",tag);
      return 0;
    }

  /* check for inferior names */
  result = db_listmailboxchildren(mboxid, &children, &nchildren, "%");
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1;
    }

  /* replace name for each child */
  oldnamelen = strlen(args[0]);
  for (i=0; i<nchildren; i++)
    {
      result = db_getmailboxname(children[i], name);
      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\n");
	  free(children);
	  return -1;
	}

      if (oldnamelen >= strlen(name))
	{
	  /* strange error, let's say its fatal */
	  trace(TRACE_ERROR,"IMAPD: rename(): mailbox names are fucked up\n");
	  fprintf(ci->tx,"* BYE internal error regarding mailbox names\n");
	  free(children);
	  return -1;
	}
	  
      snprintf(newname, IMAP_MAX_MAILBOX_NAMELEN, "%s%s",args[1],&name[oldnamelen]);

      result = db_setmailboxname(children[i], newname);
      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\n");
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
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1;
    }

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
/*  imap_userdata_t *ud = (imap_userdata_t*)ci->userData; */

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
/*  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;*/

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
  unsigned long *children=NULL,mboxid;
  int nchildren=0,result,i,j,cnt,percpresent,starpresent;
  char name[IMAP_MAX_MAILBOX_NAMELEN];

  if (!check_state_and_args("LIST", tag, args, 2, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  /* remove trailing '/' if exist */
  while (strlen(args[0]) > 0 && args[0][strlen(args[0]) - 1] == '/') args[0][strlen(args[0]) - 1] = '\0';
  while (strlen(args[1]) > 0 && args[1][strlen(args[1]) - 1] == '/') args[1][strlen(args[1]) - 1] = '\0';

  /* check if args are both empty strings */
  if (strlen(args[0]) == 0 && strlen(args[1]) == 0)
    {
      /* this has special meaning, show root & delimiter */
      fprintf(ci->tx,"* LIST (\\Noselect) \"/\" \"\"\n");
      fprintf(ci->tx,"%s OK LIST completed\n",tag);
      return 0;
    }

  /* show first arg info */
  mboxid = db_findmailbox(args[0], ud->userid);
  if (mboxid == (unsigned long)(-1))
    {
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1;
    }
  
  if (mboxid > 0)
    {
      /* mailbox exists */
      fprintf(ci->tx,"* LIST (");
      
      /* show flags */
      result = db_isselectable(mboxid);
      if (result == -1)
	{
	  fprintf(ci->tx,"\n* BYE internal dbase error\n");
	  return -1;
	}

      if (result) fprintf(ci->tx,"\\Noselect ");

      result = db_noinferiors(mboxid);
      if (result == -1)
	{
	  fprintf(ci->tx,"\n* BYE internal dbase error\n");
	  return -1;
	}

      if (result) fprintf(ci->tx,"\\Noinferiors ");

      result = db_getmailboxname(mboxid, name);
      if (result == -1)
	{
	  fprintf(ci->tx,"\n* BYE internal dbase error\n");
	  return -1;
	}

      /* show delimiter & name */
      fprintf(ci->tx,") \"/\" %s\n",name);
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
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1;
    }

  for (i=0; i<nchildren; i++)
    {
      /* get name */
      result = db_getmailboxname(children[i], name);
      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\n");
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

      fprintf(ci->tx,"* LIST (");

      /* show flags */
      result = db_isselectable(children[i]);
      if (result == -1)
	{
	  fprintf(ci->tx,"\n* BYE internal dbase error\n");
	  free(children);
	  return -1;
	}

      if (!result) fprintf(ci->tx,"\\Noselect ");

      result = db_noinferiors(children[i]);
      if (result == -1)
	{
	  fprintf(ci->tx,"\n* BYE internal dbase error\n");
	  free(children);
	  return -1;
	}

      if (result) fprintf(ci->tx,"\\Noinferiors ");

	  /* show delimiter & name */
      fprintf(ci->tx,") \"/\" %s\n",name);
    }

  if (children)
    free(children);

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
/*  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;*/

  if (!check_state_and_args("LSUB", tag, args, 2, IMAPCS_AUTHENTICATED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK LSUB completed\n",tag);
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
  mailbox_t mb;
  int i,endfound,result;

  if (ud->state != IMAPCS_AUTHENTICATED && ud->state != IMAPCS_SELECTED)
    {
      fprintf(ci->tx,"%s BAD STATUS command received in invalid state\n", tag);
      return 1;
    }

  if (!args[0] || !args[1] || !args[2])
    {
      fprintf(ci->tx,"%s BAD missing argument(s) to STATUS\n", tag);
      return 1;
    }

  if (strcmp(args[1],"(") != 0)
    {
      fprintf(ci->tx,"%s BAD argument list should be parenthesed\n", tag);
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
	  fprintf(ci->tx,"%s BAD too many parentheses specified\n", tag);
	  return 1;
	}
    }
	
  if (endfound == 2)
    {
      fprintf(ci->tx,"%s BAD argument list empty\n", tag);
      return 1;
    }

  if (args[endfound+1])
    {
      fprintf(ci->tx,"%s BAD argument list too long\n", tag);
      return 1;
    }
    
  /* check if mailbox exists */
  mb.uid = db_findmailbox(args[0], ud->userid);
  if (mb.uid == (unsigned long)(-1))
    {
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1;
    }

  if (mb.uid == 0)
    {
      /* mailbox does not exist */
      fprintf(ci->tx,"%s NO specified mailbox does not exist\n",tag);
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
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\n");
      return -1;
    }
  
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\n");
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
	  fprintf(ci->tx,"\n%s BAD unrecognized option '%s' specified\n",tag,args[i]);
	  return 1;
	}
    }

  fprintf(ci->tx,")\n");

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
/*  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;*/

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
 * (equivalent to NOOP)
 */
int _ic_check(char *tag, char **args, ClientInfo *ci)
{
  if (!check_state_and_args("CHECK", tag, args, 0, IMAPCS_SELECTED, ci))
    return 1; /* error, return */

  fprintf(ci->tx,"%s OK CHECK completed\n",tag);
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

  fprintf(ci->tx,"%s OK CLOSE completed\n",tag);
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
      fprintf(ci->tx,"%s NO you do not have write permission on this folder\n",tag);
      return 1;
    }

  /* delete messages */
  result = db_expunge(ud->mailbox.uid,&msgids,&nmsgs);
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE dbase/memory error\n");
      return -1;
    }
  
  /* show expunge info */
  for (i=0; i<nmsgs; i++)
    {
      /* dump debug info */
      trace(TRACE_DEBUG,"trying to find %lu...\n",msgids[i]);
      for (j=0; j<ud->mailbox.exists; j++)
	trace(TRACE_DEBUG,"member #%d: %lu\n",j,ud->mailbox.seq_list[j]);
	  
      /* find the message sequence number */
      idx = binary_search(ud->mailbox.seq_list, ud->mailbox.exists, msgids[i]);

      fprintf(ci->tx,"* %d EXPUNGE\n",idx+1); /* add one: IMAP MSN starts at 1 not zero */
    }


  /* update mailbox info */
  i = 0;
  do
    {
      result = db_getmailbox(&ud->mailbox, ud->userid);
    } while (result == 1 && i++<MAX_RETRIES);

  if (result == 1)
    {
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\n");
      return -1;
    }
  
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1; /* fatal  */
    }


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
/*  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;*/

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
  int i,fetch_start,fetch_end,delimpos,result,setseen,j;
  fetch_items_t fetchitems;
  mime_message_t msg;
  char date[IMAP_INTERNALDATE_LEN];

  memset(&msg, 0, sizeof(msg));

  if (ud->state != IMAPCS_SELECTED)
    {
      fprintf(ci->tx,"%s BAD FETCH command received in invalid state\n", tag);
      return 1;
    }

  if (!args[0] || !args[1])
    {
      fprintf(ci->tx,"%s BAD missing argument(s) to FETCH\n", tag);
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
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\n");
      return -1;
    }
  
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1; /* fatal  */
    }


  /* determine message range */
  /* first check for a range specifier (':') */
  for (i=0,delimpos=-1; args[0][i]; i++)
    {
      if (args[0][i] == ':')
	delimpos = i;
      else if (!isdigit(args[0][i]))
	{
	  fprintf(ci->tx, "%s BAD invalid message range specified\n",tag);
	  return 1;
	}
    }

  /* delimiter at start/end ? */
  if (delimpos == 0 || delimpos == strlen(args[0])-1)
    {
      fprintf(ci->tx, "%s BAD invalid message range specified\n",tag);
      return 1;
    }

  if (delimpos == -1)
    {
      /* no delimiter, just a single number */
      fetch_start = atoi(args[0]);
      fetch_end = fetch_start;
    }
  else
    {
      /* delimiter present */
      args[0][delimpos] = '\0'; /* split up the two numbers */
      
      fetch_start = atoi(args[0]);
      fetch_end   = atoi(&args[0][delimpos+1]);
    }

  if (fetch_start == 0 || fetch_end == 0)
    {
      /* MSN starts at 1 */
      fprintf(ci->tx, "%s BAD invalid message range specified\n",tag);
      return 1;
    }

  if (fetch_start > ud->mailbox.exists || fetch_end > ud->mailbox.exists)
    {
      /* range contains non-existing msgs */
      fprintf(ci->tx, "%s BAD invalid message range specified\n",tag);
      return 1;
    }

  /* make sure start & end are in right order */
  if (fetch_start > fetch_end)
    {
      i = fetch_start;
      fetch_start = fetch_end;
      fetch_end = i;
    }

  /* lower start & end because our indices do start at 0 */
  fetch_start--;
  fetch_end--;


  /* OK msg MSN boundaries established */
  /* now fetch fetch items */

  result = get_fetch_items(&args[1], &fetchitems);
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE server ran out of memory\n");
      return -1; /* fatal  */
    }
  if (result == 1)
    {
      fprintf(ci->tx,"%s BAD invalid argument list to FETCH\n",tag);
      return 1;
    }

  /* now fetch results for each msg */
  for (i=fetch_start; i<=fetch_end; i++)
    {
      setseen = 0;
      fprintf(ci->tx,"* %d FETCH (",i+1);

      trace(TRACE_DEBUG, "Fetching msgID %lu\n", ud->mailbox.seq_list[i]);

      /* walk by the arguments */
      if (fetchitems.getFlags) 
	{
	  fprintf(ci->tx,"FLAGS (");
	  
	  for (j=0; j<IMAP_NFLAGS; j++)
	    {
	      result = db_get_msgflag(imap_flag_desc[j], ud->mailbox.uid, ud->mailbox.seq_list[i]);

	      if (result == -1)
		{
		  fprintf(ci->tx,"* BYE internal dbase error\n");
		  if (fetchitems.bodyfetches) free(fetchitems.bodyfetches);
		  return -1;
		}
	      else if (result == 1)
		fprintf(ci->tx,"\\%s ",imap_flag_desc[j]);
	    }
	  fprintf(ci->tx,") ");
	}

      if (fetchitems.getInternalDate)
	{
	  result = db_get_msgdate(ud->mailbox.uid, ud->mailbox.seq_list[i], date);
	  if (result == -1)
	    {
	      fprintf(ci->tx,"* BYE internal dbase error\n");
	      if (fetchitems.bodyfetches) free(fetchitems.bodyfetches);
	      return -1;
	    }

	  fprintf(ci->tx,"INTERNALDATE \"%s\" ",date);
	}

      if (fetchitems.getUID)
	{
	  fprintf(ci->tx,"UID %lu ",ud->mailbox.seq_list[i]);
	}
      
      if (fetchitems.getMIME_IMB)
	{
	  
	}

      if (fetchitems.getEnvelope)
	{
	}

      if (fetchitems.getSize)
	{
	}

      if (fetchitems.getRFC822Header)
	{
	}

      if (fetchitems.getRFC822Text)
	{
	}

      db_fetch_headers(ud->mailbox.seq_list[i], &msg);
      db_msgdump(&msg, ud->mailbox.seq_list[i]);

      for (j=0; j<fetchitems.nbodyfetches; j++)
	{
	  switch (fetchitems.bodyfetches[j].itemtype)
	    {
	    case BFIT_TEXT:
	      break;
	    case BFIT_HEADER:
	      break;
	    case BFIT_HEADER_FIELDS:
	      break;
	    case BFIT_HEADER_FIELDS_NOT:
	      break;
	    case BFIT_MIME:
	      break;
	    default:
	      fprintf(ci->tx, "* BYE internal server error\n");
	      free(fetchitems.bodyfetches);
	      return -1;
	    }
	}
    }
      
  if (fetchitems.bodyfetches)
    free(fetchitems.bodyfetches);
  
  fprintf(ci->tx," )\n");
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
  int i,store_start,store_end,delimpos,result,j;
  int be_silent=0,action=IMAPFA_NONE;
  int flaglist[IMAP_NFLAGS];

  memset(flaglist, 0, sizeof(int) * IMAP_NFLAGS);

  if (ud->state != IMAPCS_SELECTED)
    {
      fprintf(ci->tx,"%s BAD STORE command received in invalid state\n", tag);
      return 1;
    }

  if (!args[0] || !args[1] || !args[2])
    {
      fprintf(ci->tx,"%s BAD missing argument(s) to STORE\n", tag);
      return 1;
    }

  if (strcmp(args[2],"(") != 0)
    {
      fprintf(ci->tx,"%s BAD invalid argument(s) to STORE\n", tag);
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
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\n");
      return -1;
    }
  
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1; /* fatal  */
    }


  /* determine message range */
  /* first check for a range specifier (':') */
  for (i=0,delimpos=-1; args[0][i]; i++)
    {
      if (args[0][i] == ':')
	delimpos = i;
      else if (!isdigit(args[0][i]))
	{
	  fprintf(ci->tx, "%s BAD invalid message range specified\n",tag);
	  return 1;
	}
    }

  /* delimiter at start/end ? */
  if (delimpos == 0 || delimpos == strlen(args[0])-1)
    {
      fprintf(ci->tx, "%s BAD invalid message range specified\n",tag);
      return 1;
    }

  if (delimpos == -1)
    {
      /* no delimiter, just a single number */
      store_start = atoi(args[0]);
      store_end = store_start;
    }
  else
    {
      /* delimiter present */
      args[0][delimpos] = '\0'; /* split up the two numbers */
      
      store_start = atoi(args[0]);
      store_end   = atoi(&args[0][delimpos+1]);
    }

  if (store_start == 0 || store_end == 0)
    {
      /* MSN starts at 1 */
      fprintf(ci->tx, "%s BAD invalid message range specified\n",tag);
      return 1;
    }

  if (store_start > ud->mailbox.exists || store_end > ud->mailbox.exists)
    {
      /* range contains non-existing msgs */
      fprintf(ci->tx, "%s BAD invalid message range specified\n",tag);
      return 1;
    }

  /* make sure start & end are in right order */
  if (store_start > store_end)
    {
      i = store_start;
      store_start = store_end;
      store_end = i;
    }

  /* lower start & end because our indices do start at 0 */
  store_start--;
  store_end--;


  /* OK msg MSN boundaries established */
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
      fprintf(ci->tx,"%s BAD invalid STORE action specified\n",tag);
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
	  fprintf(ci->tx,"%s BAD invalid flag list to STORE command\n",tag);
	  return 1;
	}
    }

  /* set flags & show if needed */
  for (i=store_start; i<=store_end; i++)
    {
      if (!be_silent)
	fprintf(ci->tx,"* %d FETCH FLAGS (",i);

      switch (action)
	{
	case IMAPFA_REPLACE:
	  for (j=0; j<IMAP_NFLAGS; j++)
	    {
	      result = db_set_msgflag(imap_flag_desc[j], ud->mailbox.uid, ud->mailbox.seq_list[i],
				      flaglist[j]);

	      if (result == -1)
		{
		  fprintf(ci->tx,"\n* BYE internal dbase error\n");
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
		  result = db_set_msgflag(imap_flag_desc[j], ud->mailbox.uid, ud->mailbox.seq_list[i],
					  (action==IMAPFA_ADD));

		  if (result == -1)
		    {
		      fprintf(ci->tx,"\n* BYE internal dbase error\n");
		      return -1;
		    }
		}
	      if (!be_silent)
		{
		  result = db_get_msgflag(imap_flag_desc[j], ud->mailbox.uid, ud->mailbox.seq_list[i]);

		  if (result == -1)
		    {
		      fprintf(ci->tx,"\n* BYE internal dbase error\n");
		      return -1;
		    }
		  if (result == 1)
		    fprintf(ci->tx,"%s ",imap_flag_desc_escaped[j]);
		}
	    }
	  break;
	}

      if (!be_silent)
	fprintf(ci->tx,")\n");
    }

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
  int i,copy_start,copy_end,delimpos,result,j;
  unsigned long destmboxid;

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
      fprintf(ci->tx,"* BYE troubles synchronizing dbase\n");
      return -1;
    }
  
  if (result == -1)
    {
      fprintf(ci->tx,"* BYE internal dbase error\n");
      return -1; /* fatal  */
    }


  /* determine message range */
  /* first check for a range specifier (':') */
  for (i=0,delimpos=-1; args[0][i]; i++)
    {
      if (args[0][i] == ':')
	delimpos = i;
      else if (!isdigit(args[0][i]))
	{
	  fprintf(ci->tx, "%s BAD invalid message range specified\n",tag);
	  return 1;
	}
    }

  /* delimiter at start/end ? */
  if (delimpos == 0 || delimpos == strlen(args[0])-1)
    {
      fprintf(ci->tx, "%s BAD invalid message range specified\n",tag);
      return 1;
    }

  if (delimpos == -1)
    {
      /* no delimiter, just a single number */
      copy_start = atoi(args[0]);
      copy_end = copy_start;
    }
  else
    {
      /* delimiter present */
      args[0][delimpos] = '\0'; /* split up the two numbers */
      
      copy_start = atoi(args[0]);
      copy_end   = atoi(&args[0][delimpos+1]);
    }

  if (copy_start == 0 || copy_end == 0)
    {
      /* MSN starts at 1 */
      fprintf(ci->tx, "%s BAD invalid message range specified\n",tag);
      return 1;
    }

  if (copy_start > ud->mailbox.exists || copy_end > ud->mailbox.exists)
    {
      /* range contains non-existing msgs */
      fprintf(ci->tx, "%s BAD invalid message range specified\n",tag);
      return 1;
    }

  /* make sure start & end are in right order */
  if (copy_start > copy_end)
    {
      i = copy_start;
      copy_start = copy_end;
      copy_end = i;
    }

  /* lower start & end because our indices do start at 0 */
  copy_start--;
  copy_end--;

  /* OK msg MSN boundaries established */
  /* check if destination mailbox exists */
  destmboxid = db_findmailbox(args[1], ud->userid);
  if (destmboxid == 0)
    {
      /* error: cannot select mailbox */
      fprintf(ci->tx, "%s NO [TRYCREATE] specified mailbox does not exist\n",tag);
      return 1;
    }
  if (destmboxid == (unsigned long)(-1))
    {
      fprintf(ci->tx, "* BYE internal dbase error\n");
      return -1; /* fatal */
    }

  /* ok copy msgs */
  for (i=copy_start; i<=copy_end; i++)
    {
      result = db_copymsg(ud->mailbox.seq_list[i], destmboxid);
      if (result == -1)
	{
	  fprintf(ci->tx,"* BYE internal dbase error\n");
	  return -1;
	}
    }

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






