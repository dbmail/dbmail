/*
  
 Copyright (c) 2004-2010 NFG Net Facilities Group BV support@nfg.nl

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

#include "dbmail.h"
#include <openssl/err.h>

#define THIS_MODULE "clientbase"

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

extern serverConfig_t *server_conf;
extern SSL_CTX *tls_context;

static void dm_tls_error(void)
{
	unsigned long e;
	e = ERR_get_error();
	if (e == 0) {
		if (errno != 0) {
			int se = errno;
			switch (se) {
				case EAGAIN:
				case EINTR:
					break;
				default:
					TRACE(TRACE_ERR, "%s", strerror(se));
				break;
			}
		} else {
			TRACE(TRACE_ERR, "Unknown error");
		}
		return;
	}
	TRACE(TRACE_ERR, "%s", ERR_error_string(e, NULL));
}

static void client_wbuf_clear(clientbase_t *client)
{
	if (client->write_buffer) {
		client->write_buffer = g_string_truncate(client->write_buffer,0);
		g_string_maybe_shrink(client->write_buffer);
	}

}

static void client_rbuf_clear(clientbase_t *client)
{
	if (client->read_buffer) {
		client->read_buffer = g_string_truncate(client->read_buffer,0);
		g_string_maybe_shrink(client->read_buffer);
	}
}

static void client_rbuf_scale(clientbase_t *self)
{
	if (self->read_buffer_offset == self->read_buffer->len) {
		g_string_truncate(self->read_buffer,0);
		self->read_buffer_offset = 0;
		g_string_maybe_shrink(self->read_buffer);
	}

}

static void client_wbuf_scale(clientbase_t *self)
{
	if (self->write_buffer_offset == self->write_buffer->len) {
		g_string_truncate(self->write_buffer,0);
		self->write_buffer_offset = 0;
		g_string_maybe_shrink(self->write_buffer);
	}

}


static int client_error_cb(int sock, int error, void *arg)
{
	int r = 0;
	clientbase_t *client = (clientbase_t *)arg;
	if (client->ssl) {
		int sslerr = 0;
		if (! (sslerr = SSL_get_error(client->ssl, error)))
			return r;
		
		dm_tls_error();
		switch (sslerr) {
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				break; // reschedule
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

			case EPIPE: // ignore
				TRACE(TRACE_DEBUG,"[%p] %d %s[%d], %p", client, sock, strerror(error), error, arg);
				client_rbuf_clear(client);
				client_wbuf_clear(client);
				break;
			default:
				r = -1;
				TRACE(TRACE_DEBUG,"[%p] %d %s[%d], %p", client, sock, strerror(error), error, arg);
				client_rbuf_clear(client);
				client_wbuf_clear(client);
				break;
		}
	}
	return r;
}

clientbase_t * client_init(client_sock *c)
{
	int serr;
	clientbase_t *client	= g_new0(clientbase_t, 1);

	client->timeout         = g_new0(struct timeval,1);

	if (g_thread_supported())
		client->queue   = g_async_queue_new();
	client->cb_error        = client_error_cb;

	/* set byte counters to 0 */
	client->bytes_rx = 0;
	client->bytes_tx = 0;

	/* make streams */
	if (c == NULL) {
		client->rx		= STDIN_FILENO;
		client->tx		= STDOUT_FILENO;
	} else {
		/* server-side */
		TRACE(TRACE_DEBUG,"saddr [%p] sa_family [%d] len [%d]", c->saddr, c->saddr->sa_family, c->saddr_len);
		if ((serr = getnameinfo(c->saddr, c->saddr_len, client->dst_ip, NI_MAXHOST, client->dst_port, NI_MAXSERV, 
						NI_NUMERICHOST | NI_NUMERICSERV))) {
			TRACE(TRACE_INFO, "getnameinfo::error [%s]", gai_strerror(serr));
		}
		TRACE(TRACE_NOTICE, "incoming connection on [%s:%s]", client->dst_ip, client->dst_port);

		/* client-side */
		TRACE(TRACE_DEBUG,"caddr [%p] sa_family [%d] len [%d]", c->caddr, c->caddr->sa_family, c->caddr_len);
		if ((serr = getnameinfo(c->caddr, c->caddr_len, client->src_ip, NI_MAXHOST, client->src_port, NI_MAXSERV,
						NI_NUMERICHOST | NI_NUMERICSERV))) {
			TRACE(TRACE_EMERG, "getnameinfo:error [%s]", gai_strerror(serr));
		} 

		if (server_conf->resolveIP) {
			if ((serr = getnameinfo(c->caddr, c->caddr_len, client->clientname, NI_MAXHOST, NULL, 0, NI_NAMEREQD))) {
				TRACE(TRACE_INFO, "getnameinfo:error [%s]", gai_strerror(serr));
			} 

			TRACE(TRACE_NOTICE, "incoming connection from [%s:%s (%s)]",
					client->src_ip, client->src_port,
					client->clientname[0] ? client->clientname : "Lookup failed");
		} else {
			TRACE(TRACE_NOTICE, "incoming connection from [%s:%s]", client->src_ip, client->src_port);
		}

		/* make streams */
		client->rx = client->tx = c->sock;
		client->ssl = c->ssl;

		if (c->ssl)
			client->ssl_state = TRUE;
	}

	client->read_buffer = g_string_new("");
	client->write_buffer = g_string_new("");
	client->rev = g_new0(struct event, 1);
	client->wev = g_new0(struct event, 1);

	return client;
}

