/* $Id$
 * (c) 2003 Aaron Stone
 *
 * Functions for running user defined sorting rules
 * on a message in the temporary store, usually
 * just delivering the message to the user's INBOX
 * ...unless they have fancy rules defined, that is :-)
 * */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>
#include <ctype.h>
#include "db.h"
#include "auth.h"
#include "debug.h"
#include "list.h"
#include "dbmail.h"
#include "debug.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "dbmd5.h"
#include "misc.h"

#include "sortsieve.h"
#include "sort.h"
#include <sieve2_interface.h>

extern struct list smtpItems, sysItems;

/* typedef sort_action {
 *   int method,
 *   char *destination,
 *   char *message
 * } sort_action_t;
 * */

/* Pull up the relevant sieve scripts for this
 * user and begin running them against the header
 * and possibly the body of the message.
 *
 * Returns 0 on success, -1 on failure,
 * and +1 on success but with memory leaking.
 * In the +1 case, if called from a daemon
 * such as dbmail-lmtpd, the daemon should
 * finish storing the message and restart.
 * */
int sortsieve_msgsort(u64_t useridnr, char *header, u64_t headersize, u64_t messagesize, struct list *actions)
{
  sieve2_message_t *m;
  sieve2_support_t *p;
  sieve2_script_t *s;
  sieve2_action_t *a;
  sieve2_loader_t scriptloader, msgloader;
  char *scriptname = NULL, *script = NULL, *freestr = NULL;
  int res = 0, ret = 0;

  /* Pass the address of the char *script, and it will come
   * back magically allocated. Don't forget to free it later! */
  res = db_get_sievescript_active(useridnr, &scriptname);
  if( res < 0 ) {
    printf("db_get_sievescript_active() returns %d\n", res);
    ret = -1;
    goto no_free;
  }

  printf( "Looking up script [%s]\n", scriptname );
  res = db_get_sievescript_byname(useridnr, scriptname, &script);
  if( res < 0 ) {
    printf("db_get_sievescript_byname() returns %d\n", res);
    ret = -1;
    goto char_scriptname_free;
  }

  res = sieve2_action_alloc(&a);
  if (res != SIEVE2_OK) {
    printf("sieve2_action_alloc() returns %d\n", res);
    ret = -1;
    goto char_script_free;
  }

  res = sieve2_support_alloc(&p);
  if (res != SIEVE2_OK) {
    printf("sieve2_support_alloc() returns %d\n", res);
    ret = -1;
    goto action_free;
  }

  res = sieve2_support_register(p, SIEVE2_ACTION_FILEINTO);
  res = sieve2_support_register(p, SIEVE2_ACTION_REDIRECT);
  res = sieve2_support_register(p, SIEVE2_ACTION_REJECT);
//  res = sieve2_support_register(p, SIEVE2_ACTION_NOTIFY);

  res = sieve2_script_alloc(&s);
  if (res != SIEVE2_OK) {
    printf("sieve2_script_alloc() returns %d\n", res);
    ret = -1;
    goto support_free;
  }

  res = sieve2_support_bind(p, s);
  if (res != SIEVE2_OK) {
    printf("sieve2_support_bind() returns %d\n", res);
    ret = -1;
    goto script_free;
  }

  res = sieve2_script_parse(s, script);
  if (res != SIEVE2_OK) {
    printf("sieve2_script_parse() returns %d: %s\n", res, sieve2_errstr(res, &freestr));
    my_free(freestr);
    ret = -1;
    goto script_free;
  }

  res = sieve2_message_alloc(&m);
  if (res != SIEVE2_OK) {
    printf("sieve2_message_alloc() returns %d\n", res);
    ret = -1;
    goto script_free;
  }

  res = sieve2_message_register(m, &messagesize, SIEVE2_MESSAGE_SIZE);
  if (res != SIEVE2_OK) {
    printf("sieve2_message_register() returns %d\n", res);
    ret = -1;
    goto message_free;
  }
  res = sieve2_message_register(m, header, SIEVE2_MESSAGE_HEADER);
  if (res != SIEVE2_OK) {
    printf("sieve2_message_register() returns %d\n", res);
    ret = -1;
    goto message_free;
  }

  res = sieve2_script_exec(s, m, a);
  if (res != SIEVE2_OK) {
    printf("sieve2_execute_script() returns %d\n", res);
    ret = -1;
    goto message_free;
  }

  res = sortsieve_unroll_action(a, actions);
  if (res != SIEVE2_OK && res != SIEVE2_DONE ) {
    printf("unroll_action() returns %d\n", res);
    ret = -1;
    goto action_free;
  }

message_free:
  res = sieve2_message_free(m);
  if (res != SIEVE2_OK) {
    printf("sieve2_message_free() returns %d\n", res);
    ret = 1;
  }

script_free:
  res = sieve2_script_free(s);
  if (res != SIEVE2_OK) {
    printf("sieve2_script_free() returns %d\n", res);
    ret = 1;
  }

support_free:
  res = sieve2_support_free(p);
  if (res != SIEVE2_OK) {
    printf("sieve2_support_free() returns %d\n", res);
    ret = 1;
  }

action_free:
  res = sieve2_action_free(a);
  if (res != SIEVE2_OK) {
    printf("sieve2_action_free() returns %d\n", res);
    ret = 1;
  }

  /* Good thing we're not forgetting ;-) */
char_script_free:
  if (script != NULL)
      my_free(script);
char_scriptname_free:
  if (scriptname != NULL)
      my_free(scriptname);

no_free:
  return ret;
}

