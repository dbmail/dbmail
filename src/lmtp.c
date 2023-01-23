/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
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

/* implementation for lmtp commands according to RFC 1081 */

#include "dbmail.h"
#define THIS_MODULE "lmtp"

#define MAX_ERRORS 3

extern ServerConfig_T *server_conf;

/* allowed lmtp commands */
static const char *const commands[] = {
	"LHLO", 
	"QUIT", 
	"RSET", 
	"DATA", 
	"MAIL",
	"VRFY", 
	"EXPN", 
	"HELP", 
	"NOOP", 
	"RCPT",
	NULL
};

typedef enum {
	LMTP_LHLO,
	LMTP_QUIT,
	LMTP_RSET,
	LMTP_DATA,
	LMTP_MAIL,
	LMTP_VRFY,
	LMTP_EXPN,
	LMTP_HELP,
	LMTP_NOOP,
	LMTP_RCPT,
	LMTP_END
} command_t;

int lmtp(ClientSession_T *session);

static int lmtp_tokenizer(ClientSession_T *session, char *buffer);

void send_greeting(ClientSession_T *session)
{
	Field_T banner;
	GETCONFIGVALUE("banner", "LMTP", banner);
	if (! dm_db_ping()) {
		ci_write(session->ci, "500 database has gone fishing BYE\r\n");
		session->state = CLIENTSTATE_QUIT;
		return;
	}

	if (strlen(banner) > 0)
		ci_write(session->ci, "220 %s %s\r\n", session->hostname, banner);
	else
		ci_write(session->ci, "220 %s LMTP\r\n", session->hostname);
}

static void lmtp_cb_time(void *arg)
{
	ClientSession_T *session = (ClientSession_T *)arg;
	ci_write(session->ci, "221 Connection timeout BYE\r\n");
	client_session_bailout(&session);
}
		
static void lmtp_handle_input(void *arg)
{
	int l;
	char buffer[MAX_LINESIZE];	/* connection buffer */
	ClientSession_T *session = (ClientSession_T *)arg;
	while (TRUE) {
		memset(buffer, 0, sizeof(buffer));

		l = ci_readln(session->ci, buffer);

		if (l==0) break;

		if ((l = lmtp_tokenizer(session, buffer))) {
			if (l == -3) {
				client_session_bailout(&session);
				return;
			}

			if (l > 0) {
				ci_cork(session->ci);
				if (lmtp(session) == -3) {
					client_session_bailout(&session);
					return;
				}
				ci_uncork(session->ci);
				client_session_reset_parser(session);
			}

			if (l < 0) {
				client_session_reset_parser(session);
			}
		}
	}

	TRACE(TRACE_DEBUG,"[%p] done", session);
}

void lmtp_cb_write(void *arg)
{
	ClientSession_T *session = (ClientSession_T *)arg;
	int state = session->state;

	TRACE(TRACE_DEBUG, "[%p] state: [%d]", session, state);

	switch (state) {
		case CLIENTSTATE_QUIT_QUEUED:
		case CLIENTSTATE_QUIT:
			break;
		default:
			if (p_string_len(session->ci->write_buffer) > session->ci->write_buffer_offset) {
				ci_write(session->ci,NULL);
				break;
			}
			lmtp_handle_input(session);
			break;
	}
}

static void reset_callbacks(ClientSession_T *session)
{
        session->ci->cb_time = lmtp_cb_time;
        session->ci->cb_write = lmtp_cb_write;
	session->handle_input = lmtp_handle_input;

        UNBLOCK(session->ci->rx);
        UNBLOCK(session->ci->tx);

	ci_uncork(session->ci);
}

static void lmtp_rset(ClientSession_T *session, gboolean reset_state)
{
	int state = session->state;

	client_session_reset(session);
	if (reset_state)
		session->state = CLIENTSTATE_AUTHENTICATED;
	else
		session->state = state;
}


// socket callbacks.

int lmtp_handle_connection(client_sock *c)
{
	ClientSession_T *session = client_session_new(c);
	client_session_set_timeout(session, server_conf->login_timeout);
        send_greeting(session);
	reset_callbacks(session);
	return 0;
}