int ci_starttls(clientbase_t *self)
{
	int e;
	TRACE(TRACE_DEBUG,"[%p] ssl_state [%d]", self, self->ssl_state);
	if (self->ssl && self->ssl_state) {
		TRACE(TRACE_ERR, "ssl already initialized");
		return DM_EGENERAL;
	}

	if (! self->ssl) {
		self->ssl_state = FALSE;
		if (! (self->ssl = SSL_new(tls_context))) {
			TRACE(TRACE_ERR, "Error creating TLS connection: %s", tls_get_error());
			return DM_EGENERAL;
		}
		if ( !SSL_set_fd(self->ssl, self->tx)) {
			TRACE(TRACE_ERR, "Error linking SSL structure to file descriptor: %s", tls_get_error());
			SSL_free(self->ssl);
			self->ssl = NULL;
			return DM_EGENERAL;
		}
	}
	if (! self->ssl_state) {
		if ((e = SSL_accept(self->ssl)) != 1) {
			int e2;
			if ((e2 = self->cb_error(self->rx, e, (void *)self))) {
				SSL_free(self->ssl);
				self->ssl = NULL;
				return DM_EGENERAL;
			} else {
				event_add(self->rev, self->timeout);
				return e;
			}
		}
		self->ssl_state = TRUE;
	}

	TRACE(TRACE_DEBUG,"[%p] ssl initialized", self);
	return DM_SUCCESS;
}

void ci_write_cb(clientbase_t *self)
{
	if (self->write_buffer->len > self->write_buffer_offset)
		ci_write(self,NULL);
}

int ci_write(clientbase_t *self, char * msg, ...)
{
	va_list ap, cp;
	ssize_t t = 0;
	int e = 0;
	size_t n;
	char *s;

	if (! (self && self->write_buffer)) {
		TRACE(TRACE_DEBUG, "called while clientbase is stale");
		return -1;
	}

	if (msg) {
		va_start(ap, msg);
		va_copy(cp, ap);
		g_string_append_vprintf(self->write_buffer, msg, cp);
		va_end(cp);
	}
	
	if (self->write_buffer->len < 1) { 
		TRACE(TRACE_DEBUG, "write_buffer is empty [%d]", self->write_buffer->len);
		return 0;
	}

	s = self->write_buffer->str + self->write_buffer_offset;
	n = self->write_buffer->len - self->write_buffer_offset;

	if (n > TLS_SEGMENT) n = TLS_SEGMENT;

	if (self->ssl) {
		if (! self->tls_wbuf_n) {
			strncpy(self->tls_wbuf, s, n);
			self->tls_wbuf_n = n;
		}
		t = SSL_write(self->ssl, (gconstpointer)self->tls_wbuf, self->tls_wbuf_n);
		e = t;
	} else {
		t = write(self->tx, (gconstpointer)s, n);
		e = errno;
	}

	if (t == -1) {
		if ((e = self->cb_error(self->tx, e, (void *)self))) {
			self->client_state |= CLIENT_ERR;
			return e;
		}
	} else {
		self->bytes_tx += t;	// Update our byte counter
		if (self->ssl) {
			memset(self->tls_wbuf, '\0', TLS_SEGMENT);
			self->tls_wbuf_n = 0;
		}
		self->write_buffer_offset += t;

		TRACE(TRACE_INFO, "[%p] S > [%u/%u:%s]", self, self->write_buffer_offset, self->write_buffer->len, s);

		client_wbuf_scale(self);

	}

	event_add(self->wev, NULL);

	return 0;
}

#define IBUFLEN 4096
void ci_read_cb(clientbase_t *self)
{
	/* 
	 * read all available data on the input stream
	 * and store in in read_buffer
	 */
	ssize_t t = 0;
	char ibuf[IBUFLEN];

	TRACE(TRACE_DEBUG,"[%p] reset timeout [%ld]", self, self->timeout->tv_sec); 
	event_add(self->rev, self->timeout);

	if (self->ssl && self->ssl_state == FALSE) {
		ci_starttls(self);
		return;
	}

	while (TRUE) {
		memset(ibuf, 0, sizeof(ibuf));
		if (self->ssl)
			t = SSL_read(self->ssl, (void *)ibuf, IBUFLEN);
		else
			t = read(self->rx, (void *)ibuf, IBUFLEN);

		if (t < 0) {
			int e;
			if ((e = self->cb_error(self->rx, errno, (void *)self)))
				self->client_state |= CLIENT_ERR;
			else
				self->client_state |= CLIENT_AGAIN;
			break;

		} else if (t == 0) {
			self->client_state |= CLIENT_EOF;
			break;

		} else {
			self->bytes_rx += t;	// Update our byte counter
			self->client_state = CLIENT_OK; 
			g_string_append_len(self->read_buffer, ibuf, t);
			TRACE(TRACE_DEBUG,"read [%u:%s]", t, t?ibuf:"(null)");
		}
	}

	TRACE(TRACE_DEBUG,"[%p] state [%x] read_buffer->len[%u]", self, self->client_state, self->read_buffer->len);
}

