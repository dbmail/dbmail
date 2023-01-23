/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

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

extern ServerConfig_T *server_conf;
extern GAsyncQueue *queue;
extern struct event_base *evbase;

const char AcceptedTagChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "!@#$%^&-=_`~\\|'\" ;:,.<>/? ";

const char *IMAP_COMMANDS[] = {
	"",
       	"capability",
       	"noop",
       	"logout",
	"authenticate",
       	"login",
	"select",
       	"examine",
	"enable",
       	"create",
       	"delete",
       	"rename",
       	"subscribe",
	"unsubscribe",
	"list",
       	"lsub",
       	"status",
       	"append",
	"check",
       	"close",
       	"expunge",
       	"search",
       	"fetch",
       	"store",
       	"copy",
	"uid",
       	"sort",
       	"getquotaroot",
       	"getquota",
	"setacl",
       	"deleteacl",
       	"getacl",
       	"listrights",
       	"myrights",
	"namespace",
	"thread",
	"unselect",
	"idle",
	"starttls",
       	"id",
	"***NOMORE***"
};

const IMAP_COMMAND_HANDLER imap_handler_functions[] = {
	NULL,
	_ic_capability, 
	_ic_noop,
       	_ic_logout,
	_ic_authenticate,
       	_ic_login,
	_ic_select,
       	_ic_examine,
	_ic_enable,
       	_ic_create,
       	_ic_delete,
       	_ic_rename,
	_ic_subscribe,
       	_ic_unsubscribe,
       	_ic_list,
       	_ic_lsub,
       	_ic_status,
	_ic_append,
	_ic_check,
       	_ic_close,
       	_ic_expunge,
       	_ic_search,
       	_ic_fetch,
	_ic_store,
       	_ic_copy,
       	_ic_uid,
       	_ic_sort,
	_ic_getquotaroot,
       	_ic_getquota,
	_ic_setacl,
       	_ic_deleteacl,
       	_ic_getacl,
       	_ic_listrights,
	_ic_myrights,
	_ic_namespace,
       	_ic_thread,
       	_ic_unselect,
       	_ic_idle,
       	_ic_starttls,
	_ic_id,
	NULL
};

/*
 */

static int imap4_tokenizer(ImapSession *, char *);
static int imap4(ImapSession *);
static void imap_handle_input(ImapSession *);
static void imap_handle_abort(ImapSession *);

#define DEFERRED_MAX_LOOP 100

void imap_cleanup_deferred(gpointer data)
{
	int rx;
	dm_thread_data *D = (dm_thread_data *)data;
	ImapSession *session = (ImapSession *)D->session;
	ClientBase_T *ci = session->ci;

	ci->deferred++;

	if (ci->rev) event_del(ci->rev);
	if (ci_wbuf_len(ci) && (! (ci->client_state & CLIENT_ERR)) && (ci->deferred < DEFERRED_MAX_LOOP)) {
		ci_write_cb(ci);
		dm_queue_push(imap_cleanup_deferred, session, NULL);
		return;
	}
	if (ci->deferred >= DEFERRED_MAX_LOOP) {
		TRACE(TRACE_DEBUG, "[%p] DEFERRED_MAX_LOOP reached; cleanup session", ci);
	}

	rx = ci->rx;
	ci_close(ci);
	ci = NULL;

	dbmail_imap_session_delete(&session);

	if (rx == STDIN_FILENO)
		exit(0);
}


static void imap_session_bailout(ImapSession *session)
{
	TRACE(TRACE_DEBUG,"[%p] state [%d] ci[%p]", session, session->state, session->ci);

	if (! dbmail_imap_session_set_state(session, CLIENTSTATE_QUIT_QUEUED)) {
		assert(session && session->ci);
		dm_queue_push(imap_cleanup_deferred, session, NULL);
	}
}

