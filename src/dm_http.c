/*
 Copyright (C) 2004-2013 NFG Net Facilities Group BV, support@nfg.nl
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

#define THIS_MODULE "Http"

/*
 * handle HTTP requests
 */

#define T Request_T

//--------------------------------------------------------------------------------------//
void Http_getUsers(T R)
{
	struct evbuffer *buf;
	char *username = NULL;
	uint64_t id = 0;

	if (Request_getId(R)) {
		/* 
		 * id can be specified both by name and number
		 *
		 * C < /users/testuser1
		 * C < /users/123
		 *
		 */

		if ((id = strtoull(Request_getId(R), NULL, 10)))
			username = auth_get_userid(id);
		else if (auth_user_exists(Request_getId(R), &id))
			username = g_strdup(Request_getId(R));

		if (! (username && id))
			Request_error(R, HTTP_NOTFOUND, "User not found");
	}

	buf = evbuffer_new();
	if (Request_getMethod(R) == NULL) {
		GList *users = NULL;

		if (username) {
			MailboxState_T M;
			const char *mailbox;
			uint64_t mboxid;
			/* 
			 * retrieve user meta-data
			 * C < /users/testuser1

			 * create/delete mailbox for user
			 * POST C < /users/testuser1

			 */

			if ((mailbox = evhttp_find_header(Request_getPOST(R),"create"))) {
				const char *message;

				if (db_mailbox_create_with_parents(mailbox, BOX_COMMANDLINE, id, &mboxid, &message)) {
					Request_error(R, HTTP_BADREQUEST, message);
					evbuffer_free(buf);
					return;
				}
			}
			if ((mailbox = evhttp_find_header(Request_getPOST(R),"delete"))) {

				int access;

				/* check if there is an attempt to delete inbox */
				if (MATCH(mailbox, "INBOX")) {
					Request_error(R, HTTP_BADREQUEST, "NO cannot delete special mailbox INBOX");
					evbuffer_free(buf);
					return;
				}

				if (! (db_findmailbox(mailbox, id, &mboxid)) ) {
					Request_error(R, HTTP_NOTFOUND, "NO mailbox doesn't exists");
					evbuffer_free(buf);
					return;
				}

				/* Check if the user has ACL delete rights to this mailbox */
				M = MailboxState_new(NULL, mboxid);
				access = acl_has_right(M, id, ACL_RIGHT_DELETE);
				MailboxState_free(&M);
				if (access != 1) {
					Request_error(R, HTTP_BADREQUEST, "NO permission denied");
					evbuffer_free(buf);
					return;
				}

				/* ok remove mailbox */
				if (db_delete_mailbox(mboxid, 0, 1)) {
					Request_error(R, HTTP_SERVUNAVAIL, "NO delete failed");
					evbuffer_free(buf);
					return;
				}
			}

			users = g_list_append_printf(users, "%s", username);
		} else {
			/* 
			 * list all users
			 * C < /users/
			 *
			 * create,edit,delete user
			 * POST C < /users/
			 */

			const char *user = NULL;

			if ((user = evhttp_find_header(Request_getPOST(R),"create"))) {
				const char *password, *encoding, *quota;
			       	password = evhttp_find_header(Request_getPOST(R), "password");
			       	encoding = evhttp_find_header(Request_getPOST(R), "encoding");
			       	quota    = evhttp_find_header(Request_getPOST(R), "quota");
				TRACE(TRACE_DEBUG, "create user: [%s] password: [%s] encoding [%s] quota [%s]", 
						user, password, encoding, quota);

			} else if ((user = evhttp_find_header(Request_getPOST(R),"edit"))) {
				TRACE(TRACE_DEBUG, "edit user: [%s]", user);

			} else if ((user = evhttp_find_header(Request_getPOST(R),"delete"))) {
				TRACE(TRACE_DEBUG, "delete user: [%s]", user);
			}

			users = auth_get_known_users();
		}

		Request_setContentType(R,"application/json; charset=utf-8");
		evbuffer_add_printf(buf, "{\"users\": {\n");
		while(users->data) {
			uint64_t id;
			if (auth_user_exists((char *)users->data, &id))
				evbuffer_add_printf(buf, "    \"%" PRIu64 "\":{\"name\":\"%s\"}", id, (char *)users->data);
			if (! g_list_next(users)) break;
			users = g_list_next(users);
			evbuffer_add_printf(buf,",\n");
		}
		evbuffer_add_printf(buf, "\n}}\n");
		g_list_destroy(users);

	} else if (MATCH(Request_getMethod(R),"mailboxes")) {
		GList *mailboxes = NULL;

		if (! username) {
			Request_error(R, HTTP_NOTFOUND, "User not found");
			evbuffer_free(buf);
			return;
		}

		/*
		 * list mailboxes for user
		 * GET C < /users/testuser1/mailboxes
		 *
		 */

		db_findmailbox_by_regex(id, "*", &mailboxes, FALSE);

		Request_setContentType(R,"application/json; charset=utf-8");
		evbuffer_add_printf(buf, "{\"mailboxes\": {\n");
		while (mailboxes->data) {
			MailboxState_T b = MailboxState_new(NULL, *((uint64_t *)mailboxes->data));
			MailboxState_setOwner(b, id);
			//if (MailboxState_reload(b) == DM_SUCCESS)
			evbuffer_add_printf(buf, "    \"%" PRIu64 "\":{\"name\":\"%s\",\"exists\":%u}", MailboxState_getId(b), MailboxState_getName(b), MailboxState_getExists(b));
			MailboxState_free(&b);
			if (! g_list_next(mailboxes)) break;
			mailboxes = g_list_next(mailboxes);
			evbuffer_add_printf(buf,",\n");
		}
		evbuffer_add_printf(buf, "\n}}\n");
	} 

	if (EVBUFFER_LENGTH(buf))
		Request_send(R, HTTP_OK, "OK", buf);
	else		
		Request_error(R, HTTP_SERVUNAVAIL, "Server error");

	if (username) g_free(username);
	evbuffer_free(buf);
}

