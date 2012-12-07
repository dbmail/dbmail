/*
  
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
	e = ERR_get_error();
	if (e == 0) {
		if (errno != 0) {
			int se = errno;
			switch (se) {
				case EAGAIN:
				case EINTR:
					break;
				default:
					TRACE(TRACE_INFO, "%s", strerror(se));
				break;
			}
		} else {
			TRACE(TRACE_INFO, "Unknown error");
		}
		return;
	}
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

static void client_rbuf_scale(ClientBase_T *self)
{
	if (self->read_buffer_offset == p_string_len(self->read_buffer)) {
		p_string_truncate(self->read_buffer,0);
		self->read_buffer_offset = 0;
	}

}

static void client_wbuf_scale(ClientBase_T *self)
{
	if (self->write_buffer_offset == p_string_len(self->write_buffer)) {
		p_string_truncate(self->write_buffer,0);
		self->write_buffer_offset = 0;
	}

}


static int client_error_cb(int sock, int error, void *arg)
{
	int r = 0;
	ClientBase_T *client = (ClientBase_T *)arg;
	if (client->sock->ssl) {
		int sslerr = 0;
		if (! (sslerr = SSL_get_error(client->sock->ssl, error)))
			return r;
		
		dm_tls_error();
		switch (sslerr) {
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				break; // reschedule
			case SSL_ERROR_SYSCALL:
				if (error == -1)
					TRACE(TRACE_DEBUG, "[%p] %d %s", client, sock, strerror(errno));
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
				r = error;
				TRACE(TRACE_DEBUG,"[%p] %d %s[%d], %p", client, sock, strerror(error), error, arg);
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

	client           = mempool_pop(c->pool, sizeof(*client));
	client->pool     = c->pool;
	client->timeout  = mempool_pop(client->pool, sizeof(struct timeval));
	client->sock     = c;
	client->cb_error = client_error_cb;

	/* set byte counters to 0 */
	client->bytes_rx = 0;
	client->bytes_tx = 0;

	/* make streams */
	if (c->caddr == NULL) {
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

		if (c->ssl_state == -1) {
			ci_starttls(client);
		}
	}

	client->read_buffer = p_string_new(client->pool, "");
	client->write_buffer = p_string_new(client->pool, "");
	client->rev = NULL;
	client->wev = NULL;

	return client;
}

void ci_cork(ClientBase_T *s)
{
	TRACE(TRACE_DEBUG,"[%p]", s);
	if (s->rev) event_del(s->rev);
	if (s->wev) event_del(s->wev);
}

void ci_uncork(ClientBase_T *s)
{
	TRACE(TRACE_DEBUG,"[%p]", s);

	if (! (s->client_state & CLIENT_EOF))
		event_add(s->rev, s->timeout);
	event_add(s->wev, NULL);
}

int ci_starttls(ClientBase_T *self)
{
	int e;
	TRACE(TRACE_DEBUG,"[%p] ssl_state [%d]", self, self->sock->ssl_state);
	if (self->sock->ssl && self->sock->ssl_state > 0) {
		TRACE(TRACE_WARNING, "ssl already initialized");
		return DM_EGENERAL;
	}

	if (! self->sock->ssl) {
		self->sock->ssl_state = FALSE;
		if (! (self->sock->ssl = tls_setup(self->tx))) {
			return DM_EGENERAL;
		}
	}
	if (! self->sock->ssl_state) {
		if ((e = SSL_accept(self->sock->ssl)) != 1) {
			int e2;
			if ((e2 = self->cb_error(self->rx, e, (void *)self))) {
				SSL_shutdown(self->sock->ssl);
				SSL_free(self->sock->ssl);
				self->sock->ssl = NULL;
				return DM_EGENERAL;
			} else {
				return e;
			}
		}
		TRACE(TRACE_INFO,"[%p] SSL handshake successful using %s", 
				self->sock->ssl, SSL_get_cipher(self->sock->ssl));
		self->sock->ssl_state = TRUE;
		ci_write(self,NULL);
	}

	return DM_SUCCESS;
}

void ci_write_cb(ClientBase_T *self)
{
	if (p_string_len(self->write_buffer) > self->write_buffer_offset)
		ci_write(self,NULL);
}

