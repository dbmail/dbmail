/* $Id: sortsieve.c 1893 2005-10-05 15:04:58Z paul $

 Copyright (C) 1999-2004 Aaron Stone aaron at serendipity dot cx

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * Functions for running user defined sorting rules
 * on a message in the temporary store, usually
 * just delivering the message to the user's INBOX
 * ...unless they have fancy rules defined, that is :-)
 * 
 */

#include "dbmail.h"

extern struct dm_list smtpItems, sysItems;

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
int sortsieve_msgsort(u64_t useridnr, char *header, u64_t headersize, u64_t messagesize, struct dm_list *actions)
{

	int res = 0, ret = 0;
#ifdef OLDSIEVE
	sieve2_message_t *m;
	sieve2_support_t *p;
	sieve2_script_t *s;
	sieve2_action_t *a;
	sieve2_interp_t *t;
	sieve2_error_t *e;
	sievefree_t sievefree;
	char *scriptname = NULL, *script = NULL, *freestr = NULL;

	memset(&sievefree, 0, sizeof(sievefree_t));

	/* Pass the address of the char *script, and it will come
	 * back magically allocated. Don't forget to free it later! */
	res = db_get_sievescript_active(useridnr, &scriptname);
	if (res < 0) {
		trace(TRACE_ERROR, "db_get_sievescript_active() returns %d\n", res);
		ret = -1;
		goto skip_free;
	}

	trace(TRACE_DEBUG, "Looking up script [%s]\n", scriptname);
	res = db_get_sievescript_byname(useridnr, scriptname, &script);
	if (res < 0) {
		trace(TRACE_ERROR, "db_get_sievescript_byname() returns %d\n", res);
		ret = -1;
		goto need_free;
	}

	res = sieve2_action_alloc(&a);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "sieve2_action_alloc() returns %d\n", res);
		ret = -1;
		goto need_free;
	}
	sievefree.free_action = 1;

	res = sieve2_support_alloc(&p);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "sieve2_support_alloc() returns %d\n", res);
		ret = -1;
		goto need_free;
	}
	sievefree.free_support = 1;

	res = sieve2_support_register(p, NULL, SIEVE2_ACTION_FILEINTO);
	res = sieve2_support_register(p, NULL, SIEVE2_ACTION_REDIRECT);
	res = sieve2_support_register(p, NULL, SIEVE2_ACTION_REJECT);
//  res = sieve2_support_register(p, SIEVE2_ACTION_NOTIFY);

	res = sieve2_script_alloc(&s);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "sieve2_script_alloc() returns %d\n", res);
		ret = -1;
		goto need_free;
	}
	sievefree.free_script = 1;

	res = sieve2_message_alloc(&m);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "sieve2_message_alloc() returns %d\n", res);
		ret = -1;
		goto need_free;
	}
	sievefree.free_message = 1;

	res = sieve2_script_register(s, script, SIEVE2_SCRIPT_CHAR_ARRAY);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "sieve2_script_parse() returns %d: %s\n", res,
		       sieve2_errstr(res, &freestr));
		dm_free(freestr);
		ret = -1;
		goto need_free;
	}

	res = sieve2_message_register(m, &messagesize, SIEVE2_MESSAGE_SIZE);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "sieve2_message_register() returns %d\n", res);
		ret = -1;
		goto need_free;
	}
	res = sieve2_message_register(m, header, SIEVE2_MESSAGE_HEADER);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "sieve2_message_register() returns %d\n", res);
		ret = -1;
		goto need_free;
	}

	res = sieve2_execute(t, s, p, e, m, a);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "sieve2_execute_script() returns %d\n", res);
		ret = -1;
		goto need_free;
	}

	res = sortsieve_unroll_action(a, actions);
	if (res != SIEVE2_OK && res != SIEVE2_DONE) {
		trace(TRACE_ERROR, "unroll_action() returns %d\n", res);
		ret = -1;
		goto need_free;
	}

	/* Fixme: If one of these fails, we should assume a memory leak
	 * and go suicidal so that a new child process will start. */
      need_free:
	if (sievefree.free_interp)
		sieve2_interp_free(t);
	if (sievefree.free_support)
		sieve2_support_free(p);
	if (sievefree.free_script)
		sieve2_script_free(s);
	if (sievefree.free_error)
		sieve2_error_free(e);
	if (sievefree.free_message)
		sieve2_message_free(m);
	if (sievefree.free_action)
		sieve2_action_free(a);

	if (script != NULL)
		dm_free(script);
	if (scriptname != NULL)
		dm_free(scriptname);
  
	if (script != NULL)
		dm_free(script);
	if (scriptname != NULL)
		dm_free(scriptname);

#endif
      skip_free:
	return ret;
	
}

