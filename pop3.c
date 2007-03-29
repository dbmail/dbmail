/*
  Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
  Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl

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
 * implementation for pop3 commands according to RFC 1081 */


#include "dbmail.h"
#define THIS_MODULE "pop3"

#define INCOMING_BUFFER_SIZE 512
#define APOP_STAMP_SIZE 255
#define MAX_USERID_SIZE 100

/* default timeout for server daemon */
#define DEFAULT_SERVER_TIMEOUT 300

/* max_errors defines the maximum number of allowed failures */
#define MAX_ERRORS 3

/* max_in_buffer defines the maximum number of bytes that are allowed to be
 * in the incoming buffer */
#define MAX_IN_BUFFER 255

extern int pop_before_smtp;
extern volatile sig_atomic_t alarm_occured;

int pop3_error(PopSession_t * session, void *stream,
	       const char *formatstring, ...) PRINTF_ARGS(3, 4);

/* allowed pop3 commands */
const char *commands[] = {
	"quit", /**< POP3_QUIT */
	"user", /**< POP3_USER */
	"pass", /**< POP3_PASS */
	"stat", /**< POP3_STAT */
	"list", /**< POP3_LIST */
	"retr", /**< POP3_RETR */
	"dele", /**< POP3_DELE */
	"noop", /**< POP3_NOOP */
	"last", /**< POP3_LAST */
	"rset", /**< POP3_RSET */
	"uidl", /**< POP3_UIDL */
	"apop", /**< POP3_APOP */
	"auth", /**< POP3_AUTH */
	"top", /**< POP3_TOP */
	"capa" /**< POP3_CAPA */
};

int pop3_handle_connection(clientinfo_t * ci)
{
	/*
	   Handles connection and calls
	   pop command handler
	 */

	int done = 1;		/* loop state */
	char *buffer = NULL;	/* connection buffer */
	char myhostname[64];
	int cnt;		/* counter */
	char unique_id[UID_SIZE];
	PopSession_t session;	/* current connection session */

	/* setting Session variables */
	memset(&session,0,sizeof(session));
	
	/* clear the message list */
	dm_list_init(&session.messagelst);

	/* getting hostname */
	gethostname(myhostname, 64);
	myhostname[63] = '\0';	/* make sure string is terminated */
	
	
	/* create an unique timestamp + processid for APOP authentication */
	session.apop_stamp = g_new0(char,APOP_STAMP_SIZE);
	create_unique_id(unique_id, 0);
	snprintf(session.apop_stamp, APOP_STAMP_SIZE, "<%s@%s>", unique_id, myhostname);

	if (ci->tx) {
		/* sending greeting */
		ci_write(ci->tx, "+OK DBMAIL pop3 server ready to rock %s\r\n",
			session.apop_stamp);
		fflush(ci->tx);
	} else {
		TRACE(TRACE_MESSAGE, "TX stream is null!");
		g_free(session.apop_stamp);
		return 0;
	}

	/* set authorization state */
	session.state = POP3_AUTHORIZATION_STATE;

	/* setup the read buffer */
	buffer = g_new0(char,INCOMING_BUFFER_SIZE);

	/* lets start handling commands */
	while (done > 0) {

		if (db_check_connection())
			break;

		/* set the timeout counter */
		alarm(ci->timeout);

		/* clear the buffer */
		memset(buffer, 0, INCOMING_BUFFER_SIZE);

		for (cnt = 0; cnt < INCOMING_BUFFER_SIZE - 1; cnt++) {
			do {
				clearerr(ci->rx);
				
				fread(&buffer[cnt], 1, 1, ci->rx);
				
				if (alarm_occured) {
					alarm_occured = 0;
					client_close();
					done = -1;
					break;
				}

			} while (ferror(ci->rx) && errno == EINTR);

			if (buffer[cnt] == '\n' || feof(ci->rx) || ferror(ci->rx)) {
				buffer[cnt + 1] = '\0';
				break;
			}
		}

		if (feof(ci->rx) || ferror(ci->rx))
			done = -1;
		else {
			/* reset function handle timeout */
			alarm(0);	
			
			/* handle pop3 commands */
			done = pop3(ci, buffer, &session);	
		}
		fflush(ci->tx);
	}
	g_free(buffer);
	buffer = NULL;

	/* We're done with this client. The rest is cleanup */
	
	session.state = POP3_UPDATE_STATE;

	/* memory cleanup */
	g_free(session.apop_stamp);
	session.apop_stamp = NULL;

	if (session.username != NULL && (session.was_apop || session.password != NULL)) {
		switch (session.SessionResult) {
		case 0:
			TRACE(TRACE_MESSAGE, "user %s logging out [messages=%llu, octets=%llu]", 
					session.username, 
					session.virtual_totalmessages, 
					session.virtual_totalsize);

			/* if everything went well, write down everything and do a cleanup */
			if (db_update_pop(&session) == DM_SUCCESS)
				ci_write(ci->tx, "+OK see ya later\r\n");
			else
				ci_write(ci->tx, "-ERR some deleted messages not removed\r\n");

			fflush(ci->tx);
			break;

		case 1:
			TRACE(TRACE_ERROR, "EOF from client, connection terminated");
			break;

		case 2:
			TRACE(TRACE_ERROR, "alert! possible flood attempt, closing connection");
			break;

		case 3:
			TRACE(TRACE_ERROR, "authorization layer failure");
			break;
		case 4:
			TRACE(TRACE_ERROR, "storage layer failure");
			break;
		}
	} else if (done==0) { // QUIT issued before AUTH phase completed
		ci_write(ci->tx, "+OK see ya later\r\n");
		fflush(ci->tx);
	} else {
		TRACE(TRACE_ERROR, "error, incomplete session");
	}
	
	/* remove session info like messagelist etc. */
	db_session_cleanup(&session);

	/* username cleanup */
	if (session.username != NULL) {
		g_free(session.username);
		session.username = NULL;
	}

	/* password cleanup */
	if (session.password != NULL) {
		g_free(session.password);
		session.password = NULL;
	}

	/* reset timers */
	alarm(0);

	return 0;
}

