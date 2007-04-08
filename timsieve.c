/* 

 Copyright (C) 1999-2004 Aaron Stone aaron at serendipity dot cx

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

/* implementation for tims commands according to RFC 1081 */

#include "dbmail.h"
#define THIS_MODULE "timsieved"

#define INCOMING_BUFFER_SIZE 512

/* default timeout for server daemon */
#define DEFAULT_SERVER_TIMEOUT 300

/* max_errors defines the maximum number of allowed failures */
#define MAX_ERRORS 3

/* max_in_buffer defines the maximum number of bytes that are allowed to be
 * in the incoming buffer */
#define MAX_IN_BUFFER 255

#define GREETING(stream) \
          ci_write(stream, "\"IMPLEMENTATION\" \"DBMail timsieved v%s\"\r\n", VERSION); \
          ci_write(stream, "\"SASL\" \"PLAIN\"\r\n"); \
          ci_write(stream, "\"SIEVE\" \"%s\"\r\n", sieve_extensions); \
          ci_write(stream, "OK\r\n")
	  /* Remember, no trailing semicolon! */
	  /* Sadly, some clients seem to be hardwired to look for 'timsieved',
	   * so that part of the Implementation line is absolutely required. */

/* allowed timsieve commands */
static const char *commands[] = {
	"LOGOUT", "STARTTLS", "CAPABILITY", "LISTSCRIPTS",
	"AUTHENTICATE", "DELETESCRIPT", "GETSCRIPT", "SETACTIVE",
	"HAVESPACE", "PUTSCRIPT"
};

static char myhostname[64];

/* Defined in timsieved.c */
extern const char *sieve_extensions;
extern volatile sig_atomic_t alarm_occured;

int tims_handle_connection(clientinfo_t * ci)
{
	/*
	   Handles connection and calls
	   tims command handler
	 */

	int done = 1;		/* loop state */
	char *buffer = NULL;	/* connection buffer */
	int cnt;		/* counter */

	PopSession_t session;	/* current connection session */

	memset(&session,0,sizeof(session));
	
	/* getting hostname */
	gethostname(myhostname, 64);
	myhostname[63] = 0;	/* make sure string is terminated */

	buffer = g_new0(char, INCOMING_BUFFER_SIZE);

	if (! ci->tx) {
		TRACE(TRACE_MESSAGE, "TX stream is null!");
		dm_free(buffer);
		return 0;
	}
	
	/* This is a macro shared with TIMS_CAPA, per the draft RFC. */
	GREETING(ci->tx);
	fflush(ci->tx);

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
				if (alarm_occured) {
					alarm_occured = 0;
					client_close();
					dm_free(buffer);
					return 0;
				}
			} while (ferror(ci->rx) && errno == EINTR);

			if (buffer[cnt] == '\n' || feof(ci->rx) || ferror(ci->rx)) {
				if (cnt > 0) {
					/* Ignore single newlines and \r\n pairs */
					if (cnt != 1 || buffer[cnt - 1] != '\r') {
						buffer[cnt + 1] = '\0';
						break;
					} else {
						/* Overwrite those silly extra \r\n's */
						/* Incremented to 0 at top of loop */
						cnt = -1;
					}
				}
			}
		}

		if (feof(ci->rx) || ferror(ci->rx)) {
			/* check client eof  */
			done = -1;
		} else {
			/* reset function handle timeout */
			alarm(0);
			/* handle tims commands */
			done = tims(ci, buffer, &session);
		}
		fflush(ci->tx);
	}

	/* memory cleanup */
	dm_free(buffer);
	buffer = NULL;

	/* reset timers */
	alarm(0);

	return 0;
}


int tims_reset(PopSession_t * session)
{
	session->state = STRT;
	return 1;
}


