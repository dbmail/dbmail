/*
 Copyright (C) 2004 IC & S dbmail@ic-s.nl

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

#define INCOMING_BUFFER_SIZE 512

/* default timeout for server daemon */
#define DEFAULT_SERVER_TIMEOUT 300

/* max_errors defines the maximum number of allowed failures */
#define MAX_ERRORS 3

/* max_in_buffer defines the maximum number of bytes that are allowed to be
 * in the incoming buffer */
#define MAX_IN_BUFFER 255

#define THIS_MODULE "lmtp"

extern volatile sig_atomic_t alarm_occured;

/* These are needed across multiple calls to lmtp() */
static struct dm_list from, rcpt;

/* allowed lmtp commands */
static const char *const commands[] = {
	"LHLO", "QUIT", "RSET", "DATA", "MAIL",
	"VRFY", "EXPN", "HELP", "NOOP", "RCPT"
};

static char myhostname[64];

/**
 * \function lmtp_error
 *
 * report an LMTP error
 * \param session current LMTP session
 * \param stream stream to right to
 * \param formatstring format string
 * \param ... values to fill up formatstring
 */
int lmtp_error(PopSession_t * session, void *stream,
	       const char *formatstring, ...) PRINTF_ARGS(3, 4);

/**
 * initialize a new session. Sets all relevant variables in session
 * \param[in,out] session to initialize
 */
void lmtp_init(PopSession_t *session) 
{
	/* setting Session variables */
	session->state = STRT;
	session->error_count = 0;

	session->username = NULL;
	session->password = NULL;

	session->SessionResult = 0;

	/* reset counters */
	session->totalsize = 0;
	session->virtual_totalsize = 0;
	session->totalmessages = 0;
	session->virtual_totalmessages = 0;

	/* set the lists to zero length */
	dm_list_init(&rcpt);
	dm_list_init(&from);
}

int lmtp_reset(PopSession_t * session)
{
	if (dm_list_length(&rcpt) > 0) {
		dsnuser_free_list(&rcpt);
	}
	dm_list_init(&rcpt);

	if (dm_list_length(&from) > 0) {
		dm_list_free(&from.start);
	}
	dm_list_init(&from);

	session->state = LHLO;

	return 1;
}


int lmtp_handle_connection(clientinfo_t * ci)
{
	/*
	   Handles connection and calls
	   lmtp command handler
	 */

	int done = 1;		/* loop state */
	char *buffer = NULL;	/* connection buffer */
	int cnt;		/* counter */

	PopSession_t session;	/* current connection session */
	
	lmtp_init(&session);

	/* getting hostname */
	gethostname(myhostname, 64);
	myhostname[63] = 0;	/* make sure string is terminated */

	buffer = g_new0(char, INCOMING_BUFFER_SIZE);

	if (!buffer) {
		TRACE(TRACE_MESSAGE, "Could not allocate buffer");
		return 0;
	}

	if (ci->tx) {
		/* sending greeting */
		ci_write(ci->tx, "220 %s DBMail LMTP service ready to rock\r\n", myhostname);
		fflush(ci->tx);
	} else {
		TRACE(TRACE_MESSAGE, "TX stream is null!");
		g_free(buffer);
		return 0;
	}

	lmtp_reset(&session);
	while (done > 0) {

		if (db_check_connection()) {
			TRACE(TRACE_DEBUG,"database has gone away");
			done=-1;
			break;
		}

		/* set the timeout counter */
		alarm(ci->timeout);

		/* clear the buffer */
		memset(buffer, 0, INCOMING_BUFFER_SIZE);

		for (cnt = 0; cnt < INCOMING_BUFFER_SIZE - 1; cnt++) {
			do {
				clearerr(ci->rx);
				fread(&buffer[cnt], 1, 1, ci->rx);

				/* leave, an alarm has occured during fread */
				if (alarm_occured) {
					alarm_occured = 0;
					client_close();
					g_free(buffer);
					return 0;
				}
			} while (ferror(ci->rx) && errno == EINTR);

			if (buffer[cnt] == '\n' || feof(ci->rx)
			    || ferror(ci->rx)) {
				buffer[cnt + 1] = '\0';
				break;
			}
		}

		if (feof(ci->rx) || ferror(ci->rx)) {
			/* check client eof  */
			done = -1;
		} else {
			/* reset function handle timeout */
			alarm(0);
			/* handle lmtp commands */
			done = lmtp(ci->tx, ci->rx, buffer, ci->ip_src, &session);
		}
		fflush(ci->tx);
	}

	/* memory cleanup */
	lmtp_reset(&session);
	g_free(buffer);
	buffer = NULL;

	/* reset timers */
	alarm(0);

	return 0;
}

