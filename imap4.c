/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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
*/

/* $Id$
 * imap4.c
 *
 * implements an IMAP 4 rev 1 server.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "imap4.h"
#include "imaputil.h"
#include "imapcommands.h"
#include "misc.h"
#include "clientinfo.h"
#include "debug.h"
#include "db.h"
#include "auth.h"

#define MAX_LINESIZE (10*1024)
#define COMMAND_SHOW_LEVEL TRACE_ERROR

#define null_free(p) { my_free(p); p = NULL; }


/* cache */
cache_t cached_msg;

/* consts */
const char AcceptedChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "!@#$%^&*()-=_+`~[]{}\\|'\" ;:,.<>/? \n\r";

const char AcceptedTagChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "!@#$%^&-=_`~\\|'\" ;:,.<>/? ";

const char AcceptedMailboxnameChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-=/ _.&,+";

const char *IMAP_COMMANDS[] = {
	"", "capability", "noop", "logout",
	"authenticate", "login",
	"select", "examine", "create", "delete", "rename", "subscribe",
	"unsubscribe",
	"list", "lsub", "status", "append",
	"check", "close", "expunge", "search", "fetch", "store", "copy",
	"uid", "sort", "getquotaroot", "getquota",
	"setacl", "deleteacl", "getacl", "listrights", "myrights",
	"namespace",
	"***NOMORE***"
};


enum IMAP_COMMAND_TYPES { IMAP_COMM_NONE,
	IMAP_COMM_CAPABILITY, IMAP_COMM_NOOP, IMAP_COMM_LOGOUT,
	IMAP_COMM_AUTH, IMAP_COMM_LOGIN,
	IMAP_COMM_SELECT, IMAP_COMM_EXAMINE, IMAP_COMM_CREATE,
	IMAP_COMM_DELETE, IMAP_COMM_RENAME, IMAP_COMM_SUBSCRIBE,
	IMAP_COMM_UNSUBSCRIBE, IMAP_COMM_LIST, IMAP_COMM_LSUB,
	IMAP_COMM_STATUS, IMAP_COMM_APPEND,
	IMAP_COMM_CHECK, IMAP_COMM_CLOSE, IMAP_COMM_EXPUNGE,
	IMAP_COMM_SEARCH, IMAP_COMM_FETCH, IMAP_COMM_STORE,
	IMAP_COMM_COPY, IMAP_COMM_UID, IMAP_COMM_SORT,
	IMAP_COMM_GETQUOTAROOT, IMAP_COMM_GETQUOTA,
	IMAP_COMM_SETACL, IMAP_COMM_DELETEACL, IMAP_COMM_GETACL,
	IMAP_COMM_LISTRIGHTS, IMAP_COMM_MYRIGHTS,
	IMAP_COMM_NAMESPACE,
	IMAP_COMM_LAST
};


const IMAP_COMMAND_HANDLER imap_handler_functions[] = {
	NULL,
	_ic_capability, _ic_noop, _ic_logout,
	_ic_authenticate, _ic_login,
	_ic_select, _ic_examine, _ic_create, _ic_delete, _ic_rename,
	_ic_subscribe, _ic_unsubscribe, _ic_list, _ic_lsub, _ic_status,
	_ic_append,
	_ic_check, _ic_close, _ic_expunge, _ic_search, _ic_fetch,
	_ic_store, _ic_copy, _ic_uid, _ic_sort,
	_ic_getquotaroot, _ic_getquota,
	_ic_setacl, _ic_deleteacl, _ic_getacl, _ic_listrights,
	_ic_myrights,
	_ic_namespace,
	NULL
};



void ci_cleanup(ClientInfo *ci);
void ci_cleanup(ClientInfo *ci)
{
	close_cache();
	null_free(((imap_userdata_t*)ci->userData)->mailbox.seq_list);
	null_free(ci->userData);
}


/*
 * Main handling procedure
 *
 * returns EOF on logout/fatal error or 1 otherwise
 */
