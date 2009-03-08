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

#define THIS_MODULE "imap"

/* max number of BAD/NO responses */
#define MAX_FAULTY_RESPONSES 5

const char *IMAP_COMMANDS[] = {
	"", "capability", "noop", "logout",
	"authenticate", "login",
	"select", "examine", "create", "delete", "rename", "subscribe",
	"unsubscribe",
	"list", "lsub", "status", "append",
	"check", "close", "expunge", "search", "fetch", "store", "copy",
	"uid", "sort", "getquotaroot", "getquota",
	"setacl", "deleteacl", "getacl", "listrights", "myrights",
	"namespace","thread","unselect","idle","starttls",
	"***NOMORE***"
};

extern serverConfig_t *server_conf;
extern GAsyncQueue *queue;

const char AcceptedTagChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "!@#$%^&-=_`~\\|'\" ;:,.<>/? ";


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
	_ic_namespace, _ic_thread, _ic_unselect, _ic_idle, _ic_starttls,
	NULL
};


/*
 */

static int imap4_tokenizer(ImapSession *, char *);
static int imap4(ImapSession *);
static void imap_handle_input(ImapSession *);

static void imap_session_bailout(ImapSession *session)
{
	// brute force:
	if (server_conf->no_daemonize == 1) _exit(0);

	assert(session && session->ci);
	TRACE(TRACE_DEBUG,"[%p] state [%d]", session, session->state);

	if ( session->command_type == IMAP_COMM_IDLE ) { // session is in a IDLE loop - need to exit the loop first
		TRACE(TRACE_DEBUG, "[%p] Session is in an idle loop, exiting loop.", session);
		dbmail_imap_session_set_state(session,IMAPCS_ERROR);
		session->command_state = FALSE;
		dm_thread_data *D = g_new0(dm_thread_data,1);
		D->data = (gpointer)"DONE\n\0";
		g_async_queue_push(session->ci->queue, (gpointer)D);
		return;
	}

	ci_close(session->ci);
	dbmail_imap_session_delete(session);
}

void socket_write_cb(int fd UNUSED, short what, void *arg)
{
	ImapSession *session = (ImapSession *)arg;

	TRACE(TRACE_DEBUG,"[%p] what [%d] state [%d] command_state [%d]", session, what, session->state, session->command_state);

	switch(session->state) {
		case IMAPCS_LOGOUT:
			event_del(session->ci->wev);
		case IMAPCS_ERROR:
			imap_session_bailout(session);
			break;
		default:
			if (session->ci->rev) {
				if ( session->command_type == IMAP_COMM_IDLE ) {
					if ( session->command_state == FALSE ) {
						// make _very_ sure this is done only once during an idle command run
						// only when the idle loop has just begun: just after we pushed the
						// continuation '+' to the client
						session->command_state = IDLE;
						event_add(session->ci->rev, NULL);
					} else if (session->command_state == TRUE)  { // IDLE is done
						event_add(session->ci->rev, session->ci->timeout);
					}
				}
			}
			
			ci_write_cb(session->ci);
			imap_handle_input(session);

			break;
	}
}

void imap_cb_read(void *arg)
{
	int state;
	ImapSession *session = (ImapSession *) arg;
	TRACE(TRACE_DEBUG,"reading...");

	ci_read_cb(session->ci);

	state = session->ci->client_state;
	if (state & CLIENT_OK || state & CLIENT_AGAIN) {
		imap_handle_input(session);
	} else if (state & CLIENT_ERR) {
		TRACE(TRACE_DEBUG,"client_state ERROR");
		dbmail_imap_session_set_state(session,IMAPCS_ERROR);
	} else if (state & CLIENT_EOF) {
		TRACE(TRACE_NOTICE,"reached EOF");
		event_del(session->ci->rev);
		if (session->ci->read_buffer->len < 1)
			imap_session_bailout(session);
	}
}