//--------------------------------------------------------------------------------------//
void Http_getMailboxes(T R)
{
	const char *mailbox = Request_getId(R);

	TRACE(TRACE_DEBUG,"mailbox [%s]", mailbox);
	char *endptr = NULL;
	struct evbuffer *buf;
	uint64_t id = 0;

	if (! mailbox) {
		Request_error(R, HTTP_SERVUNAVAIL, "Server error");
		return;
	}	

	if (! (id = strtoull(mailbox, &endptr, 10))) {
		Request_error(R, HTTP_NOTFOUND, "Not found");
		return;
	}

	TRACE(TRACE_DEBUG,"mailbox id [%" PRIu64 "]", id);
	buf = evbuffer_new();
	Request_setContentType(R,"application/json; charset=utf-8");

	if (Request_getMethod(R) == NULL) {

		/*
		 * retrieve mailbox meta-data
		 * C < GET /mailboxes/876
		 *
		 * or
		 *
		 * append a new message
		 * C < POST /mailboxes/876 
		 */

		const char *msg;
		uint64_t msg_id = 0;
		MailboxState_T b = MailboxState_new(NULL, id);
		unsigned exists = MailboxState_getExists(b);

		if ((msg = evhttp_find_header(Request_getPOST(R),"message"))) {
			if (! db_append_msg(msg, MailboxState_getId(b), MailboxState_getOwner(b), NULL, &msg_id, TRUE))
				exists++;		
		}
		evbuffer_add_printf(buf, "{\"mailboxes\": {\n");
		evbuffer_add_printf(buf, "    \"%" PRIu64 "\":{\"name\":\"%s\",\"exists\":%d}", MailboxState_getId(b), MailboxState_getName(b), exists);
		evbuffer_add_printf(buf, "\n}}\n");
		MailboxState_free(&b);

	} else if (MATCH(Request_getMethod(R),"messages")) {

		/*
		 * list messages in mailbox
		 * C < GET /mailboxes/876/messages
		 */

		MailboxState_T b = MailboxState_new(NULL, id);
		GTree *msns = MailboxState_getMsn(b);
		GList *ids = g_tree_keys(msns);
		GTree *msginfo = MailboxState_getMsginfo(b);

		evbuffer_add_printf(buf, "{\"messages\": {\n");
		while (ids && ids->data) {
			uint64_t *msn = (uint64_t *)ids->data;
			uint64_t *uid = (uint64_t *)g_tree_lookup(msns, msn);
			MessageInfo *info = (MessageInfo *)g_tree_lookup(msginfo, uid);
			evbuffer_add_printf(buf, "    \"%" PRIu64 "\":{\"size\":%" PRIu64 "}", *uid, info->rfcsize);
			if (! g_list_next(ids)) break;
			ids = g_list_next(ids);
			evbuffer_add_printf(buf,",\n");
		}
		evbuffer_add_printf(buf, "\n}}\n");
	
		if (ids) g_list_free(g_list_first(ids));
		MailboxState_free(&b);
	}

	if (EVBUFFER_LENGTH(buf))
		Request_send(R, HTTP_OK, "OK", buf);
	else		
		Request_error(R, HTTP_SERVUNAVAIL, "Server error");
	evbuffer_free(buf);
}

