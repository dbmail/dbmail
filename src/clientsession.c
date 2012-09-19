/*
 Copyright (C) 2004 IC & S dbmail@ic-s.nl
 Copyright (c) 2004-2012 NFG Net Facilities Group BV support@nfg.nl

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
	char unique_id[UID_SIZE];

	ClientSession_T * session = g_new0(ClientSession_T,1);
	ClientBase_T *ci;

	if (c)
		ci = client_init(c);
	else
		ci = client_init(NULL);

	session->state = CLIENTSTATE_INITIAL_CONNECT;

	gethostname(session->hostname, sizeof(session->hostname));

	memset(unique_id,0,sizeof(unique_id));
	create_unique_id(unique_id, 0);
	session->apop_stamp = g_strdup_printf("<%s@%s>", unique_id, session->hostname);

	assert(evbase);
        ci->rev = event_new(evbase, ci->rx, EV_READ|EV_PERSIST, socket_read_cb, (void *)session);
        ci->wev = event_new(evbase, ci->tx, EV_WRITE, socket_write_cb, (void *)session);

	session->ci = ci;
	session->rbuff = g_string_new("");

	return session;
}

void client_session_reset(ClientSession_T * session)
{
	dsnuser_free_list(session->rcpt);
	session->rcpt = NULL;

	g_list_destroy(session->from);
	session->from = NULL;

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
		g_string_truncate(session->rbuff,0);
		g_string_maybe_shrink(session->rbuff);
	}

	if (session->args) {
		g_list_destroy(session->args);
		session->args = NULL;
	}
}

void client_session_bailout(ClientSession_T **session)
{
	ClientSession_T *c = *session;

	if (! c) return;
	TRACE(TRACE_DEBUG,"[%p]", c);

	// brute force:
	if (server_conf->no_daemonize == 1) _exit(0);

	client_session_reset(c);
	c->state = CLIENTSTATE_ANY;
	ci_close(c->ci);
	c->ci = NULL;
	g_free(c);
	c = NULL;
}

void client_session_read(void *arg)
{
	int state;
	ClientSession_T *session = (ClientSession_T *)arg;
	TRACE(TRACE_DEBUG, "[%p] state: [%d]", session, session->state);
	ci_read_cb(session->ci);
		
	state = session->ci->client_state;
	if (state & CLIENT_ERR) {
		TRACE(TRACE_DEBUG,"client_state ERROR");
		client_session_bailout(&session);
	} else if (state & CLIENT_EOF) {
		TRACE(TRACE_NOTICE,"reached EOF");
		event_del(session->ci->rev);
		if (session->ci->read_buffer->len < 1)
			client_session_bailout(&session);
		else 
			session->handle_input(session);
	}
	else if (state & CLIENT_OK || state & CLIENT_AGAIN) {
		session->handle_input(session);
	}
}

void client_session_set_timeout(ClientSession_T *session, int timeout)
{
	if (session && (session->state > CLIENTSTATE_ANY) && session->ci && session->ci->timeout) {
		int current = session->ci->timeout->tv_sec;
		if (timeout != current) {
			ci_cork(session->ci);
			session->ci->timeout->tv_sec = timeout;
			ci_uncork(session->ci);
		}
	}
}

void socket_read_cb(int fd UNUSED, short what UNUSED, void *arg)
{
	ClientSession_T *session = (ClientSession_T *)arg;
	if (what == EV_READ)
		client_session_read(session); // drain the read-event handle
	else if (what == EV_TIMEOUT && session->ci->cb_time)
		session->ci->cb_time(session);
}

void socket_write_cb(int fd UNUSED, short what UNUSED, void *arg)
{
	ClientSession_T *session = (ClientSession_T *)arg;
	TRACE(TRACE_DEBUG,"[%p] state: [%d]", session, session->state);

	if (session->ci->cb_write)
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

		default:
		case CLIENTSTATE_ANY:
			break;

		case CLIENTSTATE_LOGOUT:
		case CLIENTSTATE_QUIT:
		case CLIENTSTATE_ERROR:
			client_session_bailout(&session);
			break;


	}
}

