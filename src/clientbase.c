/*
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
 */

#include <openssl/err.h>
#include "dbmail.h"
#include "dm_mempool.h"

#define THIS_MODULE "clientbase"

extern DBParam_T db_params;
#define DBPFX db_params.pfx

extern ServerConfig_T *server_conf;
extern SSL_CTX *tls_context;

static void dm_tls_error(void)
{
	unsigned long e;
	while ((e = ERR_get_error()))
		TRACE(TRACE_INFO, "%s", ERR_error_string(e, NULL));
}


static void client_wbuf_clear(ClientBase_T *client)
{
	if (client->write_buffer) {
		client->write_buffer = p_string_truncate(client->write_buffer,0);
	}

}

static void client_rbuf_clear(ClientBase_T *client)
{
	if (client->read_buffer) {
		p_string_truncate(client->read_buffer,0);
	}
}

static void client_rbuf_scale(ClientBase_T *client)
{
	if (client->read_buffer_offset == p_string_len(client->read_buffer)) {
		p_string_truncate(client->read_buffer,0);
		client->read_buffer_offset = 0;
	}

}

static void client_wbuf_scale(ClientBase_T *client)
{
	if (client->write_buffer_offset == p_string_len(client->write_buffer)) {
		p_string_truncate(client->write_buffer,0);
		client->write_buffer_offset = 0;
	}
}


static int client_error_cb(int sock, int error, void *arg)
{
	int r = 0;
	int serr;
	ClientBase_T *client = (ClientBase_T *)arg;
	if (client->sock->ssl) {
		int sslerr = 0;
		if (! (sslerr = SSL_get_error(client->sock->ssl, error)))
			return r;

		serr = errno;

		dm_tls_error();
		switch (sslerr) {
			case SSL_ERROR_ZERO_RETURN:
				PLOCK(client->lock);
				client->client_state |= CLIENT_EOF;
				PUNLOCK(client->lock);
				break;
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				break; // reschedule
			case SSL_ERROR_SYSCALL:
				TRACE(TRACE_DEBUG, "[%p] %d %s", client, sock, strerror(serr));
				client_rbuf_clear(client);
				client_wbuf_clear(client);
				r = -1;
				break;
			default:
				TRACE(TRACE_DEBUG,"[%p] %d %d, %p", client, sock, sslerr, arg);
				client_rbuf_clear(client);
				client_wbuf_clear(client);
				r = -1;
				break;
		}

	} else {
		switch (error) {
			case EAGAIN:
			case EINTR:
				break; // reschedule

			default:
				TRACE(TRACE_DEBUG,"[%p] fd [%d] %s[%d], %p", client, sock, strerror(error), error, arg);
				r = error;
				client_rbuf_clear(client);
				client_wbuf_clear(client);
				break;
		}
	}
	return r;
}

ClientBase_T * client_init(client_sock *c)
{
	int serr;
	ClientBase_T *client;
	Mempool_T pool = c->pool;

	client           = mempool_pop(pool, sizeof(ClientBase_T));
	client->pool     = pool;
	client->sock     = c;
	client->cb_error = client_error_cb;

	pthread_mutex_init(&client->lock, NULL);

	/* set byte counters to 0 */
	client->bytes_rx = 0;
	client->bytes_tx = 0;

	/* make streams */
	if (c->caddr_len == 0) {
		client->rx		= STDIN_FILENO;
		client->tx		= STDOUT_FILENO;
	} else {
		/* server-side */
		if ((serr = getnameinfo(&c->saddr, c->saddr_len, client->dst_ip, 
						NI_MAXHOST, client->dst_port, 
						NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV))) {
			TRACE(TRACE_INFO, "getnameinfo::error [%s]", gai_strerror(serr));
		}

		/* client-side */
		if (server_conf->resolveIP) {
			if ((serr = getnameinfo(&c->caddr, c->caddr_len, client->clientname,
						       	NI_MAXHOST-1, NULL, 0, NI_NAMEREQD))) {
				TRACE(TRACE_INFO, "getnameinfo:error [%s]", gai_strerror(serr));
			} 

			TRACE(TRACE_NOTICE, "incoming connection on [%s:%s] from [%s:%s (%s)]",
					client->dst_ip, client->dst_port,
					client->src_ip, client->src_port,
					client->clientname[0] ? client->clientname : "Lookup failed");
		} else {

			if ((serr = getnameinfo(&c->caddr, c->caddr_len, client->src_ip,
						       	NI_MAXHOST-1, client->src_port,
						       	NI_MAXSERV-1, NI_NUMERICHOST | NI_NUMERICSERV))) {
				TRACE(TRACE_INFO, "getnameinfo:error [%s]", gai_strerror(serr));
			} 

			TRACE(TRACE_NOTICE, "incoming connection on [%s:%s] from [%s:%s]", 
					client->dst_ip, client->dst_port,
					client->src_ip, client->src_port);
		}

		/* make streams */
		client->rx = client->tx = c->sock;

		if (c->ssl_state == -1) {
			ci_starttls(client);
		}
	}

	client->read_buffer = p_string_new(pool, "");
	client->write_buffer = p_string_new(pool, "");
	client->rev = NULL;
	client->wev = NULL;

	return client;
}