int ci_write(ClientBase_T *self, char * msg, ...)
{
	va_list ap, cp;
	ssize_t t = 0;
	int e = 0;
	size_t n;
	char *s;

	if (self->client_state & CLIENT_ERR)
		return -1;

	if (! (self && self->write_buffer)) {
		TRACE(TRACE_DEBUG, "called while clientbase is stale");
		return -1;
	}

	if (msg) {
		va_start(ap, msg);
		va_copy(cp, ap);
		p_string_append_vprintf(self->write_buffer, msg, cp);
		va_end(cp);
	}
	
	if (p_string_len(self->write_buffer) < 1) { 
		TRACE(TRACE_DEBUG, "write_buffer is empty [%ld]", p_string_len(self->write_buffer));
		return 0;
	}

	n = p_string_len(self->write_buffer) - self->write_buffer_offset;
	if (n == 0) {
		TRACE(TRACE_DEBUG, "write_buffer is empty [%ld]", p_string_len(self->write_buffer));
		return 0;
	}

	while (n > 0) {
		if (n > TLS_SEGMENT) n = TLS_SEGMENT;

		s = (char *)p_string_str(self->write_buffer) + self->write_buffer_offset;

		TRACE(TRACE_DEBUG, "[%p] S > [%ld/%ld:%s]", self, t, p_string_len(self->write_buffer), s);

		if (self->sock->ssl) {
			if (! self->tls_wbuf_n) {
				strncpy(self->tls_wbuf, s, n);
				self->tls_wbuf_n = n;
			}
			t = SSL_write(self->sock->ssl, (gconstpointer)self->tls_wbuf, self->tls_wbuf_n);
			e = t;
		} else {
			t = write(self->tx, (gconstpointer)s, n);
			e = errno;
		}

		if (t == -1) {
			if ((e = self->cb_error(self->tx, e, (void *)self))) {
				self->client_state |= CLIENT_ERR;
			} else {
				if (self->sock->ssl && self->sock->ssl_state)
					event_add(self->wev, NULL);
			}
			return t;
		} else {
			event_add(self->wev, NULL);

			self->bytes_tx += t;	// Update our byte counter
			self->write_buffer_offset += t;
			client_wbuf_scale(self);

			if (self->sock->ssl) {
				memset(self->tls_wbuf, '\0', TLS_SEGMENT);
				self->tls_wbuf_n = 0;
			}
		}

		n = p_string_len(self->write_buffer) - self->write_buffer_offset;
	}

	return 0;
}

#define IBUFLEN 65535
void ci_read_cb(ClientBase_T *self)
{
	/* 
	 * read all available data on the input stream
	 * and store in in read_buffer
	 */
	ssize_t t = 0;
	char ibuf[IBUFLEN];

	TRACE(TRACE_DEBUG,"[%p] reset timeout [%ld]", self, self->timeout->tv_sec); 

	if (self->sock->ssl && self->sock->ssl_state == FALSE) {
		ci_starttls(self);
		return;
	}

	while (TRUE) {
		memset(ibuf, 0, sizeof(ibuf));
		if (self->sock->ssl) {
			t = SSL_read(self->sock->ssl, ibuf, sizeof(ibuf)-1);
		} else {
			t = read(self->rx, ibuf, sizeof(ibuf)-1);
		}
		TRACE(TRACE_DEBUG, "[%p] [%ld]", self, t);

		if (t < 0) {
			int e = errno;
			if ((e = self->cb_error(self->rx, e, (void *)self)))
				self->client_state |= CLIENT_ERR;
			else
				self->client_state |= CLIENT_AGAIN;
			break;

		} else if (t == 0) {
			int e = errno;
			if (self->sock->ssl)
				self->cb_error(self->rx, e, (void *)self);
			self->client_state |= CLIENT_EOF;
			break;

		} else if (t > 0) {
			self->bytes_rx += t;	// Update our byte counter
			self->client_state = CLIENT_OK; 
			p_string_append_len(self->read_buffer, ibuf, t);
		}
	}
}

