/*
 Copyright (C) 2008-2013 NFG Net Facilities Group BV, support@nfg.nl
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
 * implementation for http commands */

#include "dbmail.h"
#include "dm_request.h"
#include "dm_http.h"

#define THIS_MODULE "Request"

/*
 * 
 */
#define T Request_T

struct T {
	struct evhttp_request *req;
	void *data;
	uint64_t user_id;
	const char *uri;
	const char *controller;
	const char *id;
	const char *method;
	const char *arg;
	struct evkeyvalq *GET;
	struct evkeyvalq *POST;
	ClientBase_T *ci;
	void (*cb)(T);
	char **parts;
};

//--------------------------------------------------------------------------------------//
static void basic_unauth(T R, const char *realm)
{
	char *fmt = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\n"
		" \"http://www.w3.org/TR/1999/REC-html401-19991224/loose.dtd\">\n"
		"<HTML>\n"
		"  <HEAD>\n"
		"    <TITLE>Error</TITLE>\n"
		"    <META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=ISO-8859-1\">\n"
		"  </HEAD>\n"
		"  <BODY><H1>401 Unauthorised.</H1></BODY>\n"
		"</HTML>\n";

	struct evbuffer *buf = evbuffer_new();
	char *r = g_strdup_printf("Basic realm=\"%s\"", realm);
	evhttp_add_header(R->req->output_headers, "WWW-Authenticate", r);
	evbuffer_add_printf(buf,"%s", fmt);
	Request_send(R, 401, "UNAUTHORIZED", buf);
	evbuffer_free(buf);
	g_free(r);
}

static gboolean Request_user_auth(T R, char *token)
{
	gboolean r = FALSE;
	gchar **array;
	array = g_strsplit(token, ":", 2);
	if (array[0] && array[1]) {
		uint64_t user_id = 0;
		const gchar *username, *password;
		username = array[0];
		password = array[1];
		if (auth_validate(NULL, username, password, &user_id) > 0) {
			R->user_id = user_id;
			r = TRUE;
		}
	}

	g_strfreev(array);

	return r;
}

static gboolean Request_basic_auth(T R)
{
	const char *auth;
	Field_T realm;
	memset(realm,0,sizeof(Field_T));
	config_get_value("realm", "HTTP", realm);
	if (! strlen(realm))
		strcpy(realm,"DBMail HTTP Access");

	// authenticate
	if (! (auth = evhttp_find_header(R->req->input_headers, "Authorization"))) {
		TRACE(TRACE_DEBUG,"No authorization header");
		basic_unauth(R, realm);
		return FALSE;
	}
	if (strncmp(auth,"Basic ", 6) == 0) {
		Field_T userpw;
		gsize len;
		guchar *s;
		gchar *safe;
		memset(userpw,0,sizeof(Field_T));
		config_get_value("admin", "HTTP", userpw);
		auth+=6;
		TRACE(TRACE_DEBUG, "auth [%s]", auth);
		s = g_base64_decode(auth, &len);
		safe = g_strndup((char *)s, (gsize)len);
		g_free(s);
		TRACE(TRACE_DEBUG,"Authorization [%" PRIu64 "][%s] <-> [%s]", (uint64_t)len, safe, userpw);
		if ((strlen(userpw) != strlen(safe)) || (strncmp(safe,(char *)userpw,strlen(userpw))!=0)) {
			if (! Request_user_auth(R, safe)) {
				TRACE(TRACE_DEBUG,"Authorization failed");
				basic_unauth(R, realm);
				g_free(safe);
				return FALSE;
			}
		}
		g_free(safe);
	} else {
		return FALSE;
	}
	return TRUE;
}

static void Request_parse_getvars(T R)
{
	struct evkeyval *val;
	R->GET = g_new0(struct evkeyvalq,1);
	evhttp_parse_query(R->uri, R->GET);
	TAILQ_FOREACH(val, R->GET, next)
		TRACE(TRACE_DEBUG,"GET: [%s]->[%s]", val->key, val->value);
}

static void Request_parse_postvars(T R)
{
	struct evkeyval *val;
	char *post = NULL, *rawpost = NULL;
	rawpost = g_strndup((char *)EVBUFFER_DATA(R->req->input_buffer), EVBUFFER_LENGTH(R->req->input_buffer));
	if (rawpost) {
		post = evhttp_decode_uri(rawpost);
		g_free(rawpost);
	}
	R->POST = g_new0(struct evkeyvalq,1);
	TAILQ_INIT(R->POST);
	if (post) {
		int i = 0;
		char **p = g_strsplit(post,"&",0);
		while (p[i]) {
			struct evkeyval *header = g_new0(struct evkeyval,1);
			char **kv = g_strsplit(p[i],"=",2);
			if (! (kv[0] && kv[1])) break;
			header->key = kv[0];
			header->value = kv[1];
			TAILQ_INSERT_TAIL(R->POST, header, next);
			i++;
		}
		g_strfreev(p);
		g_free(post);
	}

	TAILQ_FOREACH(val, R->POST, next)
		TRACE(TRACE_DEBUG,"POST: [%s]->[%s]", val->key, val->value);

}


