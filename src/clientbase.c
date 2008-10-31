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
#define THIS_MODULE "clientbase"

extern serverConfig_t *server_conf;

static int client_error_cb(int sock, int error, void *arg)
{
	int r = 0;
	clientbase_t *client = (clientbase_t *)arg;
	switch (error) {
		case EAGAIN:
		case EINTR:
			break; // reschedule
		default:
			TRACE(TRACE_DEBUG,"[%p] %d %s, %p", client, sock, strerror(error), arg);
			client->write_buffer = g_string_truncate(client->write_buffer,0);
			r = -1;
			break;
	}
	return r;
}

clientbase_t * client_init(int socket, struct sockaddr_in *caddr)
{
	int err;
	clientbase_t *client	= g_new0(clientbase_t, 1);

	client->timeout       = g_new0(struct timeval,1);
	client->line_buffer	= g_string_new("");
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
		if (!(client->rx = dup(socket))) {
			err = errno;
			TRACE(TRACE_ERR, "%s", strerror(err));
			if (socket > 0) close(socket);
			g_free(client);
			return NULL;
		}

		if (!(client->tx = socket)) {
			err = errno;
			TRACE(TRACE_ERR, "%s", strerror(err));
			if (socket > 0) close(socket);
			g_free(client);
			return NULL;
		}
	}

	client->write_buffer = g_string_new("");
	client->rev = g_new0(struct event, 1);
	client->wev = g_new0(struct event, 1);

	return client;
}


int ci_write(clientbase_t *self, char * msg, ...)
{
	va_list ap;
	ssize_t t;
	if (! self) {
		TRACE(TRACE_DEBUG, "called while self is null");
		return -1;
	}

	if (msg) {
		va_start(ap, msg);
		g_string_append_vprintf(self->write_buffer, msg, ap);
		va_end(ap);
	}
	
	if (self->write_buffer->len < 1) return 0;

	t = write(self->tx, (gconstpointer)self->write_buffer->str, self->write_buffer->len);
	if (t == -1) {
		int e;
		if ((e = self->cb_error(self->tx, errno, (void *)self)))
			return e;
	} else {
		TRACE(TRACE_INFO, "[%p] S > [%s]", self, self->write_buffer->str);
		self->write_buffer = g_string_erase(self->write_buffer, 0, t);
	}

	event_add(self->wev, NULL);

	return 0;
}

int ci_read(clientbase_t *self, char *buffer, size_t n)
{
	ssize_t t = 0;
	size_t i = 0;
	char c;

	assert(buffer);
	memset(buffer, 0, sizeof(buffer));

	TRACE(TRACE_DEBUG,"[%p] need [%ld]", self, n);
	self->len = 0;
	while (self->len < n) {
		t = read(self->rx, (void *)&c, 1);
		if (t == -1) {
			int e;
			if ((e = self->cb_error(self->rx, errno, (void *)self)))
				return e;
			break;
		}
		if (t != 1)
			break;
		self->len++;
		if (c == '\r') continue;
		buffer[i++] = c;
	}
	TRACE(TRACE_DEBUG,"[%p] read [%ld]", self, self->len);

	return self->len;
}	

int ci_readln(clientbase_t *self, char * buffer)
{
	ssize_t t = 0;
	char c = 0;
	int done = FALSE, result = 0;

	assert(self->line_buffer);
	memset(buffer, 0, MAX_LINESIZE);

	if (self->line_buffer->len == 0)
		self->len = 0;

	while (self->len < MAX_LINESIZE) {
		t = read(self->rx, (void *)&c, 1);
		if (t == -1) {
			int e;
			if ((e = self->cb_error(self->rx, errno, (void *)self)))
				return e;
			event_add(self->rev, self->timeout);
		}

		if (t != 1)
			break;

		result++;
		self->len++;

		if (c=='\r') continue;
		g_string_append_c(self->line_buffer,c);
		if (c=='\n') {
			strncpy(buffer, self->line_buffer->str, MAX_LINESIZE);
			g_string_printf(self->line_buffer,"%s", "");
			done=TRUE;
			break;
		}
	}

	if (done) TRACE(TRACE_INFO, "[%p] C < [%d:%s]", self, result, buffer);

	return result;
}


void ci_close(clientbase_t *self)
{
	assert(self);

	g_async_queue_unref(self->queue);
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

	g_string_free(self->line_buffer, TRUE);
	g_string_free(self->write_buffer, TRUE);

	g_free(self);
	
	self = NULL;
}


