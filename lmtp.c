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

/* $Id$
 *
 * implementation for lmtp commands according to RFC 1081 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmail.h"
#include "lmtp.h"
#include "pipe.h"
#include "header.h"
#include "db.h"
#include "dsn.h"
#include "debug.h"
#include "dbmailtypes.h"
#include "auth.h"
#include "clientinfo.h"
#include "lmtp.h"
#ifdef PROC_TITLES
#include "proctitleutils.h"
#endif

#define INCOMING_BUFFER_SIZE 512
#define MESSAGE_MAX_LINE_SIZE 1024

/* default timeout for server daemon */
#define DEFAULT_SERVER_TIMEOUT 300

/* max_errors defines the maximum number of allowed failures */
#define MAX_ERRORS 3

/* max_in_buffer defines the maximum number of bytes that are allowed to be
 * in the incoming buffer */
#define MAX_IN_BUFFER 255

/* These are needed across multiple calls to lmtp() */
static struct list rcpt;
static char *envelopefrom = NULL;

/* allowed lmtp commands */
static const char *const commands[] = {
	"LHLO", "QUIT", "RSET", "DATA", "MAIL",
	"VRFY", "EXPN", "HELP", "NOOP", "RCPT"
};

static const char validchars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "_.!@#$%^&*()-+=~[]{}<>:;\\/ ";

static char myhostname[64];

/**
 * read the whole message from a network connection
 * \param[in] instream input stream
 * \param[out] whole_message will hold the complete email-message
 * \param[out] whole_message_size will hold the size of the message
 * \return
 *     - -1 on error
 *     -  1 on success
 */
static int read_whole_message_network(FILE *instream, char **whole_message,
				      u64_t *whole_message_size);

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
static void init_session(PopSession_t *session);