int lmtp_error(PopSession_t * session, void *stream,
	       const char *formatstring, ...)
{
	va_list argp;

	if (session->error_count >= MAX_ERRORS) {
		TRACE(TRACE_MESSAGE, "too many errors (MAX_ERRORS is %d)", MAX_ERRORS);
		ci_write((FILE *) stream, "500 Too many errors, closing connection.\r\n");
		session->SessionResult = 2;	/* possible flood */
		lmtp_reset(session);
		return -3;
	} else {
		va_start(argp, formatstring);
		if (vfprintf((FILE *) stream, formatstring, argp) < 0) {
			va_end(argp);
			TRACE(TRACE_ERROR, "error writing to stream");
			return -1;
		}
		va_end(argp);
	}

	TRACE(TRACE_DEBUG, "an invalid command was issued");
	session->error_count++;
	return 1;
}


int lmtp(void *stream, void *instream, char *buffer,
	 char *client_ip UNUSED, PopSession_t * session)
{
	/* returns values:
	 *  0 to quit
	 * -1 on failure
	 *  1 on success */
	char *command, *value;
	int cmdtype;
	int indx = 0;

	/* buffer overflow attempt */
	if (strlen(buffer) > MAX_IN_BUFFER) {
		TRACE(TRACE_DEBUG, "buffer overflow attempt");
		return -3;
	}

	/* check for command issued */
	while (strchr(ValidNetworkChars, buffer[indx]))
		indx++;

	/* end buffer */
	buffer[indx] = '\0';

	TRACE(TRACE_DEBUG, "incoming buffer: [%s]", buffer);

	command = buffer;

	value = strstr(command, " ");	/* look for the separator */

	if (value != NULL) {
		*value = '\0';	/* set a \0 on the command end */
		value++;	/* skip space */

		if (strlen(value) == 0) {
			value = NULL;	/* no value specified */
		} else {
			TRACE(TRACE_DEBUG, "command issued :cmd [%s], value [%s]\n",
			      command, value);
		}
	}

	for (cmdtype = LMTP_STRT; cmdtype < LMTP_END; cmdtype++)
		if (strcasecmp(command, commands[cmdtype]) == 0)
			break;

	TRACE(TRACE_DEBUG, "command looked up as commandtype %d", cmdtype);

	/* Invalid command */
	if (cmdtype == LMTP_END) {
		TRACE(TRACE_INFO, "Client gave an invalid command [%s], protocol error", command);
		return lmtp_error(session, stream, "500 Invalid command.\r\n");
	}

	/* Commands that are allowed to have no arguments */
	if ((value == NULL) &&
	    !((cmdtype == LMTP_LHLO) || (cmdtype == LMTP_DATA) ||
	      (cmdtype == LMTP_RSET) || (cmdtype == LMTP_QUIT) ||
	      (cmdtype == LMTP_NOOP) || (cmdtype == LMTP_HELP) )) {
		TRACE(TRACE_INFO, "Client gave command [%s] without any arguments, protocol error", command);
		return lmtp_error(session, stream, "500 This command requires an argument.\r\n");
	}

	switch (cmdtype) {
	case LMTP_QUIT:
		{
			ci_write((FILE *) stream, "221 %s BYE\r\n", myhostname);
			lmtp_reset(session);
			return 0;	/* return 0 to cause the connection to close */
		}
	case LMTP_NOOP:
		{
			ci_write((FILE *) stream, "250 OK\r\n");
			return 1;
		}
	case LMTP_RSET:
		{
			ci_write((FILE *) stream, "250 OK\r\n");
			lmtp_reset(session);
			return 1;
		}
	case LMTP_LHLO:
		{
			/* Reply wth our hostname and a list of features.
			 * The RFC requires a couple of SMTP extensions
			 * with a MUST statement, so just hardcode them.
			 * */
			ci_write((FILE *) stream,
				 "250-%s\r\n"
				 "250-PIPELINING\r\n"
				 "250-ENHANCEDSTATUSCODES\r\n"
				 /* This is a SHOULD implement:
				  * "250-8BITMIME\r\n"
				  * Might as well do these, too:
				  * "250-CHUNKING\r\n"
				  * "250-BINARYMIME\r\n"
				  * */
				 "250 SIZE\r\n", myhostname);
			lmtp_reset(session);
			return 1;
		}
	case LMTP_HELP:
		{
			int helpcmd;

			if (value == NULL)
				helpcmd = LMTP_END;
			else
				for (helpcmd = LMTP_STRT;
				     helpcmd < LMTP_END; helpcmd++)
					if (strcasecmp
					    (value,
					     commands[helpcmd]) == 0)
						break;

			TRACE(TRACE_DEBUG, "LMTP_HELP requested for commandtype %d", helpcmd);

			if ((helpcmd == LMTP_LHLO)
			    || (helpcmd == LMTP_DATA)
			    || (helpcmd == LMTP_RSET)
			    || (helpcmd == LMTP_QUIT)
			    || (helpcmd == LMTP_NOOP)
			    || (helpcmd == LMTP_HELP)) {
				ci_write((FILE *) stream, "%s",
					 LMTP_HELP_TEXT[helpcmd]);
			} else {
				ci_write((FILE *) stream, "%s",
					LMTP_HELP_TEXT[LMTP_END]);
			}

			return 1;
		}
	case LMTP_VRFY:
		{
			/* RFC 2821 says this SHOULD be implemented...
			 * and the goal is to say if the given address
			 * is a valid delivery address at this server. */
			ci_write((FILE *) stream, "502 Command not implemented\r\n");
			return 1;
		}
	case LMTP_EXPN:
		{
			/* RFC 2821 says this SHOULD be implemented...
			 * and the goal is to return the membership
			 * of the specified mailing list. */
			ci_write((FILE *) stream, "502 Command not implemented\r\n");
			return 1;
		}
	case LMTP_MAIL:
		{
			/* We need to LHLO first because the client
			 * needs to know what extensions we support.
			 * */
			if (session->state != LHLO) {
				ci_write((FILE *) stream, "550 Command out of sequence.\r\n");
			} else if (dm_list_length(&from) > 0) {
				ci_write((FILE *) stream,
					"500 Sender already received. Use RSET to clear.\r\n");
				TRACE(TRACE_ERROR, "Sender already received: %s",
				      (char *)(dm_list_getstart(&from)->data));
			} else {
				/* First look for an email address.
				 * Don't bother verifying or whatever,
				 * just find something between angle brackets!
				 * */
				int goodtogo = 1;
				size_t tmplen = 0, tmppos = 0;
				char *tmpaddr = NULL, *tmpbody = NULL;

				find_bounded(value, '<', '>', &tmpaddr,
					     &tmplen, &tmppos);

				/* Second look for a BODY keyword.
				 * See if it has an argument, and if we
				 * support that feature. Don't give an OK
				 * if we can't handle it yet, like 8BIT!
				 * */

				/* Find the '=' following the address
				 * then advance one character past it
				 * (but only if there's more string!)
				 * */
				tmpbody = strstr(value + tmppos, "=");
				if (tmpbody != NULL)
					if (strlen(tmpbody))
						tmpbody++;

				/* This is all a bit nested now... */
				if (tmplen < 1 && tmpaddr == NULL) {
					ci_write((FILE *) stream, "500 No address found.\r\n");
					goodtogo = 0;
				} else if (tmpbody != NULL) {
					/* See RFC 3030 for the best
					 * description of this stuff.
					 * */
					if (strlen(tmpbody) < 4) {
						/* Caught */
					} else if (0 == strcasecmp(tmpbody, "7BIT")) {
						/* Sure fine go ahead. */
						goodtogo = 1;	// Not that it wasn't 1 already ;-)
					}
					/* 8BITMIME corresponds to RFC 1652,
					 * BINARYMIME corresponds to RFC 3030.
					 * */
					else if (strlen(tmpbody) < 8) {
						/* Caught */
					} else if (0 == strcasecmp(tmpbody, "8BITMIME"))
					{
						/* We can't do this yet. */
						/* session->state = BIT8;
						 * */
						ci_write((FILE *) stream,
							"500 Please use 7BIT MIME only.\r\n");
						goodtogo = 0;
					} else if (strlen(tmpbody) < 10) {
						/* Caught */
					} else if (0 ==
						   strcasecmp(tmpbody,
							      "BINARYMIME"))
					{
						/* We can't do this yet. */
						/* session->state = BDAT;
						 * */
						ci_write((FILE *) stream,
							"500 Please use 7BIT MIME only.\r\n");
						goodtogo = 0;
					}
				}

				if (goodtogo) {
					/* Sure fine go ahead. */
					dm_list_nodeadd(&from, tmpaddr, strlen(tmpaddr)+1);
					ci_write((FILE *) stream,
						"250 Sender <%s> OK\r\n",
						(char *)(dm_list_getstart(&from)->data));
					child_reg_connected_user(tmpaddr);
				}
				if (tmpaddr != NULL)
					g_free(tmpaddr);
			}
			return 1;
		}
	case LMTP_RCPT:
		{
			if (session->state != LHLO) {
				ci_write((FILE *) stream,
					"550 Command out of sequence.\r\n");
			} else {
				size_t tmplen = 0, tmppos = 0;
				char *tmpaddr = NULL;

				find_bounded(value, '<', '>', &tmpaddr,
					     &tmplen, &tmppos);

				if (tmplen < 1) {
					ci_write((FILE *) stream,
						"500 No address found.\r\n");
				} else {
					/* Note that this is not a pointer, but really is on the stack!
					 * Because dm_list_nodeadd() memcpy's the structure, we don't need
					 * it to live any longer than the duration of this stack frame. */
					deliver_to_user_t dsnuser;

					dsnuser_init(&dsnuser);

					/* find_bounded() allocated tmpaddr for us, and that's ok
					 * since dsnuser_free() will free it for us later on. */
					dsnuser.address = tmpaddr;

					if (dsnuser_resolve(&dsnuser) != 0) {
						TRACE(TRACE_ERROR, "dsnuser_resolve_list failed");
						ci_write((FILE *) stream, "430 Temporary failure in recipient lookup\r\n");
						dsnuser_free(&dsnuser);
						return 1;
					}

					/* Class 2 means the address was deliverable in some way. */
					switch (dsnuser.dsn.class) {
					case DSN_CLASS_OK:
						ci_write((FILE *) stream, "250 Recipient <%s> OK\r\n",
							dsnuser.address);
						/* A successfully found recipient goes onto the list.
						 * The struct will be free'd from lmtp_reset(). */
						dm_list_nodeadd(&rcpt, &dsnuser, sizeof(deliver_to_user_t));
						break;
					default:
						ci_write((FILE *) stream, "550 Recipient <%s> FAIL\r\n",
							dsnuser.address);
						/* If the user wasn't added, free the non-entry. */
						dsnuser_free(&dsnuser);
						break;
					}
				}
			}
			return 1;
		}
		/* Here's where it gets really exciting! */
	case LMTP_DATA:
		{
			// if (session->state != DATA || session->state != BIT8)
			if (session->state != LHLO) {
				ci_write((FILE *) stream,
					"550 Command out of sequence\r\n");
			} else if (dm_list_length(&rcpt) < 1) {
				ci_write((FILE *) stream,
					"503 No valid recipients\r\n");
			} else {
				if (dm_list_length(&rcpt) > 0 && dm_list_length(&from) > 0) {
					TRACE(TRACE_DEBUG, "requesting sender to begin message.");
					ci_write((FILE *) stream,
						"354 Start mail input; end with <CRLF>.<CRLF>\r\n");
				} else {
					if (dm_list_length(&rcpt) < 1) {
						TRACE(TRACE_DEBUG, "no valid recipients found, cancel message.");
						ci_write((FILE *) stream,
							"503 No valid recipients\r\n");
					}
					if (dm_list_length(&from) < 1) {
						TRACE(TRACE_DEBUG, "no sender provided, session cancelled.");
						ci_write((FILE *) stream,
							"554 No valid sender.\r\n");
					}
					return 1;
				}

				/* Anonymous Block */
				{
					struct element *element;
					struct DbmailMessage *msg;
					char *s;

					if (! (msg = dbmail_message_new_from_stream((FILE *)instream, DBMAIL_STREAM_LMTP))) {
						TRACE(TRACE_ERROR, "dbmail_message_new_from_stream() failed");
						discard_client_input((FILE *) instream);
						ci_write((FILE *) stream, "500 Error reading message");
						return 1;
					}
					
					s = dbmail_message_to_string(msg);
					TRACE(TRACE_DEBUG, "whole message = %s", s);
					g_free(s);

					if (dbmail_message_get_hdrs_size(msg, FALSE) > READ_BLOCK_SIZE) {
						TRACE(TRACE_ERROR, "header is too big");
						discard_client_input((FILE *) instream);
						ci_write((FILE *)stream, "500 Error reading header, "
							"header too big.\r\n");
						return 1;
					}

					dbmail_message_set_header(msg, "Return-Path", from.start->data);
					if (insert_messages(msg, &rcpt) == -1) {
						ci_write((FILE *) stream, "503 Message not received\r\n");
					} else {
						/* The DATA command itself it not given a reply except
						 * that of the status of each of the remaining recipients. */
						const char *class, *subject, *detail;

						/* The replies MUST be in the order received */
						rcpt.start =
						    dm_list_reverse(rcpt.start);

						for (element = dm_list_getstart(&rcpt);
						     element != NULL;
						     element = element->nextnode) {
							deliver_to_user_t * dsnuser =
							    (deliver_to_user_t *) element->data;
							dsn_tostring(dsnuser->dsn, &class, &subject, &detail);

							/* Give a simple OK, otherwise a detailed message. */
							switch (dsnuser->dsn.class) {
								case DSN_CLASS_OK:
									ci_write((FILE *)stream, "%d%d%d Recipient <%s> OK\r\n",
									        dsnuser->dsn.class, dsnuser->dsn.subject, dsnuser->dsn.detail,
									        dsnuser->address);
									break;
								default:
									ci_write((FILE *)stream, "%d%d%d Recipient <%s> %s %s %s\r\n",
									        dsnuser->dsn.class, dsnuser->dsn.subject, dsnuser->dsn.detail,
									        dsnuser->address, class, subject, detail);
							}
						}
					}
					dbmail_message_free(msg);
					
				}
				/* Reset the session after a successful delivery;
				 * MTA's like Exim prefer to immediately begin the
				 * next delivery without an RSET or a reconnect. */
				lmtp_reset(session);
			}
			return 1;
		}
	default:
		{
			return lmtp_error(session, stream,
					  "500 What are you trying to say here?\r\n");
		}
	}
	return 1;
}

