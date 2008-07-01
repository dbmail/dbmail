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

/* 
 * imap4.c
 *
 * implements an IMAP 4 rev 1 server.
 */

#include "dbmail.h"

#define COMMAND_SHOW_LEVEL TRACE_INFO

#define THIS_MODULE "imap"

const char *IMAP_COMMANDS[] = {
	"", "capability", "noop", "logout",
	"authenticate", "login",
	"select", "examine", "create", "delete", "rename", "subscribe",
	"unsubscribe",
	"list", "lsub", "status", "append",
	"check", "close", "expunge", "search", "fetch", "store", "copy",
	"uid", "sort", "getquotaroot", "getquota",
	"setacl", "deleteacl", "getacl", "listrights", "myrights",
	"namespace","thread","unselect","idle",
	"***NOMORE***"
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
	_ic_namespace, _ic_thread, _ic_unselect, _ic_idle,
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
	char line[MAX_LINESIZE], *tag = NULL, *cpy, **args, *command;
	int done, result, readresult, nfaultyresponses, serr;
	size_t i;
	imap_userdata_t *ud = NULL;
	struct ImapSession *session;

	session = dbmail_imap_session_new();
	session->timeout = ci->login_timeout;
	dbmail_imap_session_setClientinfo(session,ci);

	if (! (ud = dbmail_imap_userdata_new()))
		return -1;
	
	session->ci->userData = ud;

	/* greet user */
	field_t banner;
	GETCONFIGVALUE("banner", "IMAP", banner);
	if (strlen(banner) > 0) {
		if (dbmail_imap_session_printf(session,
			     "* OK %s\r\n", banner) < 0) {
			dbmail_imap_session_delete(session);
			return EOF;
		}
	} else {
		if (dbmail_imap_session_printf(session,
			     "* OK dbmail imap (protocol version 4r1) server %s "
			     "ready to run\r\n", DBMAIL_VERSION) < 0) {
			dbmail_imap_session_delete(session);
			return EOF;
		}
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
			if (errno) {
				serr = errno;
				if (serr == EPIPE) {
					TRACE(TRACE_ERROR, "[%s] on read-stream, delete session\n", strerror(serr));
					dbmail_imap_session_delete(session);
					return -1;	/* broken pipe */
				} else {
					TRACE(TRACE_INFO, "[%s] on read-stream, clearerr and proceed\n", strerror(serr));
				} 
			}
			clearerr(session->ci->rx);
		}

		if (ferror(session->ci->tx)) {
			if (errno) {
				serr = errno;
				if (serr == EPIPE) {
					TRACE(TRACE_ERROR, "[%s] on write-stream, delete session\n", strerror(serr));
					dbmail_imap_session_delete(session);
					return -1;	/* broken pipe */
				} else {
					TRACE(TRACE_INFO, "[%s] on write-stream, clearerr and proceed\n", strerror(serr));
				}
			}
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
		session->command_type = i;

		TRACE(TRACE_INFO, "Executing command %s...\n", IMAP_COMMANDS[i]);

		result = (*imap_handler_functions[i]) (session);

		if (result == -1) {
			TRACE(TRACE_ERROR,"command return with error [%s]", IMAP_COMMANDS[i]);
			done = 1;	/* fatal error occurred, kick this user */
		}

		if (result == 1)
			nfaultyresponses++;	/* server returned BAD or NO response */

		if (result == 0 && i == IMAP_COMM_LOGOUT)
			done = 1;

		fflush(session->ci->tx);	/* write! */

		dbmail_imap_session_args_free(session, FALSE);

		TRACE(TRACE_INFO, "Finished command %s [%d]\n", IMAP_COMMANDS[i], result);

	} while (!done);

	/* cleanup */
	dbmail_imap_session_printf(session, "%s OK completed\r\n", session->tag);
	TRACE(TRACE_MESSAGE, "Closing connection for client from IP [%s]\n", session->ci->ip_src);
	dbmail_imap_session_delete(session);

	return EOF;
}