#ifdef DEBUG
void socket_write_cb(int fd, short what, void *arg)
#else
void socket_write_cb(int UNUSED fd, short UNUSED what, void *arg)
#endif
{
	ImapSession *session = (ImapSession *)arg;
	ClientState_T state;
	PLOCK(session->lock);
	state = session->state;
	PUNLOCK(session->lock);
#ifdef DEBUG
	TRACE(TRACE_DEBUG,"[%p] on [%d] state [%d] event:%s%s%s%s", session,
			(int) fd, state,
			(what&EV_TIMEOUT) ? " timeout": "",
			(what&EV_READ)    ? " read":    "",
			(what&EV_WRITE)   ? " write":   "",
			(what&EV_SIGNAL)  ? " signal":  ""
	     );
#endif
	switch(state) {
		case CLIENTSTATE_QUIT_QUEUED:
			break; // ignore
		case CLIENTSTATE_LOGOUT:
			imap_session_bailout(session);
			break;
		case CLIENTSTATE_ERROR:
			imap_handle_abort(session);
			break;
		default:
			ci_write_cb(session->ci);
			break;
	}
	dm_queue_drain();
}

void imap_cb_read(void *arg)
{
	ImapSession *session = (ImapSession *) arg;

	ci_read_cb(session->ci);

	uint64_t have = p_string_len(session->ci->read_buffer);
	uint64_t need = session->ci->rbuff_size;
	int enough = (need>0?(have >= need):(have > 0));
	int state;
       
	PLOCK(session->ci->lock);
	state = session->ci->client_state;
	PUNLOCK(session->ci->lock);

	TRACE(TRACE_DEBUG,"state [%d] enough %d: %" PRIu64 "/%" PRIu64 "", state, enough, have, need);

	if (state & CLIENT_ERR) {
		imap_session_bailout(session);
	} else if (state & CLIENT_EOF) {
		ci_cork(session->ci);
		if (enough)
			imap_handle_input(session);
		else
			imap_session_bailout(session);
	} else if (have > 0)
		imap_handle_input(session);
}

#ifdef DEBUG
void socket_read_cb(int fd, short what, void *arg)
#else
void socket_read_cb(int UNUSED fd, short what, void *arg)
#endif
{
	ImapSession *session = (ImapSession *)arg;
#ifdef DEBUG
	TRACE(TRACE_DEBUG,"[%p] on [%d] event: %s%s%s%s", session,
			(int) fd,
			(what&EV_TIMEOUT) ? " timeout": "",
			(what&EV_READ)    ? " read":    "",
			(what&EV_WRITE)   ? " write":   "",
			(what&EV_SIGNAL)  ? " signal":  ""
			);
#endif
	if (what == EV_READ)
		imap_cb_read(session);
	else if (what == EV_TIMEOUT && session->ci->cb_time)
		session->ci->cb_time(session);
	
	dm_queue_drain();
}


// helpers
//
static void imap_session_reset(ImapSession *session)
{
	ClientState_T current;

	memset(session->tag, 0, sizeof(session->tag));
	memset(session->command, 0, sizeof(session->command));

	session->use_uid = 0;
	session->command_type = 0;
	session->command_state = FALSE;
	session->parser_state = FALSE;
	dbmail_imap_session_args_free(session, FALSE);

	PLOCK(session->lock);
	current = session->state;
	PUNLOCK(session->lock);

	switch (current) {
		case CLIENTSTATE_AUTHENTICATED:
		case CLIENTSTATE_SELECTED:
			session->ci->timeout.tv_sec = server_conf->timeout; 
			break;
		default:
			session->ci->timeout.tv_sec = server_conf->login_timeout; 
			break;
	}

	TRACE(TRACE_DEBUG,"[%p] state [%d] timeout [%lu]", 
            session, current, session->ci->timeout.tv_sec);

	ci_uncork(session->ci);
	
	return;
}


/* 
 * only the main thread may write to the network event
 * worker threads must use an async queue
 */
static int64_t imap_session_printf(ImapSession * session, char * message, ...)
{
        va_list ap, cp;
        uint64_t l;
	int e = 0;

	p_string_truncate(session->buff, 0);

        assert(message);
        va_start(ap, message);
	va_copy(cp, ap);
        p_string_append_vprintf(session->buff, message, cp);
        va_end(cp);
        va_end(ap);

	if ((e = ci_write(session->ci, (char *)p_string_str(session->buff))) < 0) {
		TRACE(TRACE_DEBUG, "ci_write failed [%d]", e);
		imap_handle_abort(session);
		return e;
	}

        l = p_string_len(session->buff);
	p_string_truncate(session->buff, 0);

        return (int64_t)l;
}

