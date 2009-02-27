/*
  
 Copyright (c) 2004-2008 NFG Net Facilities Group BV support@nfg.nl

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

static void client_wbuf_clear(clientbbase_t *client)
{
	if (client->write_buffer)
		client->write_buffer = g_string_truncate(client->write_buffer,0);

}

static void client_rbuf_clear(clientbbase_t *client)
{
	if (client->read_buffer)
		client->read_buffer = g_string_truncate(client->read_buffer,0);

}

static void client_rbuff_scale(client_base_t *self)
{
	if (self->read_buffer_offset == self->read_buffer->len) {
		g_string_truncate(self->read_buffer,0);
		self->read_buffer_offset = 0;
	} else if (self->read_buffer_offset >= WATERMARK) {
		g_string_erase(self->read_buffer,0, WATERMARK);
		self->read_buffer_offset -= WATERMARK;
	}
}

static void client_wbuff_scale(client_base_t *self)
{
	if (self->write_buffer_offset == self->write_buffer->len) {
		g_string_truncate(self->write_buffer,0);
		self->write_buffer_offset = 0;
	} else if (self->write_buffer_offset >= WATERMARK) {
		g_string_erase(self->write_buffer,0, WATERMARK);
		self->write_buffer_offset -= WATERMARK;
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
				client_wbuf_clear(client);
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
				TRACE(TRACE_DEBUG,"[%p] %d %s[%d], %p", client, sock, strerror(error), error, arg);
				client_wbuf_clear(client);
				client_wbuf_clear(client);
				r = -1;
				break;
		}
	}
	return r;
}

clientbase_t * client_init(int socket, struct sockaddr_in *caddr, SSL *ssl)
{
	clientbase_t *client	= g_new0(clientbase_t, 1);

	client->timeout       = g_new0(struct timeval,1);

	if (g_thread_supported())
		client->queue           = g_async_queue_new();
	client->cb_error        = client_error_cb;

	/* make streams */
	if (socket == 0 && caddr == NULL) {
		client->rx		= STDIN_FILENO;
		client->tx		= STDOUT_FILENO;
	} else {
		strncpy((char *)client->ip_src, inet_ntoa(caddr->sin_addr), sizeof(client->ip_src));
		client->ip_src_port = ntohs(caddr->sin_port);

		if (server_conf->resolveIP) {
			struct hostent *clientHost;
			clientHost = gethostbyaddr((gpointer) &(caddr->sin_addr), sizeof(caddr->sin_addr), caddr->sin_family);

			if (clientHost && clientHost->h_name)
				strncpy((char *)client->clientname, clientHost->h_name, FIELDSIZE);

			TRACE(TRACE_NOTICE, "incoming connection from [%s:%d (%s)] by pid [%d]",
					client->ip_src, client->ip_src_port,
					client->clientname[0] ? client->clientname : "Lookup failed", getpid());
		} else {
			TRACE(TRACE_NOTICE, "incoming connection from [%s:%d] by pid [%d]",
					client->ip_src, client->ip_src_port, getpid());
		}

		/* make streams */
		client->rx = client->tx = socket;
		client->ssl = ssl;
		if (ssl)
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
	
	if (self->write_buffer->len < 1) return 0;

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
		t = write(self->tx, (gconstpointer)self->write_buffer->str, n);
		e = errno;
	}

	if (t == -1) {
		if ((e = self->cb_error(self->tx, e, (void *)self)))
			return e;
	} else {
		TRACE(TRACE_INFO, "[%p] S > [%ld/%ld:%s]", self, self->write_buffer_offset, self->write_buffer->len, s);


		if (self->ssl) {
			memset(self->tls_wbuf, '\0', TLS_SEGMENT);
			self->tls_wbuf_n = 0;
		}
		self->write_buffer_offset += t;
		client_wbuff_scale(self);
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

	if (self->ssl && self->ssl_state == FALSE) {
		ci_starttls(self);
		event_add(self->rev, self->timeout);
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
			self->client_state = CLIENT_OK; 
			g_string_append_len(self->read_buffer, ibuf, t);
			TRACE(TRACE_DEBUG,"read [%ld:%s]", t, ibuf);
		}
	}

	TRACE(TRACE_DEBUG,"[%p] state [%x] read_buffer->len[%ld]", self, self->client_state, self->read_buffer->len);
}

int ci_read(clientbase_t *self, char *buffer, size_t n)
{
	assert(buffer);

	TRACE(TRACE_DEBUG,"[%p] need [%ld]", self, n);
	self->len = 0;

	char *s = self->read_buffer->str + self->read_buffer_offset;
	if ((self->read_buffer_offset + n) <= self->read_buffer->len) {
		size_t j,k = 0;
		char c;

		memset(buffer, 0, sizeof(buffer));
		for (j=0; j<n; j++) {
			c = s[j];
			if (c == '\r') continue;
			buffer[k++] = c;
		}
		self->read_buffer_offset += n;
		self->len += j;
		client_rbuff_scale(self);
	}

	if (self->len)
		TRACE(TRACE_DEBUG,"[%p] read [%ld][%s]", self, self->len, buffer);

	return self->len;
}

int ci_readln(clientbase_t *self, char * buffer)
{
	char *nl;

	assert(buffer);

	self->len = 0;
	char *s = self->read_buffer->str + self->read_buffer_offset;
	if ((nl = g_strstr_len(s, -1, "\n"))) {
		char c = 0;
		size_t j, k = 0, l;
		l = stridx(s, '\n');
		if (l >= MAX_LINESIZE) {
			TRACE(TRACE_ERR, "insane line-length [%ld]", l);
			self->client_state = CLIENT_ERR;
			return 0;
		}
		for (j=0; j<=l; j++) {
			c = s[j];
			if (c == '\r') continue;
			buffer[k++] = c;
		}
		self->read_buffer_offset += l+1;
		self->len = k;
		TRACE(TRACE_INFO, "[%p] C < %ld:[%s]", self, self->len, buffer);

		if (self->read_buffer_offset == self->read_buffer->len) {
			g_string_truncate(self->read_buffer,0);
			self->read_buffer_offset = 0;
		}
	}

	return self->len;
}


void ci_close(clientbase_t *self)
{
	assert(self);

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

	self->tx = -1;
	self->rx = -1;

	g_string_free(self->read_buffer, TRUE);
	g_string_free(self->write_buffer, TRUE);

	g_free(self->timeout);
	g_free(self);
	
	self = NULL;
}