int lmtp_error(ClientSession_T * session, const char *formatstring, ...)
{
	va_list ap, cp;
	char *s;

	if (session->error_count >= MAX_ERRORS) {
		ci_write(session->ci, "500 Too many errors, closing connection.\r\n");
		session->SessionResult = 2;	/* possible flood */
		return -3;
	}

	va_start(ap, formatstring);
	va_copy(cp, ap);
	s = g_strdup_vprintf(formatstring, cp);
	va_end(cp);
	va_end(ap);
	ci_write(session->ci, s);
	g_free(s);

	if (session->ci->client_state & CLIENT_ERR) {
		return -3;
	}

	session->error_count++;
	return -1;
}

int lmtp_tokenizer(ClientSession_T *session, char *buffer)
{
	char *command = NULL, *value;
	int command_type = 0;

	if (! session->command_type) {
		session->parser_state = FALSE;

		command = buffer;
		strip_crlf(command);
		g_strstrip(command);
		if (! strlen(command)) return FALSE; /* ignore empty commands */

		value = strstr(command, " ");	/* look for the separator */

		if (value) {
			*value++ = '\0';	/* set a \0 on the command end */

			if (strlen(value) == 0)
				value = NULL;	/* no value specified */
		}

		for (command_type = LMTP_LHLO; command_type < LMTP_END; command_type++)
			if (strcasecmp(command, commands[command_type]) == 0)
				break;

		/* Invalid command */
		if (command_type == LMTP_END)
			return lmtp_error(session, "500 Invalid command.\r\n");

		/* Commands that are allowed to have no arguments */
		if ((value == NULL) &&
		    !((command_type == LMTP_LHLO) || (command_type == LMTP_DATA) ||
		      (command_type == LMTP_RSET) || (command_type == LMTP_QUIT) ||
		      (command_type == LMTP_NOOP) || (command_type == LMTP_HELP) )) {
			return lmtp_error(session, "500 This command requires an argument.\r\n");
		}
		session->command_type = command_type;

		if (value) {
			String_T s = p_string_new(session->pool, value);
			session->args = p_list_append(session->args, s);
		}

	}

	if (session->command_type == LMTP_DATA) {
		if (command) {
			int state = session->state;

			if (state != CLIENTSTATE_AUTHENTICATED) {
				return lmtp_error(session, "550 Command out of sequence\r\n");
			}
			if (p_list_length(session->rcpt) < 1) {
				return lmtp_error(session, "503 No valid recipients\r\n");
			}
			if (p_list_length(session->from) < 1) {
				return lmtp_error(session, "554 No valid sender.\r\n");
			}
			ci_write(session->ci, "354 Start mail input; end with <CRLF>.<CRLF>\r\n");
			return FALSE;
		}

		if (strncmp(buffer,".\n",2)==0 || strncmp(buffer,".\r\n",3)==0)
			session->parser_state = TRUE;
		else if (strncmp(buffer,".",1)==0)
			p_string_append(session->rbuff, &buffer[1]);
		else
			p_string_append(session->rbuff, buffer);
	} else
		session->parser_state = TRUE;

	TRACE(TRACE_DEBUG, "[%p] cmd [%d], state [%d] [%s]", session, session->command_type, session->parser_state, buffer);

	return session->parser_state;
}