int sortsieve_unroll_action(sieve2_values_t * a, struct dm_list *actions)
{

	int res = SIEVE2_OK;
#ifdef OLDSIEVE
	int code;
	void *action_context;

	/* Working variables to set up
	 * the struct then nodeadd it */
	sort_action_t *tmpsa = NULL;
	char *tmpdest = NULL;
	char *tmpmsg = NULL;
	int tmpmeth = 0;

	while (res == SIEVE2_OK) {
		if ((tmpsa = dm_malloc(sizeof(sort_action_t))) == NULL)
			break;
		res = sieve2_action_next(&a, &code, &action_context);
		if (res == SIEVE2_DONE) {
			trace(TRACE_DEBUG, "We've reached the end.\n");
			break;
		} else if (res != SIEVE2_OK) {
			trace(TRACE_ERROR, "Error in action list.\n");
			break;
		}
		trace(TRACE_DEBUG, "Action code is: %d\n", code);

		switch (code) {
		case SIEVE2_ACTION_REDIRECT:
			{
				sieve2_redirect_context_t *context =
				    (sieve2_redirect_context_t *)
				    action_context;
				trace(TRACE_DEBUG, "Action is REDIRECT: ");
				trace(TRACE_DEBUG, "Destination is %s\n",
				       context->addr);
				tmpmeth = SA_REDIRECT;
				tmpdest = dm_strdup(context->addr);
				break;
			}
		case SIEVE2_ACTION_REJECT:
			{
				sieve2_reject_context_t *context =
				    (sieve2_reject_context_t *)
				    action_context;
				trace(TRACE_DEBUG, "Action is REJECT: ");
				trace(TRACE_DEBUG, "Message is %s\n", context->msg);
				tmpmeth = SA_REJECT;
				tmpmsg = dm_strdup(context->msg);
				break;
			}
		case SIEVE2_ACTION_DISCARD:
			trace(TRACE_DEBUG, "Action is DISCARD\n");
			tmpmeth = SA_DISCARD;
			break;
		case SIEVE2_ACTION_FILEINTO:
			{
				sieve2_fileinto_context_t *context =
				    (sieve2_fileinto_context_t *)
				    action_context;
				trace(TRACE_DEBUG, "Action is FILEINTO: ");
				trace(TRACE_DEBUG, "Destination is %s\n",
				       context->mailbox);
				tmpmeth = SA_FILEINTO;
				tmpdest = dm_strdup(context->mailbox);
				break;
			}
		case SIEVE2_ACTION_NOTIFY:
			{
				sieve2_notify_context_t *context =
				    (sieve2_notify_context_t *)
				    action_context;
				trace(TRACE_DEBUG, "Action is NOTIFY: \n");
				// FIXME: Prefer to have a function for this?
				while (context != NULL) {
					trace(TRACE_DEBUG, "  ID \"%s\" is %s\n",
					       context->id,
					       (context->
						isactive ? "ACTIVE" :
						"INACTIVE"));
					trace(TRACE_DEBUG, "    Method is %s\n",
					       context->method);
					trace(TRACE_DEBUG, "    Priority is %s\n",
					       context->priority);
					trace(TRACE_DEBUG, "    Message is %s\n",
					       context->message);
					if (context->options != NULL) {
						size_t opt = 0;
						while (context->
						       options[opt] !=
						       NULL) {
							trace(TRACE_DEBUG, "    Options are %s\n",
							     context->options[opt]);
							opt++;
						}
					}
					context = context->next;
				}
				break;
			}
		case SIEVE2_ACTION_KEEP:
			trace(TRACE_DEBUG, "Action is KEEP\n");
			break;
		default:
			trace(TRACE_DEBUG, "Unrecognized action code: %d\n", code);
			break;
		}		/* case */

		tmpsa->method = tmpmeth;
		tmpsa->destination = tmpdest;
		tmpsa->message = tmpmsg;

		dm_list_nodeadd(actions, tmpsa, sizeof(sort_action_t));

		dm_free(tmpsa);
		tmpsa = NULL;

	}			/* while */

	if (tmpsa != NULL)
		dm_free(tmpsa);

#endif
	return res;
	
}

/* Return 0 on script OK, 1 on script error. */
int sortsieve_script_validate(char *script, char **errmsg)
{
	int ret = 0, res;
#ifdef OLDSIEVE
	sieve2_interp_t *t;
	sieve2_script_t *s;
	sieve2_support_t *p;
	sieve2_error_t *e;
	sievefree_t sievefree;

	memset(&sievefree, 0, sizeof(sievefree_t));

	res = sieve2_interp_alloc(&t);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "%s, %s: sieve2_interp_alloc() returns %d",
			__FILE__, __func__, res);
		ret = 1;
	}
	sievefree.free_interp = 1;
 
	res = sieve2_support_alloc(&p);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "%s, %s: sieve2_support_alloc() returns %d",
			__FILE__, __func__, res);
		ret = 1;
	}
	sievefree.free_support = 1;
 
	res = 0;
	SIEVE2_SUPPORT_REGISTER(res);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "%s, %s: sieve2_support_register had errors.",
			__FILE__, __func__);
	}
 
	res = sieve2_script_alloc(&s);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "%s, %s: sieve2_script_alloc returns %d",
			__FILE__, __func__, res);
 
		ret = 1;
	}
	sievefree.free_script = 1;

	res = sieve2_script_register(s, script, SIEVE2_SCRIPT_CHAR_ARRAY);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "%s, %s: sieve2_script_register() returns %d",
			__FILE__, __func__, res);
		ret = 1;
	}

	res = sieve2_error_alloc(&e);
	if (res != SIEVE2_OK) {
		trace(TRACE_ERROR, "%s, %s: sieve2_error_alloc returns %s",
			__FILE__, __func__, res);
		ret = 1;
	}
 
	if (sieve2_validate(t, s, p, e) == SIEVE2_OK) {
		*errmsg = NULL;
		ret = 0;
	} else {
		*errmsg = "Script error...";
		ret = 1;
	}

	if (sievefree.free_interp)
		sieve2_interp_free(t);
	if (sievefree.free_support)
		sieve2_support_free(p);
	if (sievefree.free_script)
		sieve2_script_free(s);
	if (sievefree.free_error)
		sieve2_error_free(e);

#endif
	return ret;
	
}
