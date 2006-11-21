/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2006 NFG Net Facilities Group BV support@nfg.nl

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

/* $Id: imap4.c 2372 2006-11-16 11:21:49Z paul $
 * imap4.c
 *
 * implements an IMAP 4 rev 1 server.
 */

#include "dbmail.h"

#define MAX_LINESIZE (10*1024)
#define COMMAND_SHOW_LEVEL TRACE_INFO

#define THIS_MODULE "imap"

/* cache */
cache_t cached_msg;

const char *IMAP_COMMANDS[] = {
	"", "capability", "noop", "logout",
	"authenticate", "login",
	"select", "examine", "create", "delete", "rename", "subscribe",
	"unsubscribe",
	"list", "lsub", "status", "append",
	"check", "close", "expunge", "search", "fetch", "store", "copy",
	"uid", "sort", "getquotaroot", "getquota",
	"setacl", "deleteacl", "getacl", "listrights", "myrights",
	"namespace","thread","unselect",
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
	IMAP_COMM_NAMESPACE, IMAP_COMM_THREAD, IMAP_COMM_UNSELECT,
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
	_ic_namespace, _ic_thread, _ic_unselect,
	NULL
};


imap_userdata_t * dbmail_imap_userdata_new(void)
{
	imap_userdata_t *ud;
	ud = g_new0(imap_userdata_t,1);
	ud->state = IMAPCS_NON_AUTHENTICATED;
	return ud;
}

/*
 * Main handling procedure
 *
 * returns EOF on logout/fatal error or 1 otherwise
 */
