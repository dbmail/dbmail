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
	"namespace","thread","unselect","idle",
	"***NOMORE***"
};

// async message queue for inter-thread communication
extern int selfpipe[2];
GAsyncQueue *queue;

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
	_ic_namespace, _ic_thread, _ic_unselect, _ic_idle,
	NULL
};


/*
 */

static int imap4_tokenizer(ImapSession *, char *);
static void imap4(ImapSession *);
static void dbmail_imap_session_reset(ImapSession *session);

static void imap_session_bailout(ImapSession *session)
{
	TRACE(TRACE_DEBUG,"[%p]", session);
	if (! session) return;
	ci_close(session->ci);
	dbmail_imap_session_delete(session);
}


void socket_error_cb(struct bufferevent *ev UNUSED, short what, void *arg)
{
	ImapSession *session = (ImapSession *)arg;
	int serr = errno;

	TRACE(TRACE_DEBUG,"[%p] state: [%d]", session, session->state);
	if (what & EVBUFFER_EOF) {
		TRACE(TRACE_INFO, "client disconnected. %s", strerror(serr));
		// defer the actual disconnetion to socket_write_cb so the server
		// will disconnect only when the write buffer is depleted and we're
		// done writing
	} else if (what & EVBUFFER_TIMEOUT) {
		TRACE(TRACE_INFO, "timeout");
		session->ci->cb_time(session);
	} else {
		TRACE(TRACE_INFO, "client socket error. %s", strerror(serr));
		imap_session_bailout(session);
	}
}

static void drain_queue(clientinfo_t *client UNUSED)
{
	gpointer data;
	TRACE(TRACE_DEBUG,"...");
	do {
		data = g_async_queue_try_pop(queue);
		if (data) {
			imap_cmd_t *ic = (gpointer)data;
			ic->cb_leave(data);
		}
	} while (data);

	TRACE(TRACE_DEBUG,"done");
}


void socket_write_cb(struct bufferevent *ev UNUSED, void *arg)
{

	ImapSession *session = (ImapSession *)arg;

	TRACE(TRACE_DEBUG,"[%p] state: [%d]", session, session->state);
	switch(session->state) {

		case IMAPCS_LOGOUT:
		case IMAPCS_ERROR:
			imap_session_bailout(session);
			break;

		default:
			TRACE(TRACE_DEBUG,"reset timeout [%d]", session->timeout);
			if (session->ci->rev) {
				bufferevent_settimeout(session->ci->rev, session->timeout, 0);
				bufferevent_enable(session->ci->rev, EV_READ);
			}
			break;
	}
}

void socket_read_cb(struct bufferevent *ev UNUSED, void *arg)
{
	ImapSession *session = (ImapSession *)arg;
	C c;

	TRACE(TRACE_DEBUG,"[%p] state: [%d]", session, session->state);
	c = db_con_get();
	if (!db_check_connection(c)) {
		db_con_close(c);
		TRACE(TRACE_DEBUG,"database has gone away");
		dbmail_imap_session_set_state(session,IMAPCS_ERROR);
		return;
	}
	db_con_close(c);

	if (session->error_count >= MAX_FAULTY_RESPONSES) {
		/* we have had just about it with this user */
		sleep(2);	/* avoid DOS attacks */
		dbmail_imap_session_printf(session, "* BYE [TRY RFC]\r\n");
		dbmail_imap_session_set_state(session,IMAPCS_ERROR);
		return;
	}
	session->ci->cb_read(session);
	
}

static void send_greeting(ImapSession *session)
{
	/* greet user */
	field_t banner;
	GETCONFIGVALUE("banner", "IMAP", banner);
	if (strlen(banner) > 0)
		dbmail_imap_session_printf(session, "* OK %s\r\n", banner);
	else
		dbmail_imap_session_printf(session, "* OK dbmail imap (protocol version 4r1) server %s ready to run\r\n", VERSION);
	dbmail_imap_session_set_state(session,IMAPCS_NON_AUTHENTICATED);
}

/*
 * the default timeout callback */
void imap_cb_time(void *arg)
{
	ImapSession *session = (ImapSession *) arg;
	TRACE(TRACE_DEBUG,"[%p]", session);

	dbmail_imap_session_printf(session, "%s", IMAP_TIMEOUT_MSG);
	dbmail_imap_session_set_state(session,IMAPCS_ERROR);

	imap_session_bailout(session);
}

static int checktag(const char *s)
{
	int i;
	for (i = 0; s[i]; i++) {
		if (!strchr(AcceptedTagChars, s[i])) return 0;
	}
	return 1;
}

static size_t stridx(const char *s, char c)
{
	size_t i;
	for (i = 0; s[i] && s[i] != c; i++);
	return i;
}
void imap_cb_read(void *arg)
{
	ImapSession *session = (ImapSession *) arg;
	char buffer[MAX_LINESIZE];

	// we need to clear the session if the previous command was 
	// finished; completed or aborted
	if (session->command_state) dbmail_imap_session_reset(session);

	bufferevent_enable(session->ci->rev, EV_READ);
	while (ci_readln(session->ci, buffer)) { // drain input buffer else return to wait for more.
		if (imap4_tokenizer(session, buffer)) {
			imap4(session);
			TRACE(TRACE_DEBUG,"command state [%d]", session->command_state);
			if (! session->command_state) return; // unfinished command, new read callback
			dbmail_imap_session_reset(session);
		}
	}
}