int pop3_error(PopSession_t * session, void *stream,
	       const char *formatstring, ...)
{
	va_list argp;

	if (session->error_count >= MAX_ERRORS) {
		TRACE(TRACE_MESSAGE, "too many errors (MAX_ERRORS is %d)", 
				MAX_ERRORS);
		ci_write((FILE *) stream, "-ERR loser, go play somewhere else\r\n");
		session->SessionResult = 2;	/* possible flood */
		return -3;
	} else {
		va_start(argp, formatstring);
		vfprintf((FILE *) stream, formatstring, argp);
		va_end(argp);
	}

	TRACE(TRACE_DEBUG, "an invalid command was issued");
	session->error_count++;
	return 1;
}

int pop3(clientinfo_t *ci, char *buffer, PopSession_t * session)
{
	/* returns a 0  on a quit
	 *           -1  on a failure
	 *            1  on a success 
	 */
	char *command, *value;
	Pop3Cmd_t cmdtype;
	int found = 0;
	int indx = 0;
	u64_t result;
	int validate_result;
	u64_t top_lines, top_messageid;
	struct element *tmpelement;
	unsigned char *md5_apop_he;
	char *searchptr;
	u64_t user_idnr;
	struct message *msg;

	char *client_ip = ci->ip_src;
	FILE *stream = ci->tx;

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

	TRACE(TRACE_DEBUG, "incoming buffer: [%s]", 
			buffer);

	command = buffer;

	value = strstr(command, " ");	/* look for the separator */

	if (value != NULL) {
		*value = '\0';	/* set a \0 on the command end */
		value++;	/* skip space */

		if (strlen(value) == 0)
			value = NULL;	/* no value specified */
		else {
			TRACE(TRACE_DEBUG, "command issued :cmd [%s], value [%s]\n",
					command, value);
		}
	}

	/* find command that was issued */
	for (cmdtype = POP3_QUIT; cmdtype <= POP3_CAPA; cmdtype++)
		if (strcasecmp(command, commands[cmdtype]) == 0) {
			session->was_apop = 1;
			break;
		}

	TRACE(TRACE_DEBUG, "command looked up as commandtype %d", 
			cmdtype);

	/* commands that are allowed to have no arguments */
	if (value == NULL) {
		switch (cmdtype) {
			case POP3_QUIT:
			case POP3_LIST:
			case POP3_STAT:
			case POP3_RSET:
			case POP3_NOOP:
			case POP3_LAST:
			case POP3_UIDL:
			case POP3_AUTH:
			case POP3_CAPA:
				break;
			default:
				return pop3_error(session, stream,
				  "-ERR your command does not compute\r\n");
			break;
		}
	}

	switch (cmdtype) {
		
	case POP3_QUIT:
		/* We return 0 here, and then pop3_handle_connection cleans up
		 * the connection, commits all changes, and sends the final
		 * "OK" message indicating that QUIT has completed. */
		return 0;
		
	case POP3_USER:
		if (session->state != POP3_AUTHORIZATION_STATE)
			return pop3_error(session, stream, "-ERR wrong command mode\r\n");

		if (session->username != NULL) {
			/* reset username */
			g_free(session->username);
			session->username = NULL;
		}

		if (session->username == NULL) {
			if (strlen(value) > MAX_USERID_SIZE)
				return pop3_error(session, stream, "-ERR userid is too long\r\n");

			/* create memspace for username */
			session->username = g_new0(char,strlen(value) + 1);
			strncpy(session->username, value, strlen(value) + 1);
		}

		ci_write((FILE *) stream, "+OK Password required for %s\r\n", session->username);
		return 1;

	case POP3_PASS:
		if (session->state != POP3_AUTHORIZATION_STATE)
			return pop3_error(session, stream, "-ERR wrong command mode\r\n");

		if (session->password != NULL) {
			g_free(session->password);
			session->password = NULL;
		}

		if (session->password == NULL) {
			/* create memspace for password */
			session->password = g_new0(char,strlen(value) + 1);
			strncpy(session->password, value, strlen(value) + 1);
		}

		/* check in authorization layer if these credentials are correct */
		validate_result = auth_validate(ci, session->username, session->password, &result);

		switch (validate_result) {
		case -1:
			session->SessionResult = 3;
			return -1;
		case 0:
			TRACE(TRACE_ERROR, "user [%s] coming from [%s] tried to login with wrong password", 
				session->username, ci->ip_src);

			g_free(session->username);
			session->username = NULL;

			g_free(session->password);
			session->password = NULL;

			return pop3_error(session, stream, "-ERR username/password incorrect\r\n");

		default:
			/* user logged in OK */
			session->state = POP3_TRANSACTION_STATE;

			/* now we're going to build up a session for this user */
			TRACE(TRACE_DEBUG, "validation OK, building a session for user [%s]",
					session->username);

			/* if pop_before_smtp is active, log this ip */
			if (pop_before_smtp)
				db_log_ip(client_ip);

			result = db_createsession(result, session);
			if (result == 1) {
				ci_write((FILE *) stream, "+OK %s has %llu messages (%llu octets)\r\n", 
						session->username, 
						session->virtual_totalmessages, 
						session->virtual_totalsize);
				TRACE(TRACE_MESSAGE, "user %s logged in [messages=%llu, octets=%llu]", 
						session->username, 
						session->virtual_totalmessages, 
						session->virtual_totalsize);
			} else
				session->SessionResult = 4;	/* Database error. */

			return result;
		}
		return 1;

	case POP3_LIST:
		if (session->state != POP3_TRANSACTION_STATE)
			return pop3_error(session, stream, "-ERR wrong command mode\r\n");

		tmpelement = dm_list_getstart(&session->messagelst);
		if (value != NULL) {
			/* they're asking for a specific message */
			while (tmpelement != NULL) {
				msg = (struct message *)tmpelement->data;
				if (msg->messageid == strtoull(value,NULL, 10) && msg->virtual_messagestatus < MESSAGE_STATUS_DELETE) {
					ci_write((FILE *) stream, "+OK %llu %llu\r\n", msg->messageid,msg->msize);
					found = 1;
				}
				tmpelement = tmpelement->nextnode;
			}
			if (!found)
				return pop3_error(session, stream, "-ERR [%s] no such message\r\n", value);
			else
				return 1;
		}

		/* just drop the list */
		ci_write((FILE *) stream, "+OK %llu messages (%llu octets)\r\n", 
				session->virtual_totalmessages, 
				session->virtual_totalsize);

		if (session->virtual_totalmessages > 0) {
			/* traversing list */
			while (tmpelement != NULL) {
				msg = (struct message *)tmpelement->data;
				if (msg->virtual_messagestatus < MESSAGE_STATUS_DELETE)
					ci_write((FILE *) stream, "%llu %llu\r\n", msg->messageid,msg->msize);
				tmpelement = tmpelement->nextnode;
			}
		}
		ci_write((FILE *) stream, ".\r\n");
		return 1;

	case POP3_STAT:
		if (session->state != POP3_TRANSACTION_STATE)
			return pop3_error(session, stream, "-ERR wrong command mode\r\n");

		ci_write((FILE *) stream, "+OK %llu %llu\r\n", 
				session->virtual_totalmessages, 
				session->virtual_totalsize);

		return 1;

	case POP3_RETR:
		TRACE(TRACE_DEBUG, "RETR command, retrieving message");

		if (session->state != POP3_TRANSACTION_STATE)
			return pop3_error(session, stream, "-ERR wrong command mode\r\n");

		tmpelement = dm_list_getstart(&(session->messagelst));

		/* selecting a message */
		TRACE(TRACE_DEBUG, "RETR command, selecting message");
		
		while (tmpelement != NULL) {
			msg = (struct message *) tmpelement->data;
			if (msg->messageid == strtoull(value, NULL, 10) && msg->virtual_messagestatus < MESSAGE_STATUS_DELETE) {	/* message is not deleted */
				msg->virtual_messagestatus = MESSAGE_STATUS_SEEN;
				ci_write((FILE *) stream, "+OK %llu octets\r\n", msg->msize);
				return db_send_message_lines((void *) stream, msg->realmessageid, -2, 0);
			}
			tmpelement = tmpelement->nextnode;
		}
		return pop3_error(session, stream, "-ERR [%s] no such message\r\n", value);

	case POP3_DELE:
		if (session->state != POP3_TRANSACTION_STATE)
			return pop3_error(session, stream, "-ERR wrong command mode\r\n");

		tmpelement = dm_list_getstart(&(session->messagelst));

		/* selecting a message */
		while (tmpelement != NULL) {
			msg = (struct message *) tmpelement->data;
			if (msg->messageid == strtoull(value, NULL, 10) && msg->virtual_messagestatus < MESSAGE_STATUS_DELETE) {	/* message is not deleted */
				msg->virtual_messagestatus = MESSAGE_STATUS_DELETE;
				session->virtual_totalsize -= msg->msize;
				session->virtual_totalmessages -= 1;

				ci_write((FILE *) stream, "+OK message %llu deleted\r\n", msg->messageid);
				return 1;
			}
			tmpelement = tmpelement->nextnode;
		}
		return pop3_error(session, stream, "-ERR [%s] no such message\r\n", value);

	case POP3_RSET:
		if (session->state != POP3_TRANSACTION_STATE)
			return pop3_error(session, stream, "-ERR wrong command mode\r\n");

		tmpelement = dm_list_getstart(&(session->messagelst));

		session->virtual_totalsize = session->totalsize;
		session->virtual_totalmessages = session->totalmessages;

		while (tmpelement != NULL) {
			msg = (struct message *) tmpelement->data;
			msg->virtual_messagestatus = msg->messagestatus;
			tmpelement = tmpelement->nextnode;
		}

		ci_write((FILE *) stream, "+OK %llu messages (%llu octets)\r\n", session->virtual_totalmessages, session->virtual_totalsize);

		return 1;

	case POP3_LAST:
		if (session->state != POP3_TRANSACTION_STATE)
			return pop3_error(session, stream, "-ERR wrong command mode\r\n");

		tmpelement = dm_list_getstart(&(session->messagelst));

		while (tmpelement != NULL) {
			msg = (struct message *) tmpelement->data;
			if (msg->virtual_messagestatus == MESSAGE_STATUS_NEW) {
				/* we need the last message that has been accessed */
				ci_write((FILE *) stream, "+OK %llu\r\n", msg->messageid - 1);
				return 1;
			}
			tmpelement = tmpelement->nextnode;
		}

		/* all old messages */
		ci_write((FILE *) stream, "+OK %llu\r\n", session->virtual_totalmessages);

		return 1;

	case POP3_NOOP:
		if (session->state != POP3_TRANSACTION_STATE)
			return pop3_error(session, stream, "-ERR wrong command mode\r\n");

		ci_write((FILE *) stream, "+OK\r\n");
		return 1;

	case POP3_UIDL:
		if (session->state != POP3_TRANSACTION_STATE)
			return pop3_error(session, stream, "-ERR wrong command mode\r\n");

		tmpelement = dm_list_getstart(&(session->messagelst));

		if (value != NULL) {
			/* they're asking for a specific message */
			while (tmpelement != NULL) {
				msg = (struct message *)tmpelement->data;
				if (msg->messageid == strtoull(value,NULL, 10) && msg->virtual_messagestatus < MESSAGE_STATUS_DELETE) {
					ci_write((FILE *) stream, "+OK %llu %s\r\n", msg->messageid,msg->uidl);
					found = 1;
				}
				tmpelement = tmpelement->nextnode;
			}
			if (!found)
				return pop3_error(session, stream, "-ERR [%s] no such message\r\n", value);
			else
				return 1;
		}

		/* just drop the list */
		ci_write((FILE *) stream, "+OK Some very unique numbers for you\r\n");

		if (session->virtual_totalmessages > 0) {
			/* traversing list */
			while (tmpelement != NULL) {
				msg = (struct message *)tmpelement->data; 
				if (msg->virtual_messagestatus < MESSAGE_STATUS_DELETE)
					ci_write((FILE *) stream, "%llu %s\r\n", msg->messageid, msg->uidl);

				tmpelement = tmpelement->nextnode;
			}
		}

		ci_write((FILE *) stream, ".\r\n");

		return 1;

	case POP3_APOP:
		if (session->state != POP3_AUTHORIZATION_STATE)
			return pop3_error(session, stream, "-ERR wrong command mode\r\n");

		/* find out where the md5 hash starts */
		searchptr = strstr(value, " ");

		if (searchptr == NULL) 
			return pop3_error(session, stream, "-ERR your command does not compute\r\n");

		/* skip the space */
		searchptr = searchptr + 1;

		/* value should now be the username */
		value[searchptr - value - 1] = '\0';

		if (strlen(searchptr) != 32)
			return pop3_error(session, stream, "-ERR not a valid md5 hash\r\n");

		if (strlen(value) > MAX_USERID_SIZE)
			return pop3_error(session, stream, "-ERR userid is too long\r\n");

		md5_apop_he = g_new0(unsigned char,strlen(searchptr) + 1);
		session->username = g_new0(char,strlen(value) + 1);

		strncpy((char *)md5_apop_he, searchptr, strlen(searchptr) + 1);
		strncpy(session->username, value, strlen(value) + 1);

		/*
		 * check the encryption used for this user
		 * note that if the user does not exist it is not noted
		 * by db_getencryption()
		 */
		if (auth_user_exists(session->username, &user_idnr) == -1) {
			TRACE(TRACE_ERROR, "error finding if user exists. username = [%s]", 
					session->username);
			return -1;
		}
		if (strcasecmp(auth_getencryption(user_idnr), "") != 0) {
			/* it should be clear text */
			g_free(md5_apop_he);
			g_free(session->username);
			session->username = NULL;
			md5_apop_he = NULL;
			return pop3_error(session, stream, "-ERR APOP command is not supported for this user\r\n");
		}

		TRACE(TRACE_DEBUG, "APOP auth, username [%s], md5_hash [%s]", session->username, md5_apop_he);

		result = auth_md5_validate(ci,session->username, md5_apop_he, session->apop_stamp);

		g_free(md5_apop_he);
		md5_apop_he = 0;

		switch (result) {
		case -1:
			session->SessionResult = 3;
			return -1;
		case 0:
			TRACE(TRACE_ERROR, "user [%s] tried to login with wrong password", session->username);

			g_free(session->username);
			session->username = NULL;

			g_free(session->password);
			session->password = NULL;

			return pop3_error(session, stream, "-ERR authentication attempt is invalid\r\n");

		default:
			/* user logged in OK */
			session->state = POP3_TRANSACTION_STATE;

			/* user seems to be valid, let's build a session */
			TRACE(TRACE_DEBUG, "validation OK, building a session for user [%s]", 
					session->username);

			/* if pop_before_smtp is active, log this ip */
			if (pop_before_smtp)
				db_log_ip(client_ip);

			result = db_createsession(result, session);
			if (result == 1) {
				ci_write((FILE *) stream, "+OK %s has %llu messages (%llu octets)\r\n", 
						session->username, 
						session->virtual_totalmessages, 
						session->virtual_totalsize);
				TRACE(TRACE_MESSAGE, "user %s logged in [messages=%llu, octets=%llu]", 
						session->username, 
						session->virtual_totalmessages, 
						session->virtual_totalsize);
			} else
				session->SessionResult = 4;	/* Database error. */

			return result;
		}
		return 1;

	case POP3_AUTH:
		if (session->state != POP3_AUTHORIZATION_STATE)
			return pop3_error(session, stream, "-ERR wrong command mode\r\n");
		return pop3_error(session, stream, "-ERR no AUTH mechanisms supported\r\n");

	case POP3_TOP:
		if (session->state != POP3_TRANSACTION_STATE)
			return pop3_error(session, stream, "-ERR wrong command mode\r\n");

		/* find out how many lines they want */
		searchptr = strstr(value, " ");

		/* insufficient parameters */
		if (searchptr == NULL)
			return pop3_error(session, stream, "-ERR your command does not compute\r\n");

		/* skip the space */
		searchptr = searchptr + 1;

		/* value should now be the the message that needs to be retrieved */
		value[searchptr - value - 1] = '\0';

		/* check if searchptr or value are negative. If so return an
		   error. This is done by only letting the strings contain
		   digits (0-9) */
		if (strspn(searchptr, "0123456789") != strlen(searchptr))
			return pop3_error(session, stream, "-ERR wrong parameter\r\n");
		if (strspn(value, "0123456789") != strlen(value))
			return pop3_error(session, stream, "-ERR wrong parameter\r\n");

		top_lines = strtoull(searchptr, NULL, 10);
		top_messageid = strtoull(value, NULL, 10);
		if (top_messageid < 1)
			return pop3_error(session, stream, "-ERR wrong parameter\r\n");

		TRACE(TRACE_DEBUG, "TOP command (partially) retrieving message");

		tmpelement = dm_list_getstart(&(session->messagelst));

		/* selecting a message */
		TRACE(TRACE_DEBUG, "TOP command, selecting message");

		while (tmpelement != NULL) {
			msg = (struct message *) tmpelement->data;
			if (msg->messageid == top_messageid && msg->virtual_messagestatus < MESSAGE_STATUS_DELETE) {	/* message is not deleted */
				ci_write((FILE *) stream, "+OK %llu lines of message %llu\r\n", top_lines, top_messageid);
				return db_send_message_lines(stream, msg->realmessageid, top_lines, 0);
			}
			tmpelement = tmpelement->nextnode;
		}
		return pop3_error(session, stream, "-ERR no such message\r\n");

	case POP3_CAPA:
		ci_write((FILE *) stream, "+OK Capability list follows\r\nTOP\r\nUSER\r\nUIDL\r\n.\r\n");
		return 1;

	default:
		return pop3_error(session, stream, "-ERR command not understood\r\n");
	
	}
	return 1;
}