//--------------------------------------------------------------------------------------//




void Http_getMessages(T R)
{
	DbmailMessage *m;
	struct evbuffer *buf;
	uint64_t pid;
	uint64_t id = 0;

	if (! Request_getId(R)) return;

	if (! (id = strtoull(Request_getId(R), NULL, 10))) {
		Request_error(R, HTTP_NOTFOUND, "Not found");
		return;
	}

	db_get_physmessage_id(id, &pid);
	if (! pid) {
		Request_error(R, HTTP_NOTFOUND, "Not found");
		return;
	}
	buf = evbuffer_new();
	m = dbmail_message_new(NULL);
	m = dbmail_message_retrieve(m, pid);
	if (Request_getMethod(R) == NULL) {

		/*
		 * retrieve message meta-data
		 * C < GET /messages/1245911
		 */

		uint64_t size = dbmail_message_get_size(m, TRUE);
		Request_setContentType(R,"application/json; charset=utf-8");
		evbuffer_add_printf(buf, "{\"messages\": {\n");
		evbuffer_add_printf(buf, "   \"%" PRIu64 "\":{\"size\":%" PRIu64 "}", id, size);
		evbuffer_add_printf(buf, "\n}}\n");

	} else if (MATCH(Request_getMethod(R), "view")) {

		/*
		 * retrieve message by message_idnr
		 * C < GET /messages/1245911/view
		 */

		char *s = dbmail_message_to_string(m);
		Request_setContentType(R, "message/rfc822; charset=utf-8");
		evbuffer_add_printf(buf, "%s", s);
		g_free(s);

	} else if (MATCH(Request_getMethod(R),"headers")) {

		Request_setContentType(R, "text/plain; charset=utf-8");
		if (Request_getArg(R) && strlen(Request_getArg(R))) {

			/*
			 * retrieve selected message headers 
			 * C < GET /messages/1245911/headers/subject,from,to
			 */

			int i = 0;
			char **headerlist = g_strsplit(Request_getArg(R),",",0);
			while (headerlist[i]) {
				char *hname = headerlist[i];
				hname[0] = g_ascii_toupper(hname[0]);
				TRACE(TRACE_DEBUG,"header: [%s]", headerlist[i]);
				GList * headers = dbmail_message_get_header_repeated(m, headerlist[i]);

				while(headers) {
					evbuffer_add_printf(buf, "%s: %s\n", hname, (char *)headers->data);
					if (! g_list_next(headers))
						break;
					headers = g_list_next(headers);
				}
				i++;
				g_list_free(g_list_first(headers));
			}
		} else {

			/*
			 * retrieve all message headers
			 * C < GET /messages/1245911/headers
			 */

			char *s = dbmail_message_hdrs_to_string(m);
			Request_setContentType(R, "text/plain; charset=utf-8");
			evbuffer_add_printf(buf, "%s", s);
			g_free(s);
		}
	}

	if (EVBUFFER_LENGTH(buf))
		Request_send(R, HTTP_OK, "OK", buf);
	else		
		Request_error(R, HTTP_SERVUNAVAIL, "Server error");

	evbuffer_free(buf);
	dbmail_message_free(m);
}