void socket_read_cb(int fd UNUSED, short what, void *arg)
{
	ImapSession *session = (ImapSession *)arg;
	TRACE(TRACE_DEBUG,"[%p] what [%d] state [%d] command_state [%d]", session, what, session->state, session->command_state);
	if (what == EV_READ)
		imap_cb_read(session);
	else if (what == EV_TIMEOUT && session->ci->cb_time)
		session->ci->cb_time(session);
	
}

/* 
 * only the main thread may write to the network event
 * worker threads must use an async queue
 */
static int imap_session_printf(ImapSession * self, char * message, ...)
{
        va_list ap, cp;
        size_t l;
	int e = 0;

        assert(message);
        va_start(ap, message);
	va_copy(cp, ap);
        g_string_vprintf(self->buff, message, cp);
        va_end(cp);

	if ((e = ci_write(self->ci, self->buff->str)) < 0) {
		TRACE(TRACE_DEBUG, "ci_write failed [%s]", strerror(e));
		dbmail_imap_session_set_state(self,IMAPCS_ERROR);
		return e;
	}

        l = self->buff->len;
	self->buff = g_string_truncate(self->buff, 0);
	g_string_maybe_shrink(self->buff);

        return (int)l;
}

static void send_greeting(ImapSession *session)
{
	/* greet user */
	field_t banner;
	GETCONFIGVALUE("banner", "IMAP", banner);
	if (strlen(banner) > 0)
		imap_session_printf(session, "* OK %s\r\n", banner);
	else
		imap_session_printf(session, "* OK imap 4r1 server (dbmail %s)\r\n", VERSION);
	dbmail_imap_session_set_state(session,IMAPCS_NON_AUTHENTICATED);
}

/*
 * the default timeout callback */
void imap_cb_time(void *arg)
{
	ImapSession *session = (ImapSession *) arg;
	TRACE(TRACE_DEBUG,"[%p]", session);

	dbmail_imap_session_set_state(session,IMAPCS_ERROR);
	imap_session_printf(session, "%s", IMAP_TIMEOUT_MSG);
}

static int checktag(const char *s)
{
	int i;
	for (i = 0; s[i]; i++) {
		if (!strchr(AcceptedTagChars, s[i])) return 0;
	}
	return 1;
}

static void imap_handle_exit(ImapSession *session, int status)
{
	TRACE(TRACE_DEBUG, "[%p] state [%d] command_status [%d] [%s] returned with status [%d]", 
		session, session->state, session->command_state, session->command, status);

	switch(status) {
		case -1:
			dbmail_imap_session_set_state(session,IMAPCS_ERROR);	/* fatal error occurred, kick this user */
			imap_session_bailout(session);
			break;

		case 0:
			/* only do this in the main thread */
			if (session->state < IMAPCS_LOGOUT) {
				if (session->buff) {
					int e = 0;
					if ((e = ci_write(session->ci, session->buff->str)) < 0) {
						TRACE(TRACE_DEBUG,"ci_write returned error [%s]", strerror(e));
						dbmail_imap_session_set_state(session,IMAPCS_ERROR);
						return;
					}
					dbmail_imap_session_buff_clear(session);
				}
				if ((session->ci->write_buffer->len - session->ci->write_buffer_offset) > 0) {
					TRACE(TRACE_DEBUG,"write_buffer size: %d", (session->ci->write_buffer->len - session->ci->write_buffer_offset));
					ci_write(session->ci, NULL);
				} else if (session->command_state == TRUE) {
					dbmail_imap_session_reset(session);
				}
			} else {
				dbmail_imap_session_buff_clear(session);
			}				
		
			imap_handle_input(session);

			break;
		case 1:
			session->command_state = TRUE;
			dbmail_imap_session_buff_flush(session);
			session->error_count++;	/* server returned BAD or NO response */
			dbmail_imap_session_reset(session);
			break;

		case 2:
			/* only do this in the main thread */
			imap_session_printf(session, "* BYE\r\n");
			imap_session_printf(session, "%s OK LOGOUT completed\r\n", session->tag);
			break;

		case 3:
			/* returning from starttls */
			dbmail_imap_session_reset(session);
			break;

	}
}