void ci_cork(ClientBase_T *s)
{
	TRACE(TRACE_DEBUG,"[%p] [%d] [%d]", s, s->rx, s->tx);
	if (s->rev) event_del(s->rev);
	if (s->wev) event_del(s->wev);
}

void ci_uncork(ClientBase_T *s)
{
	int state;
	TRACE(TRACE_DEBUG,"[%p] [%d] [%d], [%ld]", s, s->rx, s->tx, s->timeout.tv_sec);

	PLOCK(s->lock);
	state = s->client_state;
	PUNLOCK(s->lock);

	if (state & CLIENT_ERR)
		return;

	if (! (state & CLIENT_EOF))
		event_add(s->rev, &s->timeout);
	event_add(s->wev, NULL);
}

int ci_starttls(ClientBase_T *client)
{
	int e;
	TRACE(TRACE_DEBUG,"[%p] ssl_state [%d]", client, client->sock->ssl_state);
	if (client->sock->ssl && client->sock->ssl_state > 0) {
		TRACE(TRACE_WARNING, "ssl already initialized");
		return DM_EGENERAL;
	}

	if (! client->sock->ssl) {
		client->sock->ssl_state = FALSE;
		if (! (client->sock->ssl = tls_setup(client->tx))) {
			TRACE(TRACE_DEBUG, "[%p] tls_setup failed", client);
			return DM_EGENERAL;
		}
	}

	if (! client->sock->ssl_state) {
		if ((e = SSL_accept(client->sock->ssl)) != 1) {
			int e2;
			if ((e2 = client->cb_error(client->rx, e, (void *)client))) {
				SSL_shutdown(client->sock->ssl);
				SSL_free(client->sock->ssl);
				client->sock->ssl = NULL;
				TRACE(TRACE_DEBUG, "[%p] SSL_accept hard failure",
						client);
				return DM_EGENERAL;
			}
		}
		client->sock->ssl_state = TRUE;
		ci_write(client,NULL);
	}

	return DM_SUCCESS;
}

void ci_write_cb(ClientBase_T *client)
{
	uint64_t rest = ci_wbuf_len(client);
	int result = 0;
	if (rest) {
	       result = ci_write(client,NULL);
	       switch(result) {
		       case 0:
			       event_add(client->wev, NULL);
			       break;
		       case 1:
			       ci_uncork(client);
			       break;
		       case -1:
			       client_wbuf_clear(client);
			       break;
	       }
	}
}