int tims_error(PopSession_t * session, void *stream,
	       const char *formatstring, ...)
{
	va_list argp;

	if (session->error_count >= MAX_ERRORS) {
		TRACE(TRACE_MESSAGE, "too many errors (MAX_ERRORS is %d)", MAX_ERRORS);
		ci_write((FILE *) stream, "BYE \"Too many errors, closing connection.\"\r\n");
		session->SessionResult = 2;	/* possible flood */
		tims_reset(session);
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


int tims(clientinfo_t *ci, char *buffer, PopSession_t * session)
{
	/* returns values:
	 *  0 to quit
	 * -1 on failure
	 *  1 on success */
	FILE *stream = ci->tx;
	FILE *instream = ci->rx;
	char *command, *value;
	int cmdtype;
	int indx = 0;
	
	size_t tmplen = 0;
	size_t tmppos = 0;
	char *tmpleft = NULL;

	char *f_buf = NULL;
	sort_result_t *sort_result = NULL;
	
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
			TRACE(TRACE_DEBUG, "command issued: cmd [%s], val [%s]",
			      command, value);
		}
	}

	for (cmdtype = TIMS_STRT; cmdtype < TIMS_END; cmdtype++)
		if (strcasecmp(command, commands[cmdtype]) == 0)
			break;

	TRACE(TRACE_DEBUG, "command looked up as commandtype %d", cmdtype);

	/* commands that are allowed to have no arguments */
	if ((value == NULL) && !(cmdtype < TIMS_NOARGS) && (cmdtype < TIMS_END)) {
		return tims_error(session, stream, "NO \"This command requires an argument.\"\r\n");
	}

	switch (cmdtype) {
	case TIMS_LOUT:
		ci_write((FILE *) stream, "OK\r\n");
		tims_reset(session);
		return 0;	/* return 0 to cause the connection to close */
		
	case TIMS_STLS:
		/* We don't support TLS, sorry! */
		ci_write((FILE *) stream, "NO\r\n");
		return 1;
		
	case TIMS_CAPA:
		/* This is macro-ized because it is also used in the greeting. */
		GREETING((FILE *) stream);
		return 1;
		
	case TIMS_AUTH:
		/* We currently only support plain authentication,
		 * which means that the command we accept will look
		 * like this: Authenticate "PLAIN" "base64-password"
		 * */
		if (strlen(value) > strlen("\"PLAIN\"")) {
			/* Only compare the first part of value */
			if (strncasecmp(value, "\"PLAIN\"", strlen("\"PLAIN\"")) == 0) {
				size_t tmplen = 0;
				size_t tmppos = 0;
				char *tmpleft = NULL, **tmp64 = NULL;

				/* First see if the base64 SASL is simply quoted */
				if (0 != find_bounded(value + strlen ("\"PLAIN\""), '"', '"', &tmpleft, &tmplen, &tmppos)) {
					u64_t authlen;	/* Actually, script length must be 32-bit unsigned int. */
					char tmpcharlen[11];	/* A 32-bit unsigned int is ten decimal digits in length. */

					/* Second, failing that, see if it's an {n+} literal */
					find_bounded(value + strlen("\"PLAIN\""), '{', '+', &tmpleft, &tmplen, &tmppos);

					strncpy(tmpcharlen, tmpleft, (10 < tmplen ? 10 : tmplen));
					tmpcharlen[(10 < tmplen ? 10 : tmplen)] = '\0';
					dm_free(tmpleft);

					authlen = strtoull(tmpcharlen, NULL, 10);
					if (authlen >= UINT_MAX) {
						ci_write((FILE *)stream, "NO \"Invalid SASL length.\"\r\n");
						tmplen = 0;	/* HACK: This prevents the next block from running. */
					} else {
						if (0 != read_from_stream((FILE *)instream, &tmpleft, authlen)) {
							ci_write((FILE *) stream, "NO \"Error reading SASL.\"\r\n");
						} else {
							tmplen = authlen;	/* HACK: This allows the next block to run. */
						}
					}
				}

				if (tmplen < 1) {
					/* Definitely an empty password string */
					ci_write((FILE *) stream, "NO \"Password required.\"\r\n");
				} else {
					size_t i;
					u64_t useridnr;

					tmpleft[tmplen] = '\0'; // Ensure there's a nul termination.
					tmp64 = base64_decodev(tmpleft);
					if (tmp64 == NULL) {
						ci_write((FILE *)stream, "NO \"SASL decode error.\"\r\n");
					} else {
						for (i = 0; tmp64[i] != NULL; i++) {	
							/* Just count 'em up */
						}
						if (i < 3) {
							ci_write((FILE *) stream, "NO \"Too few encoded SASL arguments.\"\r\n");
						}
						/* The protocol specifies that the base64 encoding
						 * be made up of three parts: proxy, username, password
						 * Between them are NULLs, which are conveniently encoded
						 * by the base64 process... */
						if (auth_validate(ci, tmp64[1], tmp64[2], &useridnr) == 1) {
							ci_write((FILE *) stream, "OK\r\n");
							session->state = AUTH;
							session->useridnr = useridnr;
							session->username = dm_strdup(tmp64[1]);
							session->password = dm_strdup(tmp64[2]);
							child_reg_connected_user(session->username);
						} else {
							ci_write((FILE *) stream, "NO \"Username or password incorrect.\"\r\n");
						}
						g_strfreev(tmp64);
					}
				}	/* if... tmplen < 1 */
			} /* if... strncasecmp() == "PLAIN" */
			else {
				TRACE(TRACE_INFO, "Input simply was not PLAIN auth");
				ci_write((FILE *) stream, "NO \"Authentication scheme not supported.\"\r\n");
			}
		} /* if... strlen() < "PLAIN" */
		else {
			TRACE(TRACE_INFO, "Input too short to possibly be PLAIN auth");
			ci_write((FILE *) stream, "NO \"Authentication scheme not supported.\"\r\n");
		}

		return 1;
	case TIMS_PUTS:
		if (session->state != AUTH) {
			ci_write((FILE *) stream, "NO \"Please authenticate first.\"\r\n");
			break;
		}
		
		tmplen = 0;
		tmppos = 0;
		tmpleft = NULL;

		find_bounded(value, '"', '"', &tmpleft, &tmplen, &tmppos);

		if (tmplen < 1) {
			/* Possibly an empty password... */
			ci_write((FILE *) stream, "NO \"Script name required.\"\r\n");
			break;
		}

		char scriptname[MAX_SIEVE_SCRIPTNAME + 1];

		strncpy(scriptname, tmpleft, (MAX_SIEVE_SCRIPTNAME < tmplen ?  MAX_SIEVE_SCRIPTNAME : tmplen));
		/* Of course, be sure to NULL terminate, because strncpy() likely won't */
		scriptname[(MAX_SIEVE_SCRIPTNAME < tmplen ?  MAX_SIEVE_SCRIPTNAME : tmplen)] = '\0';
		dm_free(tmpleft);

		/* Offset from the previous match to make sure not to pull
		 * the "length" from a script with a malicious name */
		find_bounded(value + tmppos, '{', '+', &tmpleft, &tmplen, &tmppos);

		if (tmplen < 1) {
			/* Possibly an empty password... */
			ci_write((FILE *) stream, "NO \"Length required.\"\r\n");
			break;
		}
		
		u64_t scriptlen;	/* Actually, script length must be 32-bit unsigned int. */
		char tmpcharlen[11];	/* A 32-bit unsigned int is ten decimal digits in length. */

		strncpy(tmpcharlen, tmpleft, (10 < tmplen ? 10 : tmplen));
		tmpcharlen[(10 < tmplen ? 10 : tmplen)] = '\0';
		dm_free(tmpleft);

		scriptlen = strtoull(tmpcharlen, NULL, 10);
		TRACE(TRACE_INFO, "Client sending script of length [%llu]", scriptlen);
		if (scriptlen >= UINT_MAX) {
			TRACE(TRACE_INFO, "Length [%llu] is larger than UINT_MAX [%u]",
					scriptlen, UINT_MAX);
			ci_write((FILE *) stream, "NO \"Invalid script length.\"\r\n");
			break;
		}
		

		if (0 != read_from_stream ((FILE *) instream, &f_buf, scriptlen)) {
			TRACE(TRACE_INFO, "Error reading script with read_from_stream()");
			ci_write((FILE *) stream, "NO \"Error reading script.\"\r\n");
			break;
		}
		
		if (0 != db_check_sievescript_quota(session->useridnr, scriptlen)) {
			TRACE(TRACE_INFO, "Script exceeds user's quota, dumping it");
			ci_write((FILE *) stream, "NO \"Script exceeds available space.\"\r\n");
			break;
		}


		/* Store the script temporarily,
		 * validate it, then rename it. */
		if (0 != db_add_sievescript(session->useridnr, "@!temp-script!@", f_buf)) {
			TRACE(TRACE_INFO, "Error inserting script");
			ci_write((FILE *) stream, "NO \"Error inserting script.\"\r\n");
			db_delete_sievescript(session->useridnr, "@!temp-script!@");
			break;
		}
		
		sort_result = sort_validate(session->useridnr, "@!temp-script!@");
		if (sort_result == NULL) {
			TRACE(TRACE_INFO, "Error inserting script");
			ci_write((FILE *) stream, "NO \"Error inserting script.\"\r\n");
			db_delete_sievescript(session->useridnr, "@!temp-script!@");
		} else if (sort_get_error(sort_result) > 0) {
			TRACE(TRACE_INFO, "Script has syntax errrors: [%s]",
					sort_get_errormsg(sort_result));
			ci_write((FILE *) stream, "NO \"Script error: %s.\"\r\n",
					sort_get_errormsg(sort_result));
			db_delete_sievescript(session->useridnr, "@!temp-script!@");
		} else {
			/* According to the draft RFC, a script with the same
			 * name as an existing script should [atomically] replace it. */
			if (0 != db_rename_sievescript(session->useridnr, "@!temp-script!@", scriptname)) {
				TRACE(TRACE_INFO, "Error inserting script");
				ci_write((FILE *) stream, "NO \"Error inserting script.\"\r\n");
			} else {
				TRACE(TRACE_INFO, "Script successfully received");
				ci_write((FILE *) stream, "OK \"Script successfully received.\"\r\n");
			}
		}
		break;
		
	case TIMS_SETS:
		if (session->state != AUTH) {
			ci_write((FILE *) stream, "NO \"Please authenticate first.\"\r\n");
			break;
		}
		
		int ret;
		tmplen = 0;
		tmppos = 0;
		tmpleft = NULL;

		find_bounded(value, '"', '"', &tmpleft, &tmplen, &tmppos);

		/* Only activate a new script if one was specified */
		if (tmplen > 0) {
			char scriptname[MAX_SIEVE_SCRIPTNAME + 1];

			strncpy(scriptname, tmpleft, (MAX_SIEVE_SCRIPTNAME < tmplen ?  MAX_SIEVE_SCRIPTNAME : tmplen));
			/* Of course, be sure to NULL terminate, because strncpy() likely won't */
			scriptname[(MAX_SIEVE_SCRIPTNAME < tmplen ?  MAX_SIEVE_SCRIPTNAME : tmplen)] = '\0';
			dm_free(tmpleft);

			ret = db_activate_sievescript(session->useridnr, scriptname);
			if (ret == -3) {
				ci_write((FILE *) stream, "NO \"Script does not exist.\"\r\n");
				return -1;
			} else if (ret != 0) {
				ci_write((FILE *) stream, "NO \"Internal error.\"\r\n");
				return -1;
			} else {
				ci_write((FILE *) stream, "OK \"Script activated.\"\r\n");
			}
		} else {
			char *scriptname = NULL;
			ret = db_get_sievescript_active(session->useridnr, &scriptname);
			if (scriptname == NULL) {
				ci_write((FILE *) stream, "OK \"No scripts are active at this time.\"\r\n");
			} else {
				ret = db_deactivate_sievescript(session->useridnr, scriptname);
				dm_free(scriptname);
				if (ret == -3) {
					ci_write((FILE *)stream, "NO \"Active script does not exist.\"\r\n");
					return -1;
				} else if (ret != 0) {
					ci_write((FILE *)stream, "NO \"Internal error.\"\r\n");
					return -1;
				} else {
					ci_write((FILE *)stream, "OK \"All scripts deactivated.\"\r\n");
				}
			}
		}
		return 1;
		
	case TIMS_GETS:
		if (session->state != AUTH) {
			ci_write((FILE *) stream, "NO \"Please authenticate first.\"\r\n");
			break;
		}
		
		tmplen = 0;
		tmppos = 0;
		tmpleft = NULL;

		find_bounded(value, '"', '"', &tmpleft, &tmplen, &tmppos);

		if (tmplen < 1) {
			/* Possibly an empty password... */
			ci_write((FILE *) stream, "NO \"Script name required.\"\r\n");
		} else {
			int ret = 0;
			char *script = NULL;
			char scriptname[MAX_SIEVE_SCRIPTNAME + 1];

			strncpy(scriptname, tmpleft, (MAX_SIEVE_SCRIPTNAME < tmplen ?  MAX_SIEVE_SCRIPTNAME : tmplen));
			/* Of course, be sure to NULL terminate, because strncpy() likely won't */
			scriptname[(MAX_SIEVE_SCRIPTNAME < tmplen ?  MAX_SIEVE_SCRIPTNAME : tmplen)] = '\0';
			dm_free(tmpleft);

			ret = db_get_sievescript_byname(session->useridnr, scriptname, &script);
			if (ret == -3) {
				ci_write((FILE *) stream, "NO \"Script not found.\"\r\n");
			} else if (ret != 0 || script == NULL) {
				ci_write((FILE *) stream, "NO \"Internal error.\"\r\n");
			} else {
				ci_write((FILE *) stream, "{%u+}\r\n", (unsigned int)strlen(script));
				ci_write((FILE *) stream, "%s\r\n", script);
				ci_write((FILE *) stream, "OK\r\n");
			}
		}
		return 1;
		
	case TIMS_DELS:
		if (session->state != AUTH) {
			ci_write((FILE *) stream, "NO \"Please authenticate first.\"\r\n");
			break;
		}
		
		tmplen = 0;
		tmppos = 0;
		tmpleft = NULL;

		find_bounded(value, '"', '"', &tmpleft, &tmplen, &tmppos);

		if (tmplen < 1) {
			/* Possibly an empty password... */
			ci_write((FILE *) stream, "NO \"Script name required.\"\r\n");
		} else {
			int ret = 0;
			char scriptname[MAX_SIEVE_SCRIPTNAME + 1];

			strncpy(scriptname, tmpleft, (MAX_SIEVE_SCRIPTNAME < tmplen ?  MAX_SIEVE_SCRIPTNAME : tmplen));
			/* Of course, be sure to NULL terminate, because strncpy() likely won't */
			scriptname[(MAX_SIEVE_SCRIPTNAME < tmplen ?  MAX_SIEVE_SCRIPTNAME : tmplen)] = '\0';
			dm_free(tmpleft);

			ret = db_delete_sievescript(session->useridnr, scriptname);
			if (ret == -3) {
				ci_write((FILE *) stream, "NO \"Script not found.\"\r\n");
			} else if (ret != 0) {
				ci_write((FILE *) stream, "NO \"Internal error.\"\r\n");
			} else {
				ci_write((FILE *) stream, "OK\r\n");
			}
		}
		return 1;
	case TIMS_SPAC:
		if (session->state != AUTH) {
			ci_write((FILE *) stream, "NO \"Please authenticate first.\"\r\n");
			break;
		}

		// Command is in format: HAVESPACE "scriptname" 12345
		// TODO: Actually parse for the size when the quota
		// functions get implemented. For now, just pretend.
		if (db_check_sievescript_quota(session->useridnr, 12345) == DM_SUCCESS)
			ci_write((FILE *) stream, "OK \"Command not implemented.\"\r\n");
		else
			ci_write((FILE *) stream, "NO (QUOTA) \"Quota exceeded\"\r\n");
		return 1;
		
	case TIMS_LIST:
		if (session->state != AUTH) {
			ci_write((FILE *) stream, "NO \"Please authenticate first.\"\r\n");
			break;
		}
		
		struct dm_list scriptlist;
		struct element *tmp;

		if (db_get_sievescript_listall (session->useridnr, &scriptlist) < 0) {
			ci_write((FILE *) stream, "NO \"Internal error.\"\r\n");
		} else {
			if (dm_list_length(&scriptlist) == 0) {
				/* The command hasn't failed, but there aren't any scripts */
				ci_write((FILE *) stream, "OK \"No scripts found.\"\r\n");
			} else {
				tmp = dm_list_getstart(&scriptlist);
				while (tmp != NULL) {
					sievescript_info_t *info = (sievescript_info_t *) tmp->data;
					ci_write((FILE *)stream, "\"%s\"%s\r\n", info->name, (info-> active == 1 ?  " ACTIVE" : ""));
					tmp = tmp->nextnode;
				}
				ci_write((FILE *) stream, "OK\r\n");
			}
			if (scriptlist.start)
				dm_list_free(&scriptlist.start);
		}
		return 1;
		
	default:
		return tims_error(session, stream,
				"NO \"What are you trying to say here?\"\r\n");
	}

	if (sort_result)
		sort_free_result(sort_result);
	if (f_buf)
		dm_free(f_buf);
	
	return 1;
}