static void send_greeting(ImapSession *session)
{
	/* greet user */
	Field_T banner;
	GETCONFIGVALUE("banner", "IMAP", banner);
	if (strlen(banner) > 0)
		imap_session_printf(session, "* OK [CAPABILITY %s] %s\r\n",
			       	Capa_as_string(session->preauth_capa), banner);
	else
		imap_session_printf(session, "* OK [CAPABILITY %s] dbmail %s ready.\r\n",
			       	Capa_as_string(session->preauth_capa), DM_VERSION);

	dbmail_imap_session_set_state(session, CLIENTSTATE_NON_AUTHENTICATED);
}

static void disconnect_user(ImapSession *session)
{
	imap_session_printf(session, "* BYE [Service unavailable.]\r\n");
	imap_handle_abort(session);
}

/*
 * the default timeout callback */

void imap_cb_time(void *arg)
{
	Field_T interval;
	int idle_interval = 10;
	ImapSession *session = (ImapSession *) arg;
	TRACE(TRACE_DEBUG,"[%p]", session);

	if ( session->command_type == IMAP_COMM_IDLE  && session->command_state == IDLE ) {
	       	// session is in a IDLE loop
		GETCONFIGVALUE("idle_interval", "IMAP", interval);
		if (strlen(interval) > 0) {
			int i = atoi(interval);
			if (i > 0 && i < 1000)
				idle_interval = i;
		}

		ci_cork(session->ci);
		if (! (++session->loop % idle_interval)) {
			imap_session_printf(session, "* OK Still here\r\n");
		}
		dbmail_imap_session_mailbox_status(session,TRUE);
		dbmail_imap_session_buff_flush(session);
		ci_uncork(session->ci);
	} else {
		dbmail_imap_session_set_state(session,CLIENTSTATE_ERROR);
		imap_session_printf(session, "%s", IMAP_TIMEOUT_MSG);
		imap_session_bailout(session);
	}
}

static int checktag(const char *s)
{
	int i;
	for (i = 0; s[i]; i++) {
		if (!strchr(AcceptedTagChars, s[i]))
		       	return 0;
	}
	return 1;
}

void imap_handle_abort(ImapSession *session)
{
	if (! dbmail_imap_session_set_state(session, CLIENTSTATE_ERROR)) {	/* fatal error occurred, kick this user */
		imap_session_reset(session);
		imap_session_bailout(session);
	}
}

static void imap_handle_continue(ImapSession *session)
{
	if (session->state < CLIENTSTATE_LOGOUT) {
		if (session->buff && p_string_len(session->buff) > 0) {
			int e = 0;
			if ((e = ci_write(session->ci, "%s", (char *)p_string_str(session->buff))) < 0) {
				int serr = errno;
				TRACE(TRACE_DEBUG,"ci_write returned error [%s]", strerror(serr));
				imap_handle_abort(session);
				return;
			}
			dbmail_imap_session_buff_clear(session);
		}
		if (p_string_len(session->ci->write_buffer) > session->ci->write_buffer_offset)
			ci_write(session->ci, NULL);
		if (session->command_state == TRUE)
			imap_session_reset(session);
	} else {
		dbmail_imap_session_buff_clear(session);
	}				

	// handle buffered pending input
	if (p_string_len(session->ci->read_buffer) > 0)
		imap_handle_input(session);
}

static void imap_handle_retry(ImapSession *session)
{
	if (session->ci->client_state & CLIENT_EOF) {
		imap_session_bailout(session);
		return;
	}

	session->command_state = TRUE;
	dbmail_imap_session_buff_flush(session);
	session->error_count++;	/* server returned BAD or NO response */
	imap_session_reset(session);
}

static void imap_handle_done(ImapSession *session)
{
	/* only do this in the main thread */
	imap_session_printf(session, "* BYE Requested\r\n");
	imap_session_printf(session, "%s OK LOGOUT completed\r\n", session->tag);
	imap_session_bailout(session);
}

static void imap_handle_exit(ImapSession *session, int status)
{
	if (! session) return;

	TRACE(TRACE_DEBUG, "[%p] state [%d] command_status [%d] [%s] returned with status [%d]", 
		session, session->state, session->command_state, session->command, status);

	switch(status) {
		case -1:
			imap_handle_abort(session);
			break;
		case 0:
			imap_handle_continue(session);
			break;
		case 1:
			imap_handle_retry(session);
			break;
		case 2:
			imap_handle_done(session);
			break;
		case 3: /* returning from starttls */
			imap_session_reset(session);
			break;
	}
}