#define EXISTS(x) ((x) && strlen(x))
T Request_new(struct evhttp_request *req, void *data)
{
	T R;
	struct evkeyval *val;

	R = g_malloc0(sizeof(*R));
	R->req = req;
	R->data = data;
	R->uri = evhttp_decode_uri(evhttp_request_uri(R->req));
	R->parts = g_strsplit_set(R->uri,"/?",0);

	Request_parse_getvars(R);
	Request_parse_postvars(R);

	TRACE(TRACE_DEBUG,"R->uri: [%s]", R->uri);
	TAILQ_FOREACH(val, R->req->input_headers, next)
		TRACE(TRACE_DEBUG,"input_header: [%s: %s]", val->key, val->value);

	// 
	// uri-parts: /controller/id/method/arg
	//
	if (EXISTS(R->parts[1])) {
		R->controller = R->parts[1];
		TRACE(TRACE_DEBUG,"R->controller: [%s]", R->controller);
		if (EXISTS(R->parts[2])) {
			R->id = R->parts[2];
			TRACE(TRACE_DEBUG,"R->id: [%s]", R->id);
			if (EXISTS(R->parts[3])) {
				R->method = R->parts[3];
				TRACE(TRACE_DEBUG,"R->method: [%s]", R->method);
				if (EXISTS(R->parts[4])) {
					R->arg = R->parts[4];
					TRACE(TRACE_DEBUG,"R->arg: [%s]", R->arg);
				}
			}
		}
	}

	return R;
}

uint64_t Request_getUser(T R)
{
	return R->user_id;
}

const char * Request_getController(T R)
{
	return R->controller;
}

const char * Request_getId(T R)
{
	return R->id;
}

const char * Request_getMethod(T R)
{
	return R->method;
}

const char * Request_getArg(T R)
{
	return R->arg;
}

struct evkeyvalq * Request_getPOST(T R)
{
	return R->POST;
}

struct evkeyvalq * Request_getGET(T R)
{
	return R->GET;
}

void Request_send(T R, int code, const char *message, struct evbuffer *buf)
{
	evhttp_send_reply(R->req, code, message, buf);
}

void Request_error(T R, int code, const char *message)
{
        char *fmt = "<HTML><HEAD>\n"
            "<TITLE>%d %s</TITLE>\n"
            "</HEAD><BODY>\n"
            "<H1>%d %s</H1>\n"
            "</BODY></HTML>\n";

        struct evbuffer *buf = evbuffer_new();

        /* close the connection on error */
	Request_header(R, "connection", "close");
        evbuffer_add_printf(buf, fmt, code, message, code, message);
	Request_send(R, code, message, buf);
        evbuffer_free(buf);
}

void Request_header(T R, const char *name, const char *value)
{
	evhttp_add_header(R->req->output_headers, name, value);
}

void Request_setContentType(T R, const char *type)
{
	evhttp_remove_header(R->req->output_headers, "Content-type");
	Request_header(R, "Content-type", type);
}

void Request_free(T *R)
{
	T r = *R;
	g_strfreev(r->parts);
	evhttp_clear_headers(r->GET);
	evhttp_clear_headers(r->POST);
	r = NULL;
}

/* routing setup */
void Request_handle(T R)
{
	if (R->controller) {
		if (MATCH(R->controller,"users"))
			R->cb = Http_getUsers;
		else if (MATCH(R->controller,"mailboxes"))
			R->cb = Http_getMailboxes;
		else if (MATCH(R->controller,"messages"))
			R->cb = Http_getMessages;
	}

	if (R->cb) {
		if (! Request_basic_auth(R)) return;
		Request_setContentType(R,"text/html; charset=utf-8");
		R->cb(R);
	} else {
		const char *host = evhttp_find_header(R->req->input_headers, "Host");
		char *url = g_strdup_printf("http://%s%s", host?host:"","/users/");
		Request_header(R, "Location", url);
		g_free(url);
		Request_error(R, HTTP_MOVEPERM, "Not found");
	}
}


// Public
void Request_cb(struct evhttp_request *req, void * data)
{
	Request_T R = Request_new(req, data);
	Request_handle(R);
	Request_free(&R);
}