void imap_handle_input(ImapSession *session)
{
	char buffer[MAX_LINESIZE];
	int l, result;

	TRACE(TRACE_DEBUG, "[%p] parser_state [%d] command_state [%d]", session, session->parser_state, session->command_state);

	// first flush the output buffer
	if (session->ci->write_buffer->len) {
		TRACE(TRACE_DEBUG,"[%p] write buffer not empty", session);
		ci_write(session->ci, NULL);
		return;
	}

	// command in progress
	/*
	if ( session->command_state == FALSE && session->parser_state == FALSE) {
		TRACE(TRACE_DEBUG,"[%p] wait for data", session);
		event_add(session->ci->rev, session->ci->timeout);
	}
	*/

	// nothing left to handle
	if (session->ci->read_buffer->len == 0) {
		TRACE(TRACE_DEBUG,"[%p] read buffer empty", session);
		return;
	}

	// command in progress
	if (session->command_state == FALSE && session->parser_state == TRUE) {
		TRACE(TRACE_DEBUG,"[%p] command in-progress", session);
		return;
	}

	// reset if we're done with the previous command
	if (session->command_state == TRUE)
		dbmail_imap_session_reset(session);


	// Read in a line at a time if we don't have a string literal size defined
	// Otherwise read in rbuff_size amount of data
	while (TRUE) {

		memset(buffer, 0, sizeof(buffer));

		if (session->rbuff_size <= 0) {
			l = ci_readln(session->ci, buffer);
		} else {
			int needed = MIN(session->rbuff_size, (int)sizeof(buffer)-1);
			l = ci_read(session->ci, buffer, needed);
		}

		TRACE(TRACE_DEBUG,"[%p] ci_read(ln) returned [%d]", session, l);
		if (l == 0) break; // done

		if (session->error_count >= MAX_FAULTY_RESPONSES) {
			dbmail_imap_session_set_state(session,IMAPCS_ERROR);
			imap_session_printf(session, "* BYE [TRY RFC]\r\n");
			break;
		}

		if ( session->command_type == IMAP_COMM_IDLE ) { // session is in a IDLE loop
			TRACE(TRACE_DEBUG,"read [%s] while in IDLE loop", buffer);
			session->command_state = FALSE;
			dm_thread_data *D = g_new0(dm_thread_data,1);
			D->data = (gpointer)g_strdup(buffer);
			g_async_queue_push(session->ci->queue, (gpointer)D);
			break;
		}

		if (! imap4_tokenizer(session, buffer))
			continue;

		if ( session->parser_state < 0 ) {
			imap_session_printf(session, "%s BAD parse error\r\n", session->tag);
			imap_handle_exit(session, 1);
			break;
		}

		if ( session->parser_state ) {
			if ((result = imap4(session))) 
				imap_handle_exit(session, result);
			TRACE(TRACE_DEBUG,"imap4 returned [%d]", result);
			break;
		}

		if (session->state == IMAPCS_ERROR) {
			TRACE(TRACE_NOTICE, "session->state: ERROR. abort");
			break;
		}
	}

	return;
}
static void reset_callbacks(ImapSession *session)
{
	session->ci->cb_time = imap_cb_time;
	session->ci->timeout->tv_sec = server_conf->login_timeout;

	UNBLOCK(session->ci->rx);
	UNBLOCK(session->ci->tx);

	event_add(session->ci->rev, session->ci->timeout);
	event_add(session->ci->wev, NULL);
}

int imap_handle_connection(client_sock *c)
{
	ImapSession *session;
	clientbase_t *ci;

	if (c)
		ci = client_init(c->sock, c->caddr, c->ssl);
	else
		ci = client_init(0, NULL, NULL);

	session = dbmail_imap_session_new();

	dbmail_imap_session_set_state(session, IMAPCS_NON_AUTHENTICATED);

	event_set(ci->rev, ci->rx, EV_READ|EV_PERSIST, socket_read_cb, (void *)session);
	event_set(ci->wev, ci->tx, EV_WRITE, socket_write_cb, (void *)session);

	session->ci = ci;

	reset_callbacks(session);
	
	send_greeting(session);
	
	return EOF;
}