int lmtp_reset(PopSession_t * session)
{
	/* Free the lists and reinitialize 
	 * but only if they were previously
	 * initialized by LMTP_LHLO... */
	if (session->state == LHLO) {
		list_freelist(&rcpt.start);
		list_init(&rcpt);
	}

	if (envelopefrom != NULL) {
		my_free(envelopefrom);
	}
	envelopefrom = NULL;

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
	
	init_session(&session);

	/* getting hostname */
	gethostname(myhostname, 64);
	myhostname[63] = 0;	/* make sure string is terminated */

	buffer = (char *) my_malloc(INCOMING_BUFFER_SIZE * sizeof(char));

	if (!buffer) {
		trace(TRACE_MESSAGE,
		      "lmtp_handle_connection(): Could not allocate buffer");
		return 0;
	}

	if (ci->tx) {
		/* sending greeting */
		ci_write(ci->tx,
			"220 %s DBMail LMTP service ready to rock\r\n",
			myhostname);
		fflush(ci->tx);
	} else {
		trace(TRACE_MESSAGE,
		      "lmtp_handle_connection(): TX stream is null!");
		my_free(buffer);
		return 0;
	}

	lmtp_reset(&session);
	while (done > 0) {
		/* set the timeout counter */
		alarm(ci->timeout);

		/* clear the buffer */
		memset(buffer, 0, INCOMING_BUFFER_SIZE);

		for (cnt = 0; cnt < INCOMING_BUFFER_SIZE - 1; cnt++) {
			do {
				clearerr(ci->rx);
				fread(&buffer[cnt], 1, 1, ci->rx);

				/* leave, an alarm has occured during fread */
				if (!ci->rx) {
					my_free(buffer);
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
			done =
			    lmtp(ci->tx, ci->rx, buffer, ci->ip, &session);
		}
		fflush(ci->tx);
	}

	/* memory cleanup */
	lmtp_reset(&session);
	my_free(buffer);
	buffer = NULL;

	/* reset timers */
	alarm(0);
	__debug_dumpallocs();

	return 0;
}

int lmtp_error(PopSession_t * session, void *stream,
	       const char *formatstring, ...)
{
	va_list argp;

	if (session->error_count >= MAX_ERRORS) {
		trace(TRACE_MESSAGE,
		      "lmtp_error(): too many errors (MAX_ERRORS is %d)",
		      MAX_ERRORS);
		ci_write((FILE *) stream,
			"500 Too many errors, closing connection.\r\n");
		session->SessionResult = 2;	/* possible flood */
		lmtp_reset(session);
		return -3;
	} else {
		va_start(argp, formatstring);
		if (vfprintf((FILE *) stream, formatstring, argp) < 0) {
			va_end(argp);
			trace(TRACE_ERROR, "%s,%s: error writing to stream",
			      __FILE__, __func__);
			return -1;
		}
		va_end(argp);
	}

	trace(TRACE_DEBUG, "lmtp_error(): an invalid command was issued");
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
		trace(TRACE_DEBUG, "lmtp(): buffer overflow attempt");
		return -3;
	}

	/* check for command issued */
	while (strchr(validchars, buffer[indx]))
		indx++;

	/* end buffer */
	buffer[indx] = '\0';

	trace(TRACE_DEBUG, "lmtp(): incoming buffer: [%s]", buffer);

	command = buffer;

	value = strstr(command, " ");	/* look for the separator */

	if (value != NULL) {
		*value = '\0';	/* set a \0 on the command end */
		value++;	/* skip space */

		if (strlen(value) == 0) {
			value = NULL;	/* no value specified */
		} else {
			trace(TRACE_DEBUG,
			      "lmtp(): command issued :cmd [%s], value [%s]\n",
			      command, value);
		}
	}

	for (cmdtype = LMTP_STRT; cmdtype < LMTP_END; cmdtype++)
		if (strcasecmp(command, commands[cmdtype]) == 0)
			break;

	trace(TRACE_DEBUG, "lmtp(): command looked up as commandtype %d",
	      cmdtype);

	/* commands that are allowed to have no arguments */
	if ((value == NULL) &&
	    !((cmdtype == LMTP_LHLO) || (cmdtype == LMTP_DATA) ||
	      (cmdtype == LMTP_RSET) || (cmdtype == LMTP_QUIT) ||
	      (cmdtype == LMTP_NOOP) || (cmdtype == LMTP_HELP)
	    )) {
		trace(TRACE_ERROR, "ARGUMENT %d", cmdtype);
		return lmtp_error(session, stream,
				  "500 This command requires an argument.\r\n");
	}

	switch (cmdtype) {
	case LMTP_QUIT:
		{
			ci_write((FILE *) stream, "221 %s BYE\r\n",
				 myhostname);
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
			/* Free the recipients list and reinitialize it */
			// list_freelist( &rcpt.start );
			list_init(&rcpt);
			
			session->state = LHLO;
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

			trace(TRACE_DEBUG,
			      "lmtp(): LMTP_HELP requested for commandtype %d",
			      helpcmd);

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
			ci_write((FILE *) stream,
				"502 Command not implemented\r\n");
			return 1;
		}
	case LMTP_EXPN:
		{
			/* RFC 2821 says this SHOULD be implemented...
			 * and the goal is to return the membership
			 * of the specified mailing list. */
			ci_write((FILE *) stream,
				"502 Command not implemented\r\n");
			return 1;
		}
	case LMTP_MAIL:
		{
			/* We need to LHLO first because the client
			 * needs to know what extensions we support.
			 * */
			if (session->state != LHLO) {
				ci_write((FILE *) stream,
					"550 Command out of sequence.\r\n");
			} else if (envelopefrom != NULL) {
				ci_write((FILE *) stream,
					"500 Sender already received. Use RSET to clear.\r\n");
				trace(TRACE_ERROR, "%s,%s: Sender already "
				      "received: last envfrom: %s",
				      __FILE__, __func__, envelopefrom);
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
					ci_write((FILE *) stream,
						"500 No address found.\r\n");
					goodtogo = 0;
				} else if (tmpbody != NULL) {
					/* See RFC 3030 for the best
					 * description of this stuff.
					 * */
					if (strlen(tmpbody) < 4) {
						/* Caught */
					} else if (0 ==
						   strcasecmp(tmpbody,
							      "7BIT")) {
						/* Sure fine go ahead. */
						goodtogo = 1;	// Not that it wasn't 1 already ;-)
					}
					/* 8BITMIME corresponds to RFC 1652,
					 * BINARYMIME corresponds to RFC 3030.
					 * */
					else if (strlen(tmpbody) < 8) {
						/* Caught */
					} else if (0 ==
						   strcasecmp(tmpbody,
							      "8BITMIME"))
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
					envelopefrom = tmpaddr;
					ci_write((FILE *) stream,
						"250 Sender <%s> OK\r\n",
						envelopefrom);
				} else {
					if (tmpaddr != NULL)
						my_free(tmpaddr);
				}
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
					 * Because list_nodeadd() memcpy's the structure, we don't need
					 * it to live any longer than the duration of this stack frame. */
					deliver_to_user_t dsnuser;

					dsnuser_init(&dsnuser);

					/* find_bounded() allocated tmpaddr for us, and that's ok
					 * since dsnuser_free() will free it for us later on. */
					dsnuser.address = tmpaddr;

					/* Queue up the potential recipients. We can use one call to
					 * dsnuser_resolve_list() to look them up. This should not
					 * be a problem because the client is required to pipeline
					 * the commands, i.e. not wait for a reply to each command. */
					list_nodeadd(&rcpt, &dsnuser,
						     sizeof
						     (deliver_to_user_t));
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
			} else if (list_totalnodes(&rcpt) < 1) {
				ci_write((FILE *) stream,
					"503 No valid recipients\r\n");
			} else {
				int has_recipients = 0;
				struct element *element;

				/* The replies MUST be in the order received */
				rcpt.start =
				    dbmail_list_reverse(rcpt.start);

				/* Resolve the addresses into deliverable / non-deliverable form. */
				if (dsnuser_resolve_list(&rcpt) == -1) {
					trace(TRACE_ERROR,
					      "main(): dsnuser_resolve_list failed");
					ci_write((FILE *) stream,
						"430 Temporary failure in recipient lookup\r\n");
					return 1;
				}
				element = list_getstart(&rcpt);
				while(element) {
					deliver_to_user_t *dsnuser =
					    (deliver_to_user_t *) element->
						data;
					element = element->nextnode;
					/* Class 2 means the address was deliverable in some way. */
					switch (dsnuser->dsn.class) {
					case DSN_CLASS_OK:
						ci_write((FILE *) stream,
							"250 Recipient <%s> OK\r\n",
							dsnuser->address);
						has_recipients = 1;
						break;
					default:
						ci_write((FILE *) stream,
							"550 Recipient <%s> FAIL\r\n",
							dsnuser->address);
						/* Remove the failed user from the potential recipients list
						 * so that we do not report a failed delivery later on. */
						list_nodedel(&rcpt,
							     dsnuser);
						break;
					}
				}

				/* Now we have a list of recipients! */
				/* Let the client know if they should continue... */

				if (has_recipients && envelopefrom != NULL) {
					trace(TRACE_DEBUG,
					      "main(): requesting sender to begin message.");
					ci_write((FILE *) stream,
						"354 Start mail input; end with <CRLF>.<CRLF>\r\n");
				} else {
					if (!has_recipients) {
						trace(TRACE_DEBUG,
						      "main(): no valid recipients found, cancel message.");
						ci_write((FILE *) stream,
							"503 No valid recipients\r\n");
					}
					if (!envelopefrom) {
						trace(TRACE_DEBUG,
						      "main(): envelopefrom is empty, cancel message.");
						ci_write((FILE *) stream,
							"554 No valid sender.\r\n");
					}
					return 1;
				}

				/* Anonymous Block */
				{
					char *whole_message = NULL;
					char *header = NULL;
					const char *body;
					u64_t whole_message_size;
					u64_t headersize = 0;
					u64_t headerrfcsize = 0;
					u64_t body_size = 0;
					u64_t body_rfcsize = 0;
					u64_t dummyidx = 0, dummysize = 0;
					struct list fromlist, headerfields;
					struct element *element;
					struct list mimelist;

					list_init(&mimelist);
					list_init(&fromlist);
					list_init(&headerfields);

					/* if (envelopefrom != NULL) */
					/* We know this to be true from the 354 code, above. */
					list_nodeadd(&fromlist,
						     envelopefrom,
						     strlen(envelopefrom));
					
					if (read_whole_message_network(
						    (FILE *) instream,
						    &whole_message,
						    &whole_message_size) < 0) {
						trace(TRACE_ERROR,
						      "%s,%s: read_whole_message_network() failed",
						      __FILE__, __func__);
						discard_client_input((FILE *) instream);
						ci_write((FILE *) stream,
							"500 Error reading message");
						return 1;
					}
					
					trace(TRACE_DEBUG, "%s,%s: whole message = %s", __FILE__, __func__, whole_message);
					if (whole_message == NULL) {
						trace(TRACE_ERROR, "%s,%s message is NULL!", __FILE__, __func__);
						discard_client_input(
							(FILE *) instream);
						ci_write((FILE *) stream,
							"500 Error reading header\r\n");
						return 1;
					}

					if (split_message(whole_message, 
							  whole_message_size - 1,
							  &header,
							  &headersize,
							  &headerrfcsize,
							  &body,
							  &body_size,
							  &body_rfcsize) < 0) {
						trace(TRACE_ERROR, "%s,%s: split_message() failed",
						      __FILE__, __func__);
						my_free(whole_message);
						discard_client_input((FILE *) instream);
						ci_write((FILE *) stream,
							"500 Error in message");
						return 1;
					}
					if ( headersize > READ_BLOCK_SIZE) {
						trace(TRACE_ERROR,
						      "main(): header is too "
						      "big");
						discard_client_input
							((FILE *)
							     instream);
						ci_write((FILE *)
							stream,
							"500 Error reading header, "
							"header too big.\r\n");
						return 1;
					}
					/* Parse the list and scan for field and content */
					if (mime_readheader
					    (header, &dummyidx, &mimelist,
					     &dummysize) < 0) {
						trace(TRACE_ERROR,
						      "main(): fatal error from mime_readheader()");
						discard_client_input((FILE
								      *)
								     instream);
						ci_write((FILE *) stream,
							"500 Error reading header.\r\n");
						return 1;
					}

					if (insert_messages(
						    header, body,
						    headersize, headerrfcsize,
						    body_size, body_rfcsize,
						    &headerfields, &rcpt,
						    &fromlist) == -1) {
						ci_write((FILE *) stream,
							"503 Message not received\r\n");
					} else {
						/* The DATA command itself it not given a reply except
						 * that of the status of each of the remaining recipients. */
						const char *class, *subject, *detail;

						for (element = list_getstart(&rcpt);
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
					if (header != NULL)
						my_free(header);
					if (whole_message != NULL)
						my_free(whole_message);
				}
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

int read_whole_message_network(FILE *instream, char **whole_message,
				      u64_t *whole_message_size)
{
	char *tmpmessage = NULL;
	char tmpline[MESSAGE_MAX_LINE_SIZE + 1];
	
	size_t line_size = 0;
	size_t total_size = 0;
	size_t current_pos = 0;
	int error = 0;

	memset(tmpline, '\0', MESSAGE_MAX_LINE_SIZE + 1);
	while (fgets(tmpline, MESSAGE_MAX_LINE_SIZE, instream) != NULL) {
		line_size = strlen(tmpline);
		
		/* It sometimes happens that we read a line of size 0,
		   which is odd.. For now, we just step over it. */
		if (line_size < 2)
			continue;

		/* check for '.\r\n' */
		if (line_size == 3 && strncmp(tmpline, ".\r\n", 3) == 0) 
			break;
		
		/* change the \r\n ending to \n */
		
		if (!(tmpmessage = realloc(tmpmessage, 
					   total_size + line_size - 1))) {
			error = 1;
			break;
		}
		
		if (!(memcpy((void *) &tmpmessage[current_pos], 
			     (void *) tmpline, line_size -2))) {
			error = 1;
			break;
		}
		total_size += line_size - 1;
		current_pos += line_size - 2;
		tmpmessage[current_pos++] = '\n';
				
		memset(tmpline, '\0', MESSAGE_MAX_LINE_SIZE + 1);
	}
		
	if (ferror(instream)) {
		trace(TRACE_ERROR, "%s,%s: error reading instream",
		      __FILE__, __func__);
		error = 1;
	}
	if (feof(instream)) {
		trace(TRACE_ERROR, "%s,%s: unexpected EOF in instream",
		      __FILE__, __func__);
		error = 1;
	}

	total_size += 1;
	if (!(tmpmessage = realloc(tmpmessage, total_size))) {
		trace(TRACE_ERROR, "%s.%s: realloc failed",
		      __FILE__, __func__);
		error = 1;
	} else
		tmpmessage[current_pos] = '\0';

	if (error) {
		trace(TRACE_ERROR, "%s,%s: error reading message from "
		      "instream", __FILE__, __func__);
		my_free(tmpmessage);
		return -1;
	}

	*whole_message = tmpmessage;
	*whole_message_size = total_size;
	return 1;
}

void init_session(PopSession_t *session) 
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
}