int ci_read(clientbase_t *self, char *buffer, size_t n)
{
	assert(buffer);

	TRACE(TRACE_DEBUG,"[%p] need [%u]", self, n);
	self->len = 0;

	char *s = self->read_buffer->str + self->read_buffer_offset;
	if ((self->read_buffer_offset + n) <= self->read_buffer->len) {
		size_t j,k = 0;

		memset(buffer, 0, sizeof(buffer));
		for (j=0; j<n; j++)
			buffer[k++] = s[j];
		self->read_buffer_offset += n;
		self->len += j;
		client_rbuf_scale(self);
	}

	if (self->len)
		TRACE(TRACE_DEBUG,"[%p] read [%u:%s]", self, self->len, buffer);

	return self->len;
}

int ci_readln(clientbase_t *self, char * buffer)
{
	char *nl;

	assert(buffer);

	self->len = 0;
	char *s = self->read_buffer->str + self->read_buffer_offset;
	if ((nl = g_strstr_len(s, -1, "\n"))) {
		size_t j, k = 0, l;
		l = stridx(s, '\n');
		if (l >= MAX_LINESIZE) {
			TRACE(TRACE_ERR, "insane line-length [%u]", l);
			self->client_state = CLIENT_ERR;
			return 0;
		}
		for (j=0; j<=l; j++)
			buffer[k++] = s[j];
		self->read_buffer_offset += l+1;
		self->len = k;
		TRACE(TRACE_INFO, "[%p] C < [%u:%s]", self, self->len, buffer);

		client_rbuf_scale(self);
	}

	return self->len;
}


void ci_authlog_init(clientbase_t *self, const char *service, const char *username, const char *status)
{
	if ((! server_conf->authlog) || server_conf->no_daemonize == 1) return;
	C c; R r; S s;
	const char *now = db_get_sql(SQL_CURRENT_TIMESTAMP);
	char *frag = db_returning("id");
	c = db_con_get();
	TRY

		s = db_stmt_prepare(c, "INSERT INTO %sauthlog (userid, service, login_time, logout_time, src_ip, src_port, dst_ip, dst_port, status)"
				" VALUES (?, ?, %s, %s, ?, ?, ?, ?, ?) %s", DBPFX, now, now, frag);

		db_stmt_set_str(s, 1, username);
		db_stmt_set_str(s, 2, service);
		db_stmt_set_str(s, 3, (char *)self->src_ip);
		db_stmt_set_int(s, 4, atoi(self->src_port));
		db_stmt_set_str(s, 5, (char *)self->dst_ip);
		db_stmt_set_int(s, 6, atoi(self->dst_port));
		db_stmt_set_str(s, 7, status);

		r = db_stmt_query(s);
		
		if(strcmp(AUTHLOG_ERR,status)!=0) self->authlog_id = db_insert_result(c, r);
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

}

static void ci_authlog_close(clientbase_t *self)
{
	C c; S s;
	if ((! server_conf->authlog) || server_conf->no_daemonize) return;
	if (! self->authlog_id) return;
	const char *now = db_get_sql(SQL_CURRENT_TIMESTAMP);
	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, "UPDATE %sauthlog SET logout_time=%s, status=?, bytes_rx=?, bytes_tx=? "
		"WHERE id=?", DBPFX, now);
		db_stmt_set_str(s, 1, AUTHLOG_FIN);
		db_stmt_set_u64(s, 2, self->bytes_rx);
		db_stmt_set_u64(s, 3, self->bytes_tx);
		db_stmt_set_u64(s, 4, self->authlog_id);

		db_stmt_exec(s);
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;
}

void ci_close(clientbase_t *self)
{
	assert(self);
	TRACE(TRACE_DEBUG, "closing clientbase [%p]", self);

	if (self->queue) g_async_queue_unref(self->queue);
	event_del(self->rev);
	event_del(self->wev);

	g_free(self->rev); self->rev = NULL;
	g_free(self->wev); self->wev = NULL;

	if (self->tx > 0) {
		shutdown(self->tx, SHUT_RDWR);
		close(self->tx);
	}
	if (self->rx >= 0) {
		close(self->rx);
	}

	ci_authlog_close(self);
	self->tx = -1;
	self->rx = -1;

	g_string_free(self->read_buffer, TRUE);
	g_string_free(self->write_buffer, TRUE);

	g_free(self->timeout);
	self->timeout = NULL;

	if (self->auth) {
		Cram_T c = self->auth;
		Cram_free(&c);
		self->auth = NULL;
	}


	g_free(self);
	
	self = NULL;
}