void dbmail_imap_session_reset(ImapSession *session)
{
	TRACE(TRACE_DEBUG,"[%p]", session);
	if (session->tag) {
		g_free(session->tag);
		session->tag = NULL;
	}

	if (session->command) {
		g_free(session->command);
		session->command = NULL;
	}

	session->use_uid = 0;
	session->command_type = 0;
	session->command_state = FALSE;
	session->parser_state = FALSE;
	dbmail_imap_session_args_free(session, FALSE);
	
	return;
}

int imap4_tokenizer (ImapSession *session, char *buffer)
{
	char *tag = NULL, *cpy, *command;
	size_t i = 0;
		
	if (!(*buffer))
		return 0;

	/* read tag & command */
	cpy = buffer;

	/* fetch the tag and command */
	if (! session->tag) {

		if (strcmp(buffer,"\n")==0 || strcmp(buffer,"\r\n")==0)
			return 0;

		session->parser_state = 0;
		TRACE(TRACE_INFO, "[%p] COMMAND: [%s]", session, buffer);

		// tag
		i = stridx(cpy, ' ');	/* find next space */
		if (i == strlen(cpy)) {
			if (checktag(cpy))
				imap_session_printf(session, "%s BAD No command specified\r\n", cpy);
			else
				imap_session_printf(session, "* BAD Invalid tag specified\r\n");
			session->error_count++;
			session->command_state=TRUE;
			return 0;
		}

		tag = g_strndup(cpy,i);	/* set tag */
		dbmail_imap_session_set_tag(session,tag);
		g_free(tag);

		cpy[i] = '\0';
		cpy = cpy + i + 1;	/* cpy points to command now */

		if (!checktag(session->tag)) {
			imap_session_printf(session, "* BAD Invalid tag specified\r\n");
			session->error_count++;
			session->command_state=TRUE;
			return 0;
		}

		// command
		i = stridx(cpy, ' ');	/* find next space */

		command = g_strndup(cpy,i);	/* set command */
		strip_crlf(command);
		dbmail_imap_session_set_command(session,command);
		g_free(command);

		cpy = cpy + i;	/* cpy points to args now */

	}

	session->parser_state = imap4_tokenizer_main(session, cpy);	/* build argument array */

	if (session->parser_state)
		TRACE(TRACE_DEBUG,"parser_state: [%d]", session->parser_state);

	return 1;
}
	
void _ic_cb_leave(gpointer data)
{
	dm_thread_data *D = (dm_thread_data *)data;
	ImapSession *session = D->session;
	imap_handle_exit(session, D->status);
}


int imap4(ImapSession *session)
{
	// 
	// the parser/tokenizer is satisfied we're ready reading from the client
	// so now it's time to act upon the read input
	//
	int j = 0;
	
	if (! dm_db_ping()) {
		dbmail_imap_session_set_state(session,IMAPCS_ERROR);
		return DM_EQUERY;
	}

	if (session->command_state==TRUE) // done already
		return 0;

	session->command_state=TRUE; // set command-is-done-state while doing some checks
	if (! (session->tag && session->command)) {
		TRACE(TRACE_ERR,"no tag or command");
		return 1;
	}
	if (! session->args) {
		imap_session_printf(session, "%s BAD invalid argument specified\r\n",session->tag);
		session->error_count++;
		return 1;
	}

	/* lookup the command */
	for (j = IMAP_COMM_NONE; j < IMAP_COMM_LAST && strcasecmp(session->command, IMAP_COMMANDS[j]); j++);

	if (j <= IMAP_COMM_NONE || j >= IMAP_COMM_LAST) { /* unknown command */
		imap_session_printf(session, "%s BAD no valid command\r\n", session->tag);
		return 1;
	}

	session->error_count = 0;
	session->command_type = j;
	session->command_state=FALSE; // unset command-is-done-state while command in progress

	TRACE(TRACE_INFO, "dispatch [%s]...\n", IMAP_COMMANDS[session->command_type]);
	return (*imap_handler_functions[session->command_type]) (session);
}
