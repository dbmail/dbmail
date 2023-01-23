/*
 Copyright (C) 2004 IC & S dbmail@ic-s.nl
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
 *
 * implementation for lmtp commands according to RFC 1081 */

#include "dbmail.h"

#define THIS_MODULE "clientsession"

extern ServerConfig_T *server_conf;
extern struct event_base *evbase;

ClientSession_T * client_session_new(client_sock *c)
{
	ClientBase_T *ci;
	Mempool_T pool = c->pool;

	char unique_id[UID_SIZE];

	ci = client_init(c);

	ClientSession_T * session = mempool_pop(pool, sizeof(ClientSession_T));

	session->state = CLIENTSTATE_INITIAL_CONNECT;
	session->pool = pool;
	session->args = p_list_new(pool);
	session->from = p_list_new(pool);
	session->rbuff = p_string_new(pool, "");
	session->messagelst = p_list_new(pool);

	gethostname(session->hostname, sizeof(session->hostname));

	memset(unique_id,0,sizeof(unique_id));
	create_unique_id(unique_id, 0);
	session->apop_stamp = g_strdup_printf("<%s@%s>", unique_id, session->hostname);

	assert(evbase);
        ci->rev = event_new(evbase, ci->rx, EV_READ|EV_PERSIST, socket_read_cb, (void *)session);
        ci->wev = event_new(evbase, ci->tx, EV_WRITE, socket_write_cb, (void *)session);
	ci_cork(ci);

	session->ci = ci;

	return session;
}

void client_session_reset(ClientSession_T * session)
{
	List_T from;

	if (session->rcpt)
		dsnuser_free_list(session->rcpt);

	session->rcpt = p_list_new(session->pool);

	if (session->from) {
		from = p_list_first(session->from);
		while (from) {
			String_T s = p_list_data(from);
			if (s) p_string_free(s, TRUE);
			from = p_list_next(from);
		}
		from = p_list_first(session->from);
		p_list_free(&from);
	}

	session->from = p_list_new(session->pool);

	if (session->apop_stamp) {
		g_free(session->apop_stamp);
		session->apop_stamp = NULL;
	}

	if (session->username) {
		g_free(session->username);
		session->username = NULL;
	}

	if (session->password) {
		g_free(session->password);
		session->password = NULL;
	}

	session->state = CLIENTSTATE_INITIAL_CONNECT;

	client_session_reset_parser(session);
}

void client_session_reset_parser(ClientSession_T *session)
{
	session->parser_state = FALSE;
	session->command_type = FALSE;
	if (session->rbuff) {
		p_string_truncate(session->rbuff,0);
	}

	if (session->args) {
		List_T args = p_list_first(session->args);
		while (p_list_data(args)) {
			String_T s = p_list_data(args);
			p_string_free(s, TRUE);
			if (! p_list_next(args))
				break;
			args = p_list_next(args);
		}
		p_list_free(&args);
	}
	session->args = p_list_new(session->pool);
}