int sortsieve_unroll_action(sieve2_action_t *a, struct list *actions)
{
  int res = SIEVE2_OK;
  int code;
  void *action_context;

  /* Working variables to set up
   * the struct then nodeadd it */
  sort_action_t *tmpsa = NULL;
  char *tmpdest = NULL;
  char *tmpmsg = NULL;
  int tmpmeth = 0;

  while(res == SIEVE2_OK)
    {
      if((tmpsa = malloc(sizeof(sort_action_t))) == NULL)
          break;
      res = sieve2_action_next(&a, &code, &action_context);
      if(res == SIEVE2_DONE)
        {
          printf("We've reached the end.\n");
          break;
        }
      else if (res != SIEVE2_OK)
        {
          printf("Error in action list.\n");
          break;
        }
      printf("Action code is: %d\n", code);

      switch (code)
        {
          case SIEVE2_ACTION_REDIRECT:
            {
              sieve2_redirect_context_t *context = (sieve2_redirect_context_t *)action_context;
              printf( "Action is REDIRECT: " );
              printf( "Destination is %s\n", context->addr);
	      tmpmeth = SA_REDIRECT;
	      tmpdest = strdup(context->addr);
              break;
            }
          case SIEVE2_ACTION_REJECT:
            {
              sieve2_reject_context_t *context = (sieve2_reject_context_t *)action_context;
              printf( "Action is REJECT: " );
              printf( "Message is %s\n", context->msg);
	      tmpmeth = SA_REJECT;
	      tmpmsg = strdup(context->msg);
              break;
            }
          case SIEVE2_ACTION_DISCARD:
              printf( "Action is DISCARD\n" );
	      tmpmeth = SA_DISCARD;
              break;
          case SIEVE2_ACTION_FILEINTO:
            {
              sieve2_fileinto_context_t *context = (sieve2_fileinto_context_t *)action_context;
              printf( "Action is FILEINTO: " );
              printf( "Destination is %s\n", context->mailbox);
	      tmpmeth = SA_FILEINTO;
	      tmpdest = strdup(context->mailbox);
              break;
            }
          case SIEVE2_ACTION_NOTIFY:
            {
              sieve2_notify_context_t *context = (sieve2_notify_context_t *)action_context;
              printf( "Action is NOTIFY: \n" );
              // FIXME: Prefer to have a function for this?
              while(context != NULL)
                {
                  printf( "  ID \"%s\" is %s\n", context->id, ( context->isactive ? "ACTIVE" : "INACTIVE" ) );
                  printf( "    Method is %s\n", context->method );
                  printf( "    Priority is %s\n", context->priority );
                  printf( "    Message is %s\n", context->message );
                  if(context->options != NULL)
                    {
                      size_t opt = 0;
                      while(context->options[opt] != NULL)
                        {
                          printf( "    Options are %s\n", context->options[opt] );
                          opt++;
                        }
                    }
                  context = context->next;
                }
                break;
              }
            case SIEVE2_ACTION_KEEP:
              printf( "Action is KEEP\n" );
              break;
            default:
              printf( "Unrecognized action code: %d\n", code );
              break;
        } /* case */

      tmpsa->method = tmpmeth;
      tmpsa->destination = tmpdest;
      tmpsa->message = tmpmsg;
  
      list_nodeadd(actions, tmpsa, sizeof(sort_action_t));

      my_free(tmpsa);
      tmpsa = NULL;

    } /* while */

  if (tmpsa != NULL)
      my_free(tmpsa);

  return res;
}

/* Return 0 on script OK, 1 on script error. */
int sortsieve_script_validate(char *script, char **errmsg)
{
  if(sieve2_validate(t, s, p, e) == SIEVE2_OK)
    {
      *errmsg = NULL;
      return 0;
    }
  else
    {
      *errmsg = "Script error...";
      return 1;
    }
}

