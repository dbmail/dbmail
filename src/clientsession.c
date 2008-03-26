/*
 Copyright (C) 2004 IC & S dbmail@ic-s.nl
 Copyright (C) 2008 NFG Net Facilities Group BV, support@nfg.nl

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

#define MAX_ERRORS 3

#define THIS_MODULE "clientsession"

ClientSession_t * client_session_new(client_sock *c)
{
	char unique_id[UID_SIZE];

	ClientSession_t * session = g_new0(ClientSession_t,1);
	clientinfo_t *ci = client_init(c->sock, c->caddr);

	session->state = IMAPCS_INITIAL_CONNECT;

	gethostname(session->hostname, sizeof(session->hostname));

	memset(unique_id,0,sizeof(unique_id));
	create_unique_id(unique_id, 0);
	session->apop_stamp = g_strdup_printf("<%s@%s>", unique_id, session->hostname);

        ci->rev = bufferevent_new(ci->rx, socket_read_cb, NULL, socket_error_cb, (void *)session);
        ci->wev = bufferevent_new(ci->tx, NULL, socket_write_cb, socket_error_cb, (void *)session);

	session->ci = ci;
        session->timeout = ci->login_timeout;

	session->rbuff = g_string_new("");

	return session;
}

int client_session_reset(ClientSession_t * session)
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

	session->state = IMAPCS_INITIAL_CONNECT;

	client_session_reset_parser(session);

	return 1;
}

void client_session_reset_parser(ClientSession_t *session)
{
	session->parser_state = FALSE;
	session->command_type = FALSE;
	if (session->rbuff)
		g_string_printf(session->rbuff,"%s","");

	if (session->args) {
		g_list_destroy(session->args);
		session->args = NULL;
	}
}

void client_session_set_timeout(ClientSession_t *session, int timeout)
{
	session->timeout = timeout;
        bufferevent_settimeout(session->ci->rev, session->timeout, 0);
}

void client_session_bailout(ClientSession_t *session)
{
	if (! session) return;
	client_session_reset(session);
	//g_string_free(session->rbuff,TRUE);
	ci_close(session->ci);
}

void socket_read_cb(struct bufferevent *ev UNUSED, void *arg)
{
	C c;
	ClientSession_t *session = (ClientSession_t *)arg;

	TRACE(TRACE_DEBUG,"[%p] state: [%d]", session, session->state);
	c = db_con_get();
	if (!db_check_connection(c)) {
		db_con_close(c);
		TRACE(TRACE_ERROR, "database connection error");
		client_session_bailout(session);
		return;
	}
	db_con_close(c);

	session->ci->cb_read(session);
}

void socket_write_cb(struct bufferevent *ev UNUSED, void *arg)
{

	ClientSession_t *session = (ClientSession_t *)arg;
	TRACE(TRACE_DEBUG,"[%p] state: [%d]", session, session->state);

	if (session->ci->cb_write) {
		session->ci->cb_write(session);
		return;
	}

	switch(session->state) {

		case IMAPCS_LOGOUT:
		case IMAPCS_ERROR:
			client_session_bailout(session);
			break;

		case IMAPCS_INITIAL_CONNECT:
		case IMAPCS_NON_AUTHENTICATED:
			TRACE(TRACE_DEBUG,"reset timeout [%d]", session->ci->login_timeout);
			session->timeout = session->ci->login_timeout;
			break;

		default:
			TRACE(TRACE_DEBUG,"reset timeout [%d]", session->ci->timeout);
			session->timeout = session->ci->timeout;
			break;
	}
}


void socket_error_cb(struct bufferevent *ev UNUSED, short what, void *arg)
{
	ClientSession_t *session = (ClientSession_t *)arg;
	TRACE(TRACE_DEBUG,"[%p] state: [%d]", session, session->state);
	int serr = errno;
	if (what & EVBUFFER_EOF)
		TRACE(TRACE_INFO, "client disconnected. %s", strerror(serr));
		// defer the actual disconnetion to socket_write_cb so the server
		// will disconnect only when the write buffer is depleted and we're
		// done writing

	else if (what & EVBUFFER_TIMEOUT)
		session->ci->cb_time(session);
	else
		client_session_bailout(session);
}