int ci_write(ClientBase_T *client, char * msg, ...)
{
	va_list ap, cp;
	int64_t t = 0;
	int e = 0;
	uint64_t n, left;
	char *s;
	int state;

	if (! (client && client->write_buffer))
		return -1; // stale

	PLOCK(client->lock);
	state = client->client_state;
	PUNLOCK(client->lock);

	if (state & CLIENT_ERR)
		return -1; // disconnected

	if (msg) {
		va_start(ap, msg);
		va_copy(cp, ap);
		p_string_append_vprintf(client->write_buffer, msg, cp);
		va_end(cp);
		va_end(ap);
	}

	left = ci_wbuf_len(client);
	while (left > 0) {
		n = left;
		if (n >= TLS_SEGMENT) n = TLS_SEGMENT - 1;

		s = (char *)p_string_str(client->write_buffer) + client->write_buffer_offset;

		if (client->sock->ssl) {
			if (! client->tls_wbuf_n) {
				strncpy(client->tls_wbuf, s, n);
				client->tls_wbuf_n = n;
			}
			t = (int64_t)SSL_write(client->sock->ssl, (gconstpointer)client->tls_wbuf, client->tls_wbuf_n);
		} else {
			t = (int64_t)write(client->tx, (gconstpointer)s, n);
		}

		if (t == -1) {
			if (client->sock->ssl)
				e = t;
			else
				e = errno;

			if (client->cb_error(client->tx, e, (void *)client)) {
				PLOCK(client->lock);
				client->client_state |= CLIENT_ERR;
				PUNLOCK(client->lock);
				return -1;
			} 
			return 0;
		} else if ((t == 0) && (client->sock->ssl)) {
			TRACE(TRACE_DEBUG, "ssl_ragged_eof");
			if (client->cb_error(client->tx, t, (void *)client) < 0) {
				PLOCK(client->lock);
				client->client_state |= CLIENT_ERR;
				PUNLOCK(client->lock);
				return -1;
			} 
		} 

		TRACE(TRACE_DEBUG, "[%p] S > [%" PRId64 "/%" PRIu64 ":%s]", client, t, left, s);

		client->bytes_tx += t;	// Update our byte counter
		client->write_buffer_offset += t;
		client_wbuf_scale(client);
		if (client->sock->ssl) {
			memset(client->tls_wbuf, '\0', TLS_SEGMENT);
			client->tls_wbuf_n = 0;
		}

		left = ci_wbuf_len(client);
	}

	return 1;
}

size_t ci_wbuf_len(ClientBase_T *client)
{
	size_t len = 0;
	int state;

	PLOCK(client->lock);
	state = client->client_state;
	PUNLOCK(client->lock);

	if (state & CLIENT_ERR) {
		client_wbuf_clear(client);
		return len;
	}

	if (client->write_buffer)
		len = p_string_len(client->write_buffer) - client->write_buffer_offset;
	return len;
}


#define IBUFLEN 65535
void ci_read_cb(ClientBase_T *client)
{
	/* 
	 * read all available data on the input stream
	 * and store in in read_buffer
	 */
	int64_t t = 0;
	char ibuf[IBUFLEN];
	int state;

	while (TRUE) {
		memset(ibuf, 0, sizeof(ibuf));
		if (client->sock->ssl) {
			t = (int64_t)SSL_read(client->sock->ssl, ibuf, sizeof(ibuf)-1);
		} else {
			t = (int64_t)read(client->rx, ibuf, sizeof(ibuf)-1);
		}
		TRACE(TRACE_DEBUG, "[%p] [%" PRId64 "]", client, t);

		if (t < 0) {
			int e = errno;
			if ((e = client->cb_error(client->rx, e, (void *)client)))
				state = CLIENT_ERR;
			else
				state = CLIENT_AGAIN;
			PLOCK(client->lock);
			client->client_state |= state;
			PUNLOCK(client->lock);

			break;

		} else if (t == 0) {
			if (client->sock->ssl) {
				if (client->cb_error(client->rx, t, (void *)client)) {
					PLOCK(client->lock);
					client->client_state |= CLIENT_ERR;
					PUNLOCK(client->lock);
				}
			} else { // connection was closed from client side
				PLOCK(client->lock);
				client->client_state |= CLIENT_ERR;
				PUNLOCK(client->lock);
			}

			if (client->sock->ssl || client->rx) { // EOF on stdin is not an error
				PLOCK(client->lock);
				client->client_state |= CLIENT_EOF;
				PUNLOCK(client->lock);
			}
			break;

		} else if (t > 0) {
			client->bytes_rx += t;	// Update our byte counter
			PLOCK(client->lock);
			client->client_state = CLIENT_OK; 
			PUNLOCK(client->lock);
			p_string_append_len(client->read_buffer, ibuf, t);
		}
	}
}

int ci_read(ClientBase_T *client, char *buffer, size_t n)
{
	// fetch data from the read buffer
	assert(buffer);

	client->len = 0;
	char *s = (char *)p_string_str(client->read_buffer) + client->read_buffer_offset;
	if ((client->read_buffer_offset + n) <= p_string_len(client->read_buffer)) {
		/*
		size_t j,k = 0;

		memset(buffer, 0, sizeof(buffer));
		for (j=0; j<n; j++)
			buffer[k++] = s[j];
		*/
		memcpy(buffer, s, n);
		client->read_buffer_offset += n;
		client->len += n;
		client_rbuf_scale(client);
	}

	return client->len;
}