void dbmail_imap_session_set_callbacks(ImapSession *session, void *r, void *t, int timeout)
{
	if (r) session->ci->cb_read = r;
	if (t) session->ci->cb_time = t;
	if (timeout>0) session->timeout = timeout;

	assert(session->ci->cb_read);
	assert(session->ci->cb_time);
	assert(session->timeout > 0);

	TRACE(TRACE_DEBUG,"session [%p], cb_read [%p], cb_time [%p], timeout [%d]", 
		session, session->ci->cb_read, session->ci->cb_time, session->timeout);

	UNBLOCK(session->ci->rx);
	UNBLOCK(session->ci->tx);

	bufferevent_enable(session->ci->rev, EV_READ );
	bufferevent_enable(session->ci->wev, EV_WRITE);
	bufferevent_settimeout(session->ci->rev, session->timeout, 0);
}

void dbmail_imap_session_reset_callbacks(ImapSession *session)
{
	dbmail_imap_session_set_callbacks(session, imap_cb_read, imap_cb_time, session->timeout);
}


int imap_handle_connection(client_sock *c)
{
	ImapSession *session;
	clientinfo_t *ci;

	if (c)
		ci = client_init(c->sock, c->caddr);
	else
		ci = client_init(0, NULL);

	queue = g_async_queue_new();

	session = dbmail_imap_session_new();
	session->timeout = ci->login_timeout;

	dbmail_imap_session_set_state(session, IMAPCS_NON_AUTHENTICATED);

	ci->rev = bufferevent_new(ci->rx, socket_read_cb, NULL, socket_error_cb, (void *)session);
	ci->wev = bufferevent_new(ci->tx, NULL, socket_write_cb, socket_error_cb, (void *)session);
	ci->cb_pipe = (void *)drain_queue;

	session->ci = ci;

	dbmail_imap_session_reset_callbacks(session);
	
	send_greeting(session);
	
	return EOF;
}

void dbmail_imap_session_reset(ImapSession *session)
{
	if (session->tag) {
		g_free(session->tag);
		session->tag = NULL;
	}
	if (session->command) {
		g_free(session->command);
		session->command = NULL;
	}
	session->command_state = FALSE;
	dbmail_imap_session_args_free(session, FALSE);
	session->rbuff = NULL;
	bufferevent_enable(session->ci->rev, EV_READ);
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

		if (strcmp(buffer,"\n")==0) return 0;

		session->parser_state = 0;
		TRACE(TRACE_INFO, "[%p] COMMAND: [%s]", session, buffer);

		// tag
		i = stridx(cpy, ' ');	/* find next space */
		if (i == strlen(cpy)) {
			if (checktag(cpy))
				dbmail_imap_session_printf(session, "%s BAD No command specified\r\n", cpy);
			else
				dbmail_imap_session_printf(session, "* BAD Invalid tag specified\r\n");
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
			dbmail_imap_session_printf(session, "* BAD Invalid tag specified\r\n");
			session->error_count++;
			session->command_state=TRUE;
			return 0;
		}

		// command
		i = stridx(cpy, ' ');	/* find next space */

		command = g_strndup(cpy,i);	/* set command */
		if (command[i-1] == '\n') command[i-1] = '\0';
		dbmail_imap_session_set_command(session,command);
		g_free(command);

		cpy = cpy + i;	/* cpy points to args now */

	}

	session->parser_state = build_args_array_ext(session, cpy);	/* build argument array */

	if (session->parser_state)
		TRACE(TRACE_DEBUG,"parser_state: [%d]", session->parser_state);

	return session->parser_state;
}


void imap4(ImapSession *session)
{
	// 
	// the parser/tokenizer is satisfied we're ready reading from the client
	// so now it's time to act upon the read input
	//
	time_t before, after;
	int result, elapsed = 0;
	int j = 0;
	
	if (session->command_state==TRUE) // did we receive a signal we're done already
		return;

	// whatever happens we're done with this command by default (IDLE being an exemption)
	session->command_state=TRUE;

	if (! (session->tag && session->command)) {
		TRACE(TRACE_ERROR,"no tag or command");
		return;
	}

	if (! session->args) {
		dbmail_imap_session_printf(session, "%s BAD invalid argument specified\r\n",session->tag);
		session->error_count++;
		return;
	}

	session->error_count = 0;

	/* lookup and execute the command */

	for (j = IMAP_COMM_NONE; j < IMAP_COMM_LAST && strcasecmp(session->command, IMAP_COMMANDS[j]); j++);
	if (j <= IMAP_COMM_NONE || j >= IMAP_COMM_LAST) {
		/* unknown command */
		dbmail_imap_session_printf(session, "%s BAD command not recognized\r\n",session->tag);
		session->error_count++;
		return;
	}
	session->command_type = j;
	TRACE(TRACE_INFO, "Executing command %s...\n", IMAP_COMMANDS[session->command_type]);
	before = time(NULL);
	result = (*imap_handler_functions[session->command_type]) (session);
	after = time(NULL);
	
	if (result == -1) {
		TRACE(TRACE_ERROR,"command return with error [%s]", session->command);
		dbmail_imap_session_set_state(session,IMAPCS_ERROR);	/* fatal error occurred, kick this user */
	}
	if (result == 1)
		session->error_count++;	/* server returned BAD or NO response */

	if (result == 0) {
		switch(session->command_type) {
			case IMAP_COMM_LOGOUT:
				dbmail_imap_session_set_state(session,IMAPCS_LOGOUT);
			break;
			case IMAP_COMM_IDLE:
				session->command_state=FALSE;
				return;
			break;
		}
	}

	if (before != (time_t)-1 && after != (time_t)-1)
		elapsed = (int)((time_t) (after - before));

	TRACE(TRACE_INFO, "Finished %s in [%d] seconds [%d]\n", session->command, elapsed, result);
	return; //done
}