int IMAPClientHandler(ClientInfo * ci)
{
	char line[MAX_LINESIZE];
	char *tag = NULL, *cpy, **args, *command;
	int done, result;
	size_t i;
	int nfaultyresponses;
	imap_userdata_t *ud = NULL;
	mailbox_t newmailbox;
	int this_was_noop = 0;

	/* init: add userdata */
	ci->userData = my_malloc(sizeof(imap_userdata_t));
	if (!ci->userData) {
		/* out of mem */
		trace(TRACE_ERROR,
		      "IMAPClientHandler(): not enough memory.");
		return -1;
	}

	memset(ci->userData, 0, sizeof(imap_userdata_t));
	((imap_userdata_t *) ci->userData)->state =
	    IMAPCS_NON_AUTHENTICATED;
	ud = ci->userData;

	/* greet user */
	if (ci_write(ci->tx,
		     "* OK dbmail imap (protocol version 4r1) server %s "
		     "ready to run\r\n", IMAP_SERVER_VERSION)) {
		ci_cleanup(ci);
		return EOF;
	}
	fflush(ci->tx);

	/* init: cache */
	if (init_cache() != 0) {
		trace(TRACE_ERROR,
		      "IMAPClientHandler(): cannot open temporary file\n");
		if (ci_write(ci->tx, "* BYE internal system failure\r\n"))
			return -1;
		ci_cleanup(ci);
		return EOF;
	}

	done = 0;
	args = NULL;
	nfaultyresponses = 0;

	do {
		if (nfaultyresponses >= MAX_FAULTY_RESPONSES) {
			/* we have had just about it with this user */
			sleep(2);	/* avoid DOS attacks */
			if (ci_write(ci->tx, "* BYE [TRY RFC]\r\n")) {
				ci_cleanup(ci);
				return EOF;
			}
			done = 1;
			break;
		}

		if (ferror(ci->rx)) {
			trace(TRACE_ERROR,
			      "IMAPClientHandler(): error [%s] on read-stream\n",
			      strerror(errno));
			if (errno == EPIPE) {
				ci_cleanup(ci);
				return -1;	/* broken pipe */
			} else
				clearerr(ci->rx);
		}

		if (ferror(ci->tx)) {
			trace(TRACE_ERROR,
			      "IMAPClientHandler(): error [%s] on write-stream\n",
			      strerror(errno));
			if (errno == EPIPE) {
				ci_cleanup(ci);
				return -1;	/* broken pipe */
			} else
				clearerr(ci->tx);
		}

		alarm(ci->timeout);	/* install timeout handler */
		if (fgets(line, MAX_LINESIZE, ci->rx) == NULL) {
			ci_cleanup(ci);
			return -1;
		}
		alarm(0);	/* remove timeout handler */

		if (!ci->rx || !ci->tx) {
			/* if a timeout occured the streams will be closed & set to NULL */
			trace(TRACE_ERROR,
			      "IMAPClientHandler(): timeout occurred.");
			ci_cleanup(ci);
			return 1;
		}

		trace(TRACE_DEBUG,
		      "IMAPClientHandler(): line read for PID %d\n",
		      getpid());

		if (!checkchars(line)) {
			/* foute tekens ingetikt */
			if (ci_write(ci->tx,
				     "* BYE Input contains invalid "
				     "characters\r\n")) {
				ci_cleanup(ci);
				return EOF;
			}
			ci_cleanup(ci);
			return 1;
		}

		/* clarify data a little */
		cpy = &line[strlen(line)];
		cpy--;
		while (cpy >= line && (*cpy == '\r' || *cpy == '\n')) {
			*cpy = '\0';
			cpy--;
		}

//      clarify_data(line);

		trace(COMMAND_SHOW_LEVEL, "COMMAND: [%s]\n", line);

		if (!(*line)) {
			
			if (ci_write(ci->tx, "* BAD No tag specified\r\n")) {
				ci_cleanup(ci);
				return EOF;
			}
			nfaultyresponses++;
			continue;
		}
		
		/* read tag & command */
		cpy = line;

		i = stridx(cpy, ' ');	/* find next space */
		if (i == strlen(cpy)) {
			if (checktag(cpy)) {
				if (ci_write(ci->tx,
					     "%s BAD No command specified\r\n",
					     cpy)) {
					ci_cleanup(ci);
					return EOF;
				}
			} else {
				if (ci_write(ci->tx,
					     "* BAD Invalid tag specified\r\n")) {
					ci_cleanup(ci);
					return EOF;
				}
			}
			nfaultyresponses++;
			continue;
			
		}
		
		tag = cpy;	/* set tag */
		cpy[i] = '\0';
		cpy = cpy + i + 1;	/* cpy points to command now */

		/* check tag */
		if (!checktag(tag)) {

			if (ci_write(ci->tx, 
				     "* BAD Invalid tag specified\r\n")) {
				ci_cleanup(ci);
				return EOF;
			}
			nfaultyresponses++;
			continue;
		}

		command = cpy;	/* set command */
		i = stridx(cpy, ' ');	/* find next space */
		if (i == strlen(cpy)) {
			/* no arguments present */
			args = build_args_array("");
		} else {
			cpy[i] = '\0';	/* terminated command */
			cpy = cpy + i + 1;	/* cpy points to args now */
			args = build_args_array_ext(cpy, ci);	/* build argument array */

			if (!ci->rx || !ci->tx || ferror(ci->rx)
			    || ferror(ci->tx)) {
				/* some error occurred during the read of extra command info */
				trace(TRACE_ERROR,
				      "IMAPClientHandler(): error reading extra command info");
				ci_cleanup(ci);
				return -1;
			}
		}

		if (!args) {
			if (ci_write(ci->tx,
				     "%s BAD invalid argument specified\r\n",
				     tag)) {
				ci_cleanup(ci);
				return EOF;
			}
				
			nfaultyresponses++;
			continue;
		}

		for (i = IMAP_COMM_NONE;
		     i < IMAP_COMM_LAST
		     && strcasecmp(command, IMAP_COMMANDS[i]); i++);

		if (i <= IMAP_COMM_NONE || i >= IMAP_COMM_LAST) {
			/* unknown command */
			if (ci_write(ci->tx,
				     "%s BAD command not recognized\r\n",
				     tag)) {
				ci_cleanup(ci);
				return EOF;
			}
			nfaultyresponses++;

			/* free used memory */
			for (i = 0; args[i]; i++) {
				my_free(args[i]);
				args[i] = NULL;
			}

			continue;
		}

		/* reset the faulty responses counter. This is quick fix which
		 * is useful for programs that depend on sending faulty
		 * commands to the server, and checking the response. 
		 * (IB: 2004-08-23) */
		nfaultyresponses = 0;

		trace(TRACE_INFO,
		      "IMAPClientHandler(): Executing command %s...\n",
		      IMAP_COMMANDS[i]);

// dirty hack to bypass a NOOP problem: 
// unilateral server responses are not recognised by some clients 
// if they are after the OK response
		this_was_noop = 0;

		if (i != IMAP_COMM_NOOP)
			result =
			    (*imap_handler_functions[i]) (tag, args, ci);
		else {
			this_was_noop = 1;
			result = 0;
		}

		//result = (*imap_handler_functions[i])(tag, args, ci);
		if (result == -1) {
			trace(TRACE_ERROR,"%s,%s: command return with error [%s]",
					__FILE__, __FUNCTION__, IMAP_COMMANDS[i]);
			
			done = 1;	/* fatal error occurred, kick this user */
		}

		if (result == 1)
			nfaultyresponses++;	/* server returned BAD or NO response */

		if (result == 0 && i == IMAP_COMM_LOGOUT)
			done = 1;


		fflush(ci->tx);	/* write! */

		trace(TRACE_INFO,
		      "IMAPClientHandler(): Finished command %s\n",
		      IMAP_COMMANDS[i]);

		/* check if mailbox status has changed (notify client) */
		if (ud->state == IMAPCS_SELECTED) {
			if (i == IMAP_COMM_NOOP ||
			    i == IMAP_COMM_CHECK ||
			    i == IMAP_COMM_SELECT ||
			    i == IMAP_COMM_EXPUNGE) {
				/* update mailbox info */
				memset(&newmailbox, 0, sizeof(newmailbox));
				newmailbox.uid = ud->mailbox.uid;

				result = db_getmailbox(&newmailbox);
				if (result == -1) {
					if (ci_write(ci->tx,
						     "* BYE internal dbase "
						     "error\r\n")) {
						ci_cleanup(ci);
						return EOF;
					}
					trace(TRACE_ERROR,
					      "IMAPClientHandler(): could not "
					      "get mailbox info\n");
					ci_cleanup(ci);
					for (i = 0; args[i]; i++) {
						my_free(args[i]);
						args[i] = NULL;
					}

					return -1;
				}

				if (newmailbox.exists !=
				    ud->mailbox.exists) {
					if(ci_write(ci->tx, "* %u EXISTS\r\n",
						    newmailbox.exists)) {
						ci_cleanup(ci);
						return EOF;
					}
					trace(TRACE_INFO,
					      "IMAPClientHandler(): ok update "
					      "sent\r\n");
				}

				if (newmailbox.recent !=
				    ud->mailbox.recent)
					if(ci_write(ci->tx, "* %u RECENT\r\n",
						    newmailbox.recent)) {
						ci_cleanup(ci);
						return EOF;
					}

				my_free(ud->mailbox.seq_list);
				memcpy((void *) &ud->mailbox, 
				       (void *)&newmailbox,
				       sizeof(newmailbox));
			}
		}
		if (this_was_noop) {
			if(ci_write(ci->tx, "%s OK NOOP completed\r\n", tag)) {
				ci_cleanup(ci);
				return EOF; 
			}
			trace(TRACE_DEBUG, "%s,%s: tag = %s", __FILE__, __func__,
			      tag);
		}
		for (i = 0; args[i]; i++) {
			my_free(args[i]);
			args[i] = NULL;
		}

	} while (!done);

	/* cleanup */
	if (ci_write(ci->tx, "%s OK completed\r\n", tag)) {
		ci_cleanup(ci);
		return EOF;
	}
	ci_cleanup(ci);
	trace(TRACE_MESSAGE,
	      "IMAPClientHandler(): Closing connection for client from IP [%s]\n",
	      ci->ip);

	__debug_dumpallocs();

	return EOF;
}
