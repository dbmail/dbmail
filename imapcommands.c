/* 
 * imapcommands.c
 * 
 * IMAP server command implementations
 */

#include "imapcommands.h"
#include "debug.h"
#include "imaputil.h"
#include "dbmysql.h"


/*
 * _ic_login()
 *
 * Performs login-request handling.
 */
void _ic_login(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;
  unsigned long userid;

  if (ud->state != IMAPCS_NON_AUTHENTICATED)
    {
      fprintf(ci->tx,"%s BAD LOGIN command received in invalid state\n",tag);
      return;
    }

  if (!args[0] || !args[1])
    {
      /* error: need 2 args */
      fprintf(ci->tx,"%s BAD missing argument(s) to LOGIN\n",tag);
      return;
    }

  if (args[2])
    {
      /* error: >2 args */
      fprintf(ci->tx,"%s BAD too many arguments to LOGIN\n",tag);
      return;
    }

  userid = db_validate(args[0], args[1]);

  if (userid == -1)
    {
      /* a db-error occurred */
      fprintf(ci->tx,"* BAD internal db error validating user\n");
      trace(TRACE_MESSAGE,"IMAP: db-validate error while validating user %s (pass %s).",args[0],args[1]);
      return;
    }

  if (userid == 0)
    {
      /* validation failed: invalid user/pass combination */
      fprintf(ci->tx, "%s NO login rejected\n",tag);
      return;
    }

  /* login ok */
  /* update client info */
  ud->userid = userid;
  ud->state = IMAPCS_AUTHENTICATED;

  fprintf(ci->tx,"%s OK user %s logged in\n",tag,args[0]);
  return;
}

void _ic_authenticate(char *tag, char **args, ClientInfo *ci)
{

}

void _ic_select(char *tag, char **args, ClientInfo *ci) {}
void _ic_list(char *tag, char **args, ClientInfo *ci) {}
void _ic_logout(char *tag, char **args, ClientInfo *ci) {}