#define FREE_ALLOC_BUF \
	if (alloc_buf != NULL) { \
		mempool_push(session->pool, alloc_buf, alloc_size); \
		alloc_buf = NULL; \
		alloc_size = 0; \
	}


void imap_handle_input(ImapSession *session)
{
	char buffer[MAX_LINESIZE];
	char *alloc_buf = NULL;
	uint64_t alloc_size = 0;
	int l, result;

	assert(session && session->ci && session->ci->write_buffer);

	// first flush the output buffer
	if (p_string_len(session->ci->write_buffer)) {
		TRACE(TRACE_DEBUG,"[%p] write buffer not empty", session);
		ci_write(session->ci, NULL);
	}

	// nothing left to handle
	if (p_string_len(session->ci->read_buffer) == 0) {
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
		imap_session_reset(session);


	// Read in a line at a time if we don't have a string literal size defined
	// Otherwise read in rbuff_size amount of data
	while (TRUE) {
		char *input = NULL;
		FREE_ALLOC_BUF
		memset(buffer, 0, sizeof(buffer));

		if (session->ci->rbuff_size <= 0) {
			l = ci_readln(session->ci, buffer);
		} else {
			alloc_size = session->ci->rbuff_size+1;
			alloc_buf = mempool_pop(session->pool, alloc_size);
			l = ci_read(session->ci, alloc_buf, session->ci->rbuff_size);
		}

		if (l == 0) break; // done

		if (session->error_count >= MAX_FAULTY_RESPONSES) {
			imap_session_printf(session, "* BYE [TRY RFC]\r\n");
			imap_handle_abort(session);
			break;
		}

		// session is in a IDLE loop
		if (session->command_type == IMAP_COMM_IDLE  && session->command_state == IDLE) { 
			if (strlen(buffer) > 4 && strncasecmp(buffer,"DONE",4)==0)
				imap_session_printf(session, "%s OK IDLE terminated\r\n", session->tag);
			else
				imap_session_printf(session,"%s BAD Expecting DONE\r\n", session->tag);

			session->command_state = TRUE; // done
			imap_session_reset(session);

			continue;
		}

		if (alloc_buf != NULL)
			input = alloc_buf;
		else
			input = buffer;

		if (! imap4_tokenizer(session, input)) {
			FREE_ALLOC_BUF
			continue;
		}

		if ( session->parser_state < 0 ) {
			imap_session_printf(session, "%s BAD parse error\r\n", session->tag);
			imap_handle_retry(session);
			break;
		}

		if ( session->parser_state ) {
			result = imap4(session);
			TRACE(TRACE_DEBUG,"imap4 returned [%d]", result);
			if (result || (session->command_type == IMAP_COMM_IDLE && session->command_state == IDLE)) { 
				imap_handle_exit(session, result);
			}
			break;
		}

		if (session->state == CLIENTSTATE_ERROR) {
			TRACE(TRACE_NOTICE, "session->state: ERROR. abort");
			break;
		}
	}

	FREE_ALLOC_BUF

	return;
}

static void reset_callbacks(ImapSession *session)
{
	session->ci->cb_time = imap_cb_time;
	session->ci->timeout.tv_sec = server_conf->login_timeout;

	UNBLOCK(session->ci->rx);
	UNBLOCK(session->ci->tx);
	ci_uncork(session->ci);
}

int imap_handle_connection(client_sock *c)
{
	ImapSession *session;
	ClientBase_T *ci;
	struct rlimit fd_limit;
	int fd_count;

	ci = client_init(c);

	session = dbmail_imap_session_new(c->pool);

	assert(evbase);
	ci->rev = event_new(evbase, ci->rx, EV_READ|EV_PERSIST, socket_read_cb, (void *)session);
	ci->wev = event_new(evbase, ci->tx, EV_WRITE, socket_write_cb, (void *)session);
	ci_cork(ci);

	session->ci = ci;
	if ((! server_conf->ssl) || (ci->sock->ssl_state == TRUE)) {
		Capa_remove(session->capa, "STARTTLS");
		Capa_remove(session->capa, "LOGINDISABLED");
	}

	fd_count = get_opened_fd_count();
	if (fd_count < 0 || getrlimit(RLIMIT_NPROC, &fd_limit) < 0) {
		TRACE(TRACE_ERR,
			"[%p] failed to retrieve fd limits, dropping client connection",
			session);
		disconnect_user(session);
	} else if (fd_limit.rlim_cur - fd_count < FREE_DF_THRESHOLD) {
		TRACE(TRACE_WARNING,
			"[%p] fd count [%d], fd limit [%ld], fd threshold [%d]: dropping client connection",
			session, fd_count, fd_limit.rlim_cur, FREE_DF_THRESHOLD);
		disconnect_user(session);
	} else {
		send_greeting(session);
	}

	reset_callbacks(session);

	return EOF;
}

int imap4_tokenizer (ImapSession *session, char *buffer)
{
	char *cpy;
	uint64_t i = 0;
		
	if (!(*buffer))
		return 0;

	/* read tag & command */
	cpy = buffer;

	/* fetch the tag and command */
	if (! *session->tag) {

		if (strncmp(buffer, "\n", 1)==0 || strncmp(buffer, "\r\n", 2)==0)
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

		strncpy(session->tag, cpy, min(i, sizeof(session->tag)));

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

		strncpy(session->command, cpy, min(i, sizeof(session->command)));
		strip_crlf(session->command);

		cpy = cpy + i;	/* cpy points to args now */

	}

	session->parser_state = imap4_tokenizer_main(session, cpy);	/* build argument array */

	if (session->parser_state)
		TRACE(TRACE_DEBUG,"parser_state: [%d]", session->parser_state);

	return session->parser_state;
}
	
void _ic_cb_leave(gpointer data)
{
	int state;
	dm_thread_data *D = (dm_thread_data *)data;
	ImapSession *session = D->session;

	PLOCK(session->ci->lock);
	state = session->ci->client_state;
	PUNLOCK(session->ci->lock);

	TRACE(TRACE_DEBUG,"handling imap session [%p] client_state [%s%s]",
			session,
		       	(state&CLIENT_ERR)?" error":"",
			(state&CLIENT_EOF)?" eof":""
			);

	if (state & CLIENT_ERR) {
		imap_handle_abort(session);
		return;
	} 

	ci_uncork(session->ci);
	imap_handle_exit(session, D->status);
}

static void imap_unescape_args(ImapSession *session)
{
	uint64_t i = 0;
	assert(session->command_type);
	switch (session->command_type) {
		case IMAP_COMM_EXAMINE:
		case IMAP_COMM_SELECT:
		case IMAP_COMM_SEARCH:
		case IMAP_COMM_CREATE:
		case IMAP_COMM_DELETE:
		case IMAP_COMM_RENAME:
		case IMAP_COMM_SUBSCRIBE:
		case IMAP_COMM_UNSUBSCRIBE:
		case IMAP_COMM_STATUS:
		case IMAP_COMM_COPY:
		case IMAP_COMM_LOGIN:

		for (i = 0; session->args[i]; i++) { 
			p_string_unescape(session->args[i]);
		}
		break;
		default:
		break;
	}
#ifdef DEBUG
	for (i = 0; session->args[i]; i++) { 
		TRACE(TRACE_DEBUG, "[%p] arg[%" PRIu64 "]: '%s'\n", session, i, p_string_str(session->args[i])); 
	}
#endif

}


int imap4(ImapSession *session)
{
	// 
	// the parser/tokenizer is satisfied we're ready reading from the client
	// so now it's time to act upon the read input
	//
	int j = 0;
	
	if (! dm_db_ping()) {
		imap_handle_abort(session);
		return DM_EQUERY;
	}

	if (session->command_state==TRUE) // done already
		return 0;

	session->command_state=TRUE; // set command-is-done-state while doing some checks
	if (! (session->tag[0] && session->command[0])) {
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

	imap_unescape_args(session);

	TRACE(TRACE_INFO, "dispatch [%s]...\n", IMAP_COMMANDS[session->command_type]);
	return (*imap_handler_functions[session->command_type]) (session);
}