int lmtp(ClientSession_T * session)
{
	DbmailMessage *msg;
	ClientBase_T *ci = session->ci;
	int helpcmd;
	const char *class, *subject, *detail;
	size_t tmplen = 0, tmppos = 0;
	char *tmpaddr = NULL, *tmpbody = NULL, *arg;
	int state = 0;

	switch (session->command_type) {

	case LMTP_QUIT:
		ci_write(ci, "221 %s BYE\r\n", session->hostname);
		session->state = CLIENTSTATE_QUIT;
		return 1;

	case LMTP_NOOP:
		ci_write(ci, "250 OK\r\n");
		return 1;

	case LMTP_RSET:
		ci_write(ci, "250 OK\r\n");
		lmtp_rset(session,TRUE);
		return 1;

	case LMTP_LHLO:
		/* Reply wth our hostname and a list of features.
		 * The RFC requires a couple of SMTP extensions
		 * with a MUST statement, so just hardcode them.
		 * */
		ci_write(ci, "250-%s\r\n250-PIPELINING\r\n"
			"250-ENHANCEDSTATUSCODES\r\n250 SIZE\r\n", 
			session->hostname);
				/* This is a SHOULD implement:
				 * "250-8BITMIME\r\n"
				 * Might as well do these, too:
				 * "250-CHUNKING\r\n"
				 * "250-BINARYMIME\r\n"
				 * */
		client_session_reset(session);
		session->state = CLIENTSTATE_AUTHENTICATED;
		client_session_set_timeout(session, server_conf->timeout);

		return 1;

	case LMTP_HELP:
	
		session->args = p_list_first(session->args);
		if (p_list_data(session->args)) {
			String_T s = p_list_data(session->args);
			arg = (char *)p_string_str(s);
		} else {
			arg = NULL;
		}

		if (arg == NULL)
			helpcmd = LMTP_END;
		else
			for (helpcmd = LMTP_LHLO; helpcmd < LMTP_END; helpcmd++)
				if (strcasecmp (arg, commands[helpcmd]) == 0)
					break;

		TRACE(TRACE_DEBUG, "LMTP_HELP requested for commandtype %d", helpcmd);

		if ((helpcmd == LMTP_LHLO) || (helpcmd == LMTP_DATA) || 
			(helpcmd == LMTP_RSET) || (helpcmd == LMTP_QUIT) || 
			(helpcmd == LMTP_NOOP) || (helpcmd == LMTP_HELP)) {
			ci_write(ci, "%s", LMTP_HELP_TEXT[helpcmd]);
		} else
			ci_write(ci, "%s", LMTP_HELP_TEXT[LMTP_END]);
		return 1;

	case LMTP_VRFY:
		/* RFC 2821 says this SHOULD be implemented...
		 * and the goal is to say if the given address
		 * is a valid delivery address at this server. */
		ci_write(ci, "502 Command not implemented\r\n");
		return 1;

	case LMTP_EXPN:
		/* RFC 2821 says this SHOULD be implemented...
		 * and the goal is to return the membership
		 * of the specified mailing list. */
		ci_write(ci, "502 Command not implemented\r\n");
		return 1;

	case LMTP_MAIL:
		/* We need to LHLO first because the client
		 * needs to know what extensions we support.
		 * */
		state = session->state;

		if (state != CLIENTSTATE_AUTHENTICATED) {
			ci_write(ci, "550 Command out of sequence.\r\n");
			return 1;
		} 
		if (p_list_length(session->from) > 0) {
			ci_write(ci, "500 Sender already received. Use RSET to clear.\r\n");
			return 1;
		}
		/* First look for an email address.
		 * Don't bother verifying or whatever,
		 * just find something between angle brackets!
		 * */

		session->args = p_list_first(session->args);
		if (p_list_data(session->args)) {
			String_T s = p_list_data(session->args);
			arg = (char *)p_string_str(s);
		} else {
			arg = NULL;
		}


		if (! arg)
			return 1;

		if (find_bounded(arg, '<', '>', &tmpaddr, &tmplen, &tmppos) < 0) {
			ci_write(ci, "500 No address found. Missing <> boundries.\r\n");
			return 1;
		}

		/* Second look for a BODY keyword.
		 * See if it has an argument, and if we
		 * support that feature. Don't give an OK
		 * if we can't handle it yet, like 8BIT!
		 * */

		/* Find the '=' following the address
		 * then advance one character past it
		 * (but only if there's more string!)
		 * */
		if ((tmpbody = strstr(arg + tmppos, "=")) != NULL)
			if (strlen(tmpbody))
				tmpbody++;

		/* This is all a bit nested now... */
		if (tmpbody) {
			if (MATCH(tmpbody, "8BITMIME")) {   // RFC1652
				ci_write(ci, "500 Please use 7BIT MIME only.\r\n");
				return 1;
			}
			if (MATCH(tmpbody, "BINARYMIME")) { // RFC3030
				ci_write(ci, "500 Please use 7BIT MIME only.\r\n");
				return 1;
			}
		}

		String_T s = p_string_new(session->pool, tmpaddr);
		g_free(tmpaddr);

		ci_write(ci, "250 Sender <%s> OK\r\n", p_string_str(s));

		session->from = p_list_prepend(session->from, s);

		return 1;

	case LMTP_RCPT:
		state = session->state;

		if (state != CLIENTSTATE_AUTHENTICATED) {
			ci_write(ci, "550 Command out of sequence.\r\n");
			return 1;
		} 

		session->args = p_list_first(session->args);
		if (p_list_data(session->args)) {
			String_T s = p_list_data(session->args);
			arg = (char *)p_string_str(s);
		} else {
			arg = NULL;
		}

		if (! arg)
			return 1;

		if (find_bounded(arg, '<', '>', &tmpaddr, &tmplen, &tmppos) < 0 || tmplen < 1) {
			ci_write(ci, "500 No address found. Missing <> boundries or address is null.\r\n");
			return 1;
		}

		Delivery_T *dsnuser = g_new0(Delivery_T,1);

		dsnuser_init(dsnuser);

		/* find_bounded() allocated tmpaddr for us, and that's ok
		 * since dsnuser_free() will free it for us later on. */
		dsnuser->address = tmpaddr;

		if (dsnuser_resolve(dsnuser) != 0) {
			TRACE(TRACE_ERR, "dsnuser_resolve_list failed");
			ci_write(ci, "430 Temporary failure in recipient lookup\r\n");
			dsnuser_free(dsnuser);
			g_free(dsnuser);
			return 1;
		}

		/* Class 2 means the address was deliverable in some way. */
		switch (dsnuser->dsn.class) {
			case DSN_CLASS_OK:
				ci_write(ci, "250 Recipient <%s> OK\r\n", dsnuser->address);
				session->rcpt = p_list_append(session->rcpt, dsnuser);
				break;
			default:
				ci_write(ci, "550 Recipient <%s> FAIL\r\n", dsnuser->address);
				dsnuser_free(dsnuser);
				g_free(dsnuser);
				break;
		}
		return 1;

	/* Here's where it gets really exciting! */
	case LMTP_DATA:
		msg = dbmail_message_new(NULL);
		dbmail_message_init_with_string(msg, p_string_str(session->rbuff));
		if (p_list_data(session->from))
			dbmail_message_set_header(msg, "Return-Path", 
					(char *)p_string_str(p_list_data(session->from)));
		p_string_truncate(session->rbuff,0);

		if (insert_messages(msg, session->rcpt) == -1) {
			ci_write(ci, "430 Message not received\r\n");
			dbmail_message_free(msg);
			return 1;
		}
		/* The DATA command itself it not given a reply except
		 * that of the status of each of the remaining recipients. */

		/* The replies MUST be in the order received */
		session->rcpt = p_list_first(session->rcpt);
		while (session->rcpt) {
			Delivery_T * dsnuser = (Delivery_T *)p_list_data(session->rcpt);
			dsn_tostring(dsnuser->dsn, &class, &subject, &detail);

			/* Give a simple OK, otherwise a detailed message. */
			switch (dsnuser->dsn.class) {
				case DSN_CLASS_OK:
					ci_write(ci, "%d%d%d Recipient <%s> OK\r\n",
							dsnuser->dsn.class, dsnuser->dsn.subject, dsnuser->dsn.detail,
							dsnuser->address);
					break;
				default:
					ci_write(ci, "%d%d%d Recipient <%s> %s %s %s\r\n",
							dsnuser->dsn.class, dsnuser->dsn.subject, dsnuser->dsn.detail,
							dsnuser->address, class, subject, detail);
			}

			if (! p_list_next(session->rcpt))
				break;
			session->rcpt = p_list_next(session->rcpt);
		}
		dbmail_message_free(msg);
		/* Reset the session after a successful delivery;
		 * MTA's like Exim prefer to immediately begin the
		 * next delivery without an RSET or a reconnect. */
		lmtp_rset(session,TRUE);
		return 1;

	default:
		return lmtp_error(session, "500 What are you trying to say here?\r\n");

	}
	return 1;
}

