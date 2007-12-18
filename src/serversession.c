/*
 Copyright (C) 2004 IC & S dbmail@ic-s.nl
 Copyright (C) 2007 Aaron Stone aaron@serendipity.cx

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

/* Common functions for simple protocol daemons. */

#include "dbmail.h"
#define THIS_MODULE "serversession"

#define INCOMING_BUFFER_SIZE 512

/* max_errors defines the maximum number of allowed failures */
#define MAX_ERRORS 3

extern volatile sig_atomic_t alarm_occured;

/**
 * initialize a new session. Sets all relevant variables in session
 * \param[in,out] session to initialize
 */
void session_init(ServiceInfo_t *service, SessionInfo_t *session) 
{
	/* common variables */
	session->username = NULL;
	session->password = NULL;
	session->result = 0;
	session->error_count = 0;

	/* Additional initialization */
	service->init(service, session);
}

void session_reset(ServiceInfo_t *service, SessionInfo_t *session)
{
	service->reset(service, session);
}

void session_free(ServiceInfo_t *service, SessionInfo_t *session)
{
	service->free(service, session);
}

/* Handles connection and calls the service command handler */
int session_handle_connection(ServiceInfo_t *service, clientinfo_t *ci)
{
	int done = 1;		/* loop state */
	char *buffer = NULL;	/* connection buffer */
	int cnt;		/* counter */

	SessionInfo_t *session = g_new0(SessionInfo_t, 1);

	session->ci = ci;

	session_init(service, session);

	buffer = g_new0(char, INCOMING_BUFFER_SIZE);

	if (!buffer) {
		TRACE(TRACE_MESSAGE, "Could not allocate buffer");
		return 0;
	}

	if (ci->tx) {
		/* send greeting */
		ci_write(ci->tx, "%s\r\n", session->hello_message);
		fflush(ci->tx);
	} else {
		TRACE(TRACE_MESSAGE, "TX stream is null!");
		g_free(buffer);
		return 0;
	}

	session_reset(service, session);
	while (done > 0) {

		if (db_check_connection()) {
			TRACE(TRACE_DEBUG,"database has gone away");
			done=-1;
			break;
		}

		/* set the timeout counter */
		alarm(service->config->timeout);

		/* clear the buffer */
		memset(buffer, 0, INCOMING_BUFFER_SIZE);

		for (cnt = 0; cnt < INCOMING_BUFFER_SIZE - 1; cnt++) {
			do {
				clearerr(ci->rx);
				fread(&buffer[cnt], 1, 1, ci->rx);

				/* leave, an alarm has occured during fread */
				if (alarm_occured) {
					alarm_occured = 0;
					fprintf(ci->tx, "%s\r\n", session->timeout_message);
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
			/* handle commands */
			done = service->command(service, session, buffer);
		}
		fflush(ci->tx);
	}

	/* memory cleanup */
	session_free(service, session);
	g_free(buffer);
	buffer = NULL;

	/* reset timers */
	alarm(0);

	return 0;
}

int session_error(ServiceInfo_t *service, SessionInfo_t * session,
	       const char *formatstring, ...)
{
	clientinfo_t *ci = session->ci;
	va_list argp;

	if (session->error_count >= MAX_ERRORS) {
		TRACE(TRACE_MESSAGE, "too many errors (MAX_ERRORS is %d)", MAX_ERRORS);
		ci_write(ci->tx, "%s\r\n", session->too_many_errors_message);
		session->result = 2;	/* possible flood */
		session_reset(service, session);
		return -3;
	} else {
		va_start(argp, formatstring);
		if (vfprintf(ci->tx, formatstring, argp) < 0) {
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