int IMAPClientHandler(clientinfo_t * ci)
{
	char line[MAX_LINESIZE];
	char *tag = NULL, *cpy, **args, *command;
	int done, result, readresult;
	size_t i;
	int nfaultyresponses;
	imap_userdata_t *ud = NULL;
	mailbox_t newmailbox;
	int this_was_noop = 0;
	
	struct ImapSession *session = dbmail_imap_session_new();
	dbmail_imap_session_setClientinfo(session,ci);

	if (! (ud = dbmail_imap_userdata_new()))
		return -1;
	
	session->ci->userData = ud;

	/* greet user */
	if (dbmail_imap_session_printf(session,
		     "* OK dbmail imap (protocol version 4r1) server %s "
		     "ready to run\r\n", IMAP_SERVER_VERSION) < 0) {
		dbmail_imap_session_delete(session);
		return EOF;
	}
	fflush(session->ci->tx);

	done = 0;
	args = NULL;
	nfaultyresponses = 0;

	do {
		if (db_check_connection()) {
			TRACE(TRACE_DEBUG,"database has gone away");
			done=1;
			break;
		}
		
		if (nfaultyresponses >= MAX_FAULTY_RESPONSES) {
			/* we have had just about it with this user */
			sleep(2);	/* avoid DOS attacks */
			if (dbmail_imap_session_printf(session, "* BYE [TRY RFC]\r\n") < 0) {
				dbmail_imap_session_delete(session);
				return EOF;
			}
			done = 1;
			break;
		}

		if (ferror(session->ci->rx)) {
			TRACE(TRACE_ERROR, "error [%s] on read-stream\n", strerror(errno));
			if (errno == EPIPE) {
				dbmail_imap_session_delete(session);
				return -1;	/* broken pipe */
			} else
				clearerr(session->ci->rx);
		}

		if (ferror(session->ci->tx)) {
			TRACE(TRACE_ERROR, "error [%s] on write-stream\n", strerror(errno));
			if (errno == EPIPE) {
				dbmail_imap_session_delete(session);
				return -1;	/* broken pipe */
			} else
				clearerr(session->ci->tx);
		}

		readresult = dbmail_imap_session_readln(session, line);
		if (readresult < 0) { /* Fatal error: EOF &c. */
			dbmail_imap_session_delete(session);
			return -1;
		}

		/* There is an error possible (line too long, readresult == 0)
		 * that we need to note, but carry through with processing the
		 * command in order to generate the proper tag on the
		 * BAD response */

		if (!session->ci->rx || !session->ci->tx) {
			/* if a timeout occured the streams will be closed & set to NULL */
			TRACE(TRACE_ERROR, "timeout occurred.");
			dbmail_imap_session_delete(session);
			return 1;
		}

		if (!checkchars(line)) {
			dbmail_imap_session_printf(session, "* BYE Input contains invalid characters\r\n");
			dbmail_imap_session_delete(session);
			return 1;
		}

		/* strip eol chars */
		cpy = &line[strlen(line)];
		cpy--;

		while (cpy >= line && (*cpy == '\r' || *cpy == '\n')) {
			*cpy = '\0';
			cpy--;
		}

		TRACE(COMMAND_SHOW_LEVEL, "COMMAND: [%s]\n", line);

		if (!(*line)) {
			
			if (dbmail_imap_session_printf(session, "* BAD No tag specified\r\n") < 0) {
				dbmail_imap_session_delete(session);
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
				if (dbmail_imap_session_printf(session, "%s BAD No command specified\r\n", cpy) < 0) {
					dbmail_imap_session_delete(session);
					return EOF;
				}
			} else {
				if (dbmail_imap_session_printf(session, "* BAD Invalid tag specified\r\n") < 0) {
					dbmail_imap_session_delete(session);
					return EOF;
				}
			}
			nfaultyresponses++;
			continue;
			
		}
		
		tag = g_strdup(cpy);	/* set tag */
		tag[i] = '\0';
		dbmail_imap_session_setTag(session,tag);
		g_free(tag);
		
		cpy[i] = '\0';
		cpy = cpy + i + 1;	/* cpy points to command now */

		/* check tag */
		if (!checktag(session->tag)) {
			if (dbmail_imap_session_printf(session, "* BAD Invalid tag specified\r\n") < 0) {
				dbmail_imap_session_delete(session);
				return EOF;
			}
			nfaultyresponses++;
			continue;
		}

		if (readresult == 0) { /* Nonfatal error: too long line &c. */
			if (dbmail_imap_session_printf(session, "%s BAD Line too long\r\n",session->tag) < 0) {
				dbmail_imap_session_delete(session);
				return EOF;
			}
			nfaultyresponses++;
			continue;
		}

		i = stridx(cpy, ' ');	/* find next space */
		
		command = strdup(cpy);	/* set command */
		if (i < strlen(command))
			command[i]='\0';
		dbmail_imap_session_setCommand(session,command);
		g_free(command);
		
		if (i == strlen(cpy)) {
			/* no arguments present */
			args = build_args_array_ext(session, "");
		} else {
			cpy[i] = '\0';	/* terminated command */
			cpy = cpy + i + 1;	/* cpy points to args now */
			args = build_args_array_ext(session, cpy);	/* build argument array */

			if (!session->ci->rx || !session->ci->tx || ferror(session->ci->rx) || ferror(session->ci->tx)) {
				/* some error occurred during the read of extra command info */
				TRACE(TRACE_ERROR, "error reading extra command info");
				dbmail_imap_session_delete(session);
				return -1;
			}
		}

		if (!args) {
			if (dbmail_imap_session_printf(session, "%s BAD invalid argument specified\r\n",session->tag) < 0) {
				dbmail_imap_session_delete(session);
				return EOF;
			}
				
			nfaultyresponses++;
			continue;
		}
		dbmail_imap_session_setArgs(session,args);

		for (i = IMAP_COMM_NONE; i < IMAP_COMM_LAST && strcasecmp(session->command, IMAP_COMMANDS[i]); i++);

		if (i <= IMAP_COMM_NONE || i >= IMAP_COMM_LAST) {
			/* unknown command */
			if (dbmail_imap_session_printf(session, "%s BAD command not recognized\r\n",session->tag)) {
				dbmail_imap_session_delete(session);
				return EOF;
			}
			nfaultyresponses++;
			continue;
		}

		/* reset the faulty responses counter. This is quick fix which
		 * is useful for programs that depend on sending faulty
		 * commands to the server, and checking the response. 
		 * (IB: 2004-08-23) */
		nfaultyresponses = 0;

		TRACE(TRACE_INFO, "Executing command %s...\n", IMAP_COMMANDS[i]);

// dirty hack to bypass a NOOP problem: 
// unilateral server responses are not recognised by some clients 
// if they are after the OK response
		this_was_noop = 0;

		if (i != IMAP_COMM_NOOP)
			result = (*imap_handler_functions[i]) (session);
		else {
			this_was_noop = 1;
			result = 0;
		}

		if (result == -1) {
			TRACE(TRACE_ERROR,"command return with error [%s]", IMAP_COMMANDS[i]);
			done = 1;	/* fatal error occurred, kick this user */
		}

		if (result == 1)
			nfaultyresponses++;	/* server returned BAD or NO response */

		if (result == 0 && i == IMAP_COMM_LOGOUT)
			done = 1;


		fflush(session->ci->tx);	/* write! */

		TRACE(TRACE_INFO, "Finished command %s [%d]\n", IMAP_COMMANDS[i], result);

		/* check if mailbox status has changed (notify client) */
		if (ud->state == IMAPCS_SELECTED) {
			//if (i == IMAP_COMM_NOOP || i == IMAP_COMM_CHECK || i == IMAP_COMM_SELECT || i == IMAP_COMM_EXPUNGE) {
			if (i == IMAP_COMM_NOOP || i == IMAP_COMM_CHECK || i == IMAP_COMM_EXPUNGE) {
				/* update mailbox info */
				memset(&newmailbox, 0, sizeof(newmailbox));
				newmailbox.uid = ud->mailbox.uid;

				result = db_getmailbox(&newmailbox);
				if (result == -1) {
					TRACE(TRACE_ERROR, "could not get mailbox info\n");
					dbmail_imap_session_printf(session, "* BYE internal dbase error\r\n");
					dbmail_imap_session_delete(session);
					return -1;
				}

				if (newmailbox.exists != ud->mailbox.exists) {
					if(dbmail_imap_session_printf(session, "* %u EXISTS\r\n", newmailbox.exists) < 0) {
						dbmail_imap_session_delete(session);
						return EOF;
					}
					TRACE(TRACE_INFO, "ok update sent\r\n");
				}

				if (newmailbox.recent != ud->mailbox.recent)
					if(dbmail_imap_session_printf(session, "* %u RECENT\r\n", newmailbox.recent) < 0) {
						dbmail_imap_session_delete(session);
						return EOF;
					}

				dm_free(ud->mailbox.seq_list);
				memcpy((void *) &ud->mailbox, (void *)&newmailbox, sizeof(newmailbox));
			}
		}
		if (this_was_noop) {
			if(dbmail_imap_session_printf(session, "%s OK NOOP completed\r\n", session->tag) < 0) {
				dbmail_imap_session_delete(session);
				return EOF; 
			}
			TRACE(TRACE_DEBUG, "tag = %s", session->tag);
		}

	} while (!done);

	/* cleanup */
	dbmail_imap_session_printf(session, "%s OK completed\r\n", session->tag);
	TRACE(TRACE_MESSAGE, "Closing connection for client from IP [%s]\n", session->ci->ip_src);
	dbmail_imap_session_delete(session);

	return EOF;
}