int ci_readln(ClientBase_T *client, char * buffer)
{
	// fetch a line from the read buffer
	char *nl;

	assert(buffer);

	client->len = 0;
	char *s = (char *)p_string_str(client->read_buffer) + client->read_buffer_offset;
	if ((nl = g_strstr_len(s, -1, "\n"))) {
		uint64_t j, k = 0, l;
		l = stridx(s, '\n');
		if (l >= MAX_LINESIZE) {
			TRACE(TRACE_WARNING, "insane line-length [%" PRIu64 "]", l);
			PLOCK(client->lock);
			client->client_state |= CLIENT_ERR;
			PUNLOCK(client->lock);
			return 0;
		}
		for (j=0; j<=l; j++)
			buffer[k++] = s[j];
		client->read_buffer_offset += l+1;
		client->len = k;
		TRACE(TRACE_INFO, "[%p] C < [%" PRIu64 ":%s]", client, client->len, buffer);

		client_rbuf_scale(client);
	}

	return client->len;
}


void ci_authlog_init(ClientBase_T *client, const char *service, const char *username, const char *status)
{
	if ((! server_conf->authlog) || server_conf->no_daemonize == 1) return;
	Connection_T c; ResultSet_T r; PreparedStatement_T s;
	const char *now = db_get_sql(SQL_CURRENT_TIMESTAMP);
	char *frag = db_returning("id");
	c = db_con_get();
	TRY
		const char *user = client->auth?Cram_getUsername(client->auth):username;

		s = db_stmt_prepare(c, "INSERT INTO %sauthlog (userid, service, login_time, logout_time, src_ip, src_port, dst_ip, dst_port, status)"
				" VALUES (?, ?, %s, %s, ?, ?, ?, ?, ?) %s", DBPFX, now, now, frag);

		g_free(frag);
		db_stmt_set_str(s, 1, user);
		db_stmt_set_str(s, 2, service);
		db_stmt_set_str(s, 3, (char *)client->src_ip);
		db_stmt_set_int(s, 4, atoi(client->src_port));
		db_stmt_set_str(s, 5, (char *)client->dst_ip);
		db_stmt_set_int(s, 6, atoi(client->dst_port));
		db_stmt_set_str(s, 7, status);

		r = db_stmt_query(s);
		
		if(strcmp(AUTHLOG_ERR,status)!=0) client->authlog_id = db_insert_result(c, r);
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

}

static void ci_authlog_close(ClientBase_T *client)
{
	Connection_T c; PreparedStatement_T s;
	if (! client->authlog_id) return;
	if ((! server_conf->authlog) || server_conf->no_daemonize) return;
	const char *now = db_get_sql(SQL_CURRENT_TIMESTAMP);
	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, "UPDATE %sauthlog SET logout_time=%s, status=?, bytes_rx=?, bytes_tx=? "
		"WHERE id=?", DBPFX, now);
		db_stmt_set_str(s, 1, AUTHLOG_FIN);
		db_stmt_set_u64(s, 2, client->bytes_rx);
		db_stmt_set_u64(s, 3, client->bytes_tx);
		db_stmt_set_u64(s, 4, client->authlog_id);

		db_stmt_exec(s);
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;
}

void ci_close(ClientBase_T *client)
{
	assert(client);

	TRACE(TRACE_DEBUG, "closing clientbase [%p] [%d] [%d]", client,
			client->tx, client->rx);

	ci_cork(client);

	if (client->rev) {
		event_free(client->rev);
	       	client->rev = NULL;
	}
	if (client->wev) {
		event_free(client->wev);
	       	client->wev = NULL;
	}

	if ((client->sock->sock > 1) && (shutdown(client->sock->sock, SHUT_RDWR)))
		TRACE(TRACE_DEBUG, "[%s]", strerror(errno));

	if (client->tx >= 0) {
		close(client->tx);
		client->tx = -1;
	}

	if (client->rx >= 0) {
	       	close(client->rx);
		client->rx = -1;
	}

	ci_authlog_close(client);

	if (client->auth) {
		Cram_T c = client->auth;
		Cram_free(&c);
		client->auth = NULL;
	}

	if (client->sock->ssl) {
		SSL_shutdown(client->sock->ssl);
		SSL_free(client->sock->ssl);
	}

	p_string_free(client->read_buffer, TRUE);
	p_string_free(client->write_buffer, TRUE);

	pthread_mutex_destroy(&client->lock);

	Mempool_T pool = client->pool;
	mempool_push(pool, client->sock, sizeof(client_sock));
	client->sock = NULL;

	mempool_push(pool, client, sizeof(ClientBase_T));
	client = NULL;
}