int ci_read(ClientBase_T *self, char *buffer, size_t n)
{
	// fetch data from the read buffer
	assert(buffer);

	self->len = 0;
	char *s = (char *)p_string_str(self->read_buffer) + self->read_buffer_offset;
	if ((self->read_buffer_offset + n) <= p_string_len(self->read_buffer)) {
		/*
		size_t j,k = 0;

		memset(buffer, 0, sizeof(buffer));
		for (j=0; j<n; j++)
			buffer[k++] = s[j];
		*/
		memcpy(buffer, s, n);
		self->read_buffer_offset += n;
		self->len += n;
		client_rbuf_scale(self);
	}

	return self->len;
}

int ci_readln(ClientBase_T *self, char * buffer)
{
	// fetch a line from the read buffer
	char *nl;

	assert(buffer);

	self->len = 0;
	char *s = (char *)p_string_str(self->read_buffer) + self->read_buffer_offset;
	if ((nl = g_strstr_len(s, -1, "\n"))) {
		size_t j, k = 0, l;
		l = stridx(s, '\n');
		if (l >= MAX_LINESIZE) {
			TRACE(TRACE_WARNING, "insane line-length [%ld]", l);
			self->client_state = CLIENT_ERR;
			return 0;
		}
		for (j=0; j<=l; j++)
			buffer[k++] = s[j];
		self->read_buffer_offset += l+1;
		self->len = k;
		TRACE(TRACE_INFO, "[%p] C < [%ld:%s]", self, self->len, buffer);

		client_rbuf_scale(self);
	}

	return self->len;
}


void ci_authlog_init(ClientBase_T *self, const char *service, const char *username, const char *status)
{
	if ((! server_conf->authlog) || server_conf->no_daemonize == 1) return;
	Connection_T c; ResultSet_T r; PreparedStatement_T s;
	const char *now = db_get_sql(SQL_CURRENT_TIMESTAMP);
	char *frag = db_returning("id");
	const char *user = self->auth?Cram_getUsername(self->auth):username;
	c = db_con_get();
	TRY

		s = db_stmt_prepare(c, "INSERT INTO %sauthlog (userid, service, login_time, logout_time, src_ip, src_port, dst_ip, dst_port, status)"
				" VALUES (?, ?, %s, %s, ?, ?, ?, ?, ?) %s", DBPFX, now, now, frag);

		g_free(frag);
		db_stmt_set_str(s, 1, user);
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

static void ci_authlog_close(ClientBase_T *self)
{
	Connection_T c; PreparedStatement_T s;
	if (! self->authlog_id) return;
	if ((! server_conf->authlog) || server_conf->no_daemonize) return;
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

void ci_close(ClientBase_T *self)
{
	assert(self);

	TRACE(TRACE_DEBUG, "closing clientbase [%p] [%d] [%d]", self,
			self->tx, self->rx);

	ci_cork(self);

	if (self->rev) {
		event_free(self->rev);
	       	self->rev = NULL;
	}
	if (self->wev) {
		event_free(self->wev);
	       	self->wev = NULL;
	}

	if (self->tx >= 0) {
		if ((self->tx > 1) && (shutdown(self->tx, SHUT_WR)))
			TRACE(TRACE_DEBUG, "[%s]", strerror(errno));
		if (close(self->tx))
			TRACE(TRACE_DEBUG, "[%s]", strerror(errno));
	}
	if (self->rx >= 0) {
		if ((self->rx > 1) && (shutdown(self->rx, SHUT_RD)))
			TRACE(TRACE_DEBUG, "[%s]", strerror(errno));
		if (close(self->rx))
			TRACE(TRACE_DEBUG, "[%s]", strerror(errno));
	}

	ci_authlog_close(self);
	self->tx = -1;
	self->rx = -1;

	if (self->auth) {
		Cram_T c = self->auth;
		Cram_free(&c);
		self->auth = NULL;
	}

	if (self->sock->ssl) {
		SSL_shutdown(self->sock->ssl);
		SSL_free(self->sock->ssl);
	}

	p_string_free(self->read_buffer, TRUE);
	p_string_free(self->write_buffer, TRUE);
	Mempool_T pool = self->pool;
	mempool_push(pool, self, sizeof(ClientBase_T));
	mempool_close(&pool);
}


