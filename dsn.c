/* Delivery User Functions
 * Aaron Stone, 9 Feb 2004 */

#include <stdlib.h>
#include <string.h>

#include "dsn.h"
#include "list.h"
#include "auth.h"
#include "debug.h"


int dsnuser_init(deliver_to_user_t *dsnuser)
{
        dsnuser->useridnr = 0;
        dsnuser->dsn.class = 0;
        dsnuser->dsn.subject = 0;
        dsnuser->dsn.detail = 0;

        dsnuser->address = NULL;
        dsnuser->mailbox = NULL;

        dsnuser->userids = (struct list *)my_malloc(sizeof(struct list));
	if (dsnuser->userids == NULL)
		return -1;
        dsnuser->forwards = (struct list *)my_malloc(sizeof(struct list));
	if (dsnuser->forwards == NULL)
		return -1;

        list_init(dsnuser->userids);
        list_init(dsnuser->forwards);

	trace(TRACE_DEBUG, "%s, %s: dsnuser initialized",
		__FILE__, __FUNCTION__);
	return 0;
}


void dsnuser_free(deliver_to_user_t *dsnuser)
{
        dsnuser->useridnr = 0;
        dsnuser->dsn.class = 0;
        dsnuser->dsn.subject = 0;
        dsnuser->dsn.detail = 0;

	/* These are nominally const, but
	 * we really do want to free them. */
        my_free((char *)dsnuser->address);
        my_free((char *)dsnuser->mailbox);

        list_freelist(&dsnuser->userids->start);
        list_freelist(&dsnuser->forwards->start);

        my_free(dsnuser->userids);
        my_free(dsnuser->forwards);

	trace(TRACE_DEBUG, "%s, %s: dsnuser freed",
		__FILE__, __FUNCTION__);
}


int dsnuser_resolve_list(struct list *deliveries)
{
  u64_t userid;
  int alias_count = 0, domain_count = 0;
  char *domain = NULL;
  char *username = NULL;
  struct element *element;

  /* Loop through the users list */
  for (element = list_getstart(deliveries); element != NULL; element = element->nextnode)
    {
      deliver_to_user_t *delivery = (deliver_to_user_t *)element->data;

      /* If the userid is already set, then we're doing direct-to-userid. */
      if (delivery->useridnr != 0)
        {
          /* This seems to be the only way to see if a useridnr is valid. */
          username = auth_get_userid(delivery->useridnr);
          if (username != NULL)
            {
              /* Free the username, we don't actually need it. */
              my_free(username);

              /* Copy the delivery useridnr into the userids list. */
              if (list_nodeadd(delivery->userids, &delivery->useridnr, sizeof(delivery->useridnr)) == 0)
                {
                  trace(TRACE_ERROR, "%s, %s: out of memory",
                      __FILE__, __FUNCTION__);
                  return -1;
                }

              /* The userid was valid... */
              delivery->dsn.class = 2; /* Success. */
              delivery->dsn.subject = 1; /* Address related. */
              delivery->dsn.detail = 5; /* Valid. */
            }
          else /* from: 'if (username != NULL)' */
            {
              /* The userid was invalid... */
              delivery->dsn.class = 5; /* Permanent failure. */
              delivery->dsn.subject = 1; /* Address related. */
              delivery->dsn.detail = 1; /* Does not exist. */
            }
        }
      /* We don't have a useridnr, so we have either a username or an alias. */
      else /* from: 'if (delivery->useridnr != 0)' */
        {
          /* See if the address is a username. */
          switch (auth_user_exists(delivery->address, &userid))
            {
              case -1:
                {
                  /* An error occurred */
                  trace(TRACE_ERROR,"%s, %s: error checking user [%s]",
                      __FILE__, __FUNCTION__, delivery->address);
                  return -1;
                  break;
                }
              case 1:
                {
                  if (list_nodeadd(delivery->userids, &userid, sizeof(u64_t)) == 0)
	            {
                      trace(TRACE_ERROR, "%s, %s: out of memory",
                          __FILE__, __FUNCTION__);
                      return -1;
	            }
	          else
	            {
              
                      trace(TRACE_DEBUG, "%s, %s: added user [%s] id [%llu] to delivery list",
                          __FILE__, __FUNCTION__, delivery->address, userid);
                      /* The userid was valid... */
                      delivery->dsn.class = 2; /* Success. */
                      delivery->dsn.subject = 1; /* Address related. */
                      delivery->dsn.detail = 5; /* Valid. */
	            }
                  break;
                }
                /* The address needs to be looked up */
              default:
                {
                  alias_count = auth_check_user_ext(delivery->address, delivery->userids, delivery->forwards, -1);
                  trace(TRACE_DEBUG, "%s, %s: user [%s] found total of [%d] aliases",
                      __FILE__, __FUNCTION__, delivery->address, alias_count);
              
                  /* No aliases found for this user */
                  if (alias_count == 0)
                    {
                      trace(TRACE_INFO,"%s, %s: user [%s] checking for domain forwards.",
                          __FILE__, __FUNCTION__, delivery->address);  
               
                      domain = strchr(delivery->address, '@');
               
                      if (domain == NULL)
                        {
                          /* That's it, we're done here. */
                          /* Permanent failure... */
                          delivery->dsn.class = 5; /* Permanent failure. */
                          delivery->dsn.subject = 1; /* Address related. */
                          delivery->dsn.detail = 1; /* Does not exist. */
                        }
                      else
                        {
                          trace(TRACE_DEBUG, "%s, %s: domain [%s] checking for domain forwards",
                              __FILE__, __FUNCTION__, domain);
               
                          /* Checking for domain aliases */
                          domain_count = auth_check_user_ext(domain, delivery->userids, delivery->forwards, -1);
                          trace(TRACE_DEBUG,"%s, %s: domain [%s] found total of [%d] aliases",
                              __FILE__, __FUNCTION__, domain, domain_count);
               
                          if (domain_count == 0)
                            {
                              /* Permanent failure... */
                              delivery->dsn.class = 5; /* Permanent failure. */
                              delivery->dsn.subject = 1; /* Address related. */
                              delivery->dsn.detail = 1; /* Does not exist. */
                            }
                          else /* from: 'if (domain_count == 0)' */
                            {
                              /* The userid was valid... */
                              delivery->dsn.class = 2; /* Success. */
                              delivery->dsn.subject = 1; /* Address related. */
                              delivery->dsn.detail = 5; /* Valid. */
                            } /* from: 'if (domain_count == 0)' */
                        } /* from: 'if (domain == NULL)' */
                    }
                  else /* from: 'if (alias_count == 0)' */
                    {
                      /* The userid was valid... */
                      delivery->dsn.class = 2; /* Success. */
                      delivery->dsn.subject = 1; /* Address related. */
                      delivery->dsn.detail = 5; /* Valid. */
                    } /* from: 'if (alias_count == 0)' */
                } /* from: 'default:' */
            } /* from: 'switch (auth_user_exists(delivery->address, &userid))' */
        } /* from: 'if (delivery->useridnr != 0)' */
    } /* from: the main for loop */

  return 0;
}