void client_session_bailout(ClientSession_T **session)
{
	ClientSession_T *c = *session;
	Mempool_T pool;
	size_t before = 0, after = 0;
	int failcount = 0;
	List_T args = NULL;
	List_T from = NULL;
	List_T rcpt = NULL;
	List_T messagelst = NULL;

	assert(c);

	before = after = ci_wbuf_len(c->ci);

	while (after && (! (c->ci->client_state & CLIENT_ERR)) && (failcount < 100)) {
		before = ci_wbuf_len(c->ci);
		ci_write_cb(c->ci);
		after = ci_wbuf_len(c->ci);
		if (before == after)
			failcount++;
		else
			failcount = 0;

	}

	ci_cork(c->ci);

	TRACE(TRACE_DEBUG,"[%p]", c);
	// brute force:
	if (server_conf->no_daemonize == 1) _exit(0);

	client_session_reset(c);
	c->state = CLIENTSTATE_QUIT_QUEUED;
	ci_close(c->ci);

	p_string_free(c->rbuff, TRUE);

	if (c->from) {
		from = p_list_first(c->from);
		while (p_list_data(from)) {
			String_T s = p_list_data(from);
			p_string_free(s, TRUE);
			if (! p_list_next(from))
				break;
			from = p_list_next(from);
		}
		from = p_list_first(from);
		p_list_free(&from);
	}
	
	if (c->rcpt) {
		rcpt = p_list_first(c->rcpt);
		while (p_list_data(rcpt)) {
			Delivery_T *s = p_list_data(rcpt);
			g_free(s);
			if (! p_list_next(rcpt))
				break;
			rcpt = p_list_next(rcpt);
		}
		rcpt = p_list_first(rcpt);
		p_list_free(&rcpt);
	}
	
	if (c->args) {
		args = p_list_first(c->args);
		while (p_list_data(args)) {
			String_T s = p_list_data(args);
			p_string_free(s, TRUE);
			if (! p_list_next(args))
				break;
			args = p_list_next(args);
		}
		args = p_list_first(args);
		p_list_free(&args);
	}

	if (c->messagelst) {
		messagelst = p_list_first(c->messagelst);
		while (p_list_data(messagelst)) {
			struct message *m = p_list_data(messagelst);
			mempool_push(c->pool, m, sizeof(struct message));
			if (! p_list_next(messagelst))
				break;
			messagelst = p_list_next(messagelst);
		}
		messagelst = p_list_first(messagelst);
		p_list_free(&messagelst);
	}

	c->args = NULL;
	c->from = NULL;
	c->rcpt = NULL;
	c->messagelst = NULL;

	pool = c->pool;
	mempool_push(pool, c, sizeof(ClientSession_T));
	mempool_close(&pool);
	c = NULL;
}

void client_session_read(void *arg)
{
	ClientSession_T *session = (ClientSession_T *)arg;
	ci_read_cb(session->ci);

	uint64_t have = p_string_len(session->ci->read_buffer);
	uint64_t need = session->ci->rbuff_size;

	int enough = (need>0?(have >= need):(have > 0));
	int state = session->ci->client_state;

	if (state & CLIENT_ERR) {
		TRACE(TRACE_DEBUG,"client_state ERROR");
		client_session_bailout(&session);
	} else if (state & CLIENT_EOF) {
		ci_cork(session->ci);
		if (enough)
			session->handle_input(session);
		else 
			client_session_bailout(&session);
	}
	else if (have > 0)
		session->handle_input(session);
}

void client_session_set_timeout(ClientSession_T *session, int timeout)
{
	if (session && session->ci) {
		int current = session->ci->timeout.tv_sec;
		if (timeout != current)
			session->ci->timeout.tv_sec = timeout;
	}
}

void socket_read_cb(int fd UNUSED, short what, void *arg)
{
	ClientSession_T *session = (ClientSession_T *)arg;
	if (what == EV_READ)
		client_session_read(session); // drain the read-event handle
	else if (what == EV_TIMEOUT && session->ci->cb_time)
		session->ci->cb_time(session);
}

void socket_write_cb(int fd UNUSED, short what, void *arg)
{
	ClientSession_T *session = (ClientSession_T *)arg;

	if (! session->ci->cb_write)
		return;

	if (what == EV_TIMEOUT && session->ci->cb_time) {
		session->ci->cb_time(session);
		return;
	}

	session->ci->cb_write(session);

	switch(session->state) {
		case CLIENTSTATE_INITIAL_CONNECT:
		case CLIENTSTATE_NON_AUTHENTICATED:
			TRACE(TRACE_DEBUG,"reset timeout [%d]", server_conf->login_timeout);
			client_session_set_timeout(session, server_conf->login_timeout);
			break;

		case CLIENTSTATE_AUTHENTICATED:
		case CLIENTSTATE_SELECTED:
			TRACE(TRACE_DEBUG,"reset timeout [%d]", server_conf->timeout);
			client_session_set_timeout(session, server_conf->timeout);
			break;

		case CLIENTSTATE_LOGOUT:
		case CLIENTSTATE_QUIT:
		case CLIENTSTATE_ERROR:
			client_session_bailout(&session);
			break;

		default:
		case CLIENTSTATE_ANY:
		case CLIENTSTATE_QUIT_QUEUED:
			break;


	}
}

