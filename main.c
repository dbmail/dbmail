/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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
 * main file for dbmail-smtp  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmail.h"
#include "dbmail-message.h"
#include "main.h"
#include "pipe.h"
#include "list.h"
#include "auth.h"
#include "dsn.h"
#include <string.h>
#include <stdlib.h>
/* For getopt() */
#include <unistd.h>
/* For exit codes */
#include <sysexits.h>

#define MESSAGEIDSIZE 100
#define NORMAL_DELIVERY 1
#define SPECIAL_DELIVERY 2

#define INDEX_DELIVERY_MODE 1

/* value for the size of the blocks to read from the input stream.
   this can be any value (so 8192 bytes is just a raw guess.. */
#define READ_CHUNK_SIZE 8192
/* syslog */
#define PNAME "dbmail/smtp"

struct list returnpath;		/* returnpath (should aways be just 1 hop) */
struct list mimelist;		/* raw unformatted mimefields and values */
struct list dsnusers;		/* list of deliver_to_user_t structs */
struct list users;		/* list of email addresses in message */
struct element *tmp;

char *configFile = DEFAULT_CONFIG_FILE;

extern db_param_t _db_params;	/* set up database login data */

deliver_to_user_t dsnuser;

//char *header = NULL;
char *deliver_to_header = NULL;
char *deliver_to_mailbox = NULL;

/* loudness and assumptions */
int yes_to_all = 0;
int no_to_all = 0;
int verbose = 0;
/* Don't be helpful. */
int quiet = 0;
/* Don't print errors. */
int reallyquiet = 0;

int do_showhelp(void) {
	printf("*** dbmail-smtp ***\n");

	printf("Use this program to deliver mail from your MTA or on the command line.\n");
	printf("See the man page for more info. Summary:\n\n");
	printf("     -t [headerfield]   for normal deliveries (default is \"deliver-to\")\n");
	printf("     -d [addresses]     for delivery without using scanner\n");
	printf("     -u [usernames]     for direct delivery to users\n");
	printf("     -m \"mailbox\"       for delivery to a specific mailbox\n");
	printf("     -r return path     for address of bounces and other error reports\n");

	printf("\nCommon options for all DBMail utilities:\n");
	printf("     -f file   specify an alternative config file\n");
	printf("     -q        quietly skip interactive prompts\n"
	       "               use twice to suppress error messages\n");
	printf("     -n        show the intended action but do not perform it, no to all\n");
	printf("     -y        perform all proposed actions, as though yes to all\n");
	printf("     -v        verbose details\n");
	printf("     -V        show the version\n");
	printf("     -h        show this help message\n");

	return 0;
}

int main(int argc, char *argv[])
{
	int exitcode = 0;
	int c, c_prev = 0, usage_error = 0;
	struct DbmailMessage *msg = NULL;
	
	openlog(PNAME, LOG_PID, LOG_MAIL);

	list_init(&users);
	list_init(&dsnusers);
	list_init(&mimelist);
	list_init(&returnpath);

	/* Check for commandline options.
	 * The initial '-' means that arguments which are not associated
	 * with an immediately preceding option are return with option 
	 * value '1'. We will use this to allow for multiple values to
	 * follow after each of the supported options. */
	while ((c = getopt(argc, argv, "-t::m:u:d:r: f:qnyvVh")) != EOF) {
		/* Received an n-th value following the last option,
		 * so recall the last known option to be used in the switch. */
		if (c == 1)
			c = c_prev;
		c_prev = c;
		/* Do something with this option. */
		switch (c) {
		case 't':
			trace(TRACE_INFO, "main(): using NORMAL_DELIVERY");

			if (optarg) {
				if (deliver_to_header) {
					printf
					    ("Only one header field may be specified.\n");
					usage_error = 1;
				} else
					deliver_to_header = optarg;
			} else
				deliver_to_header = "deliver-to";

			break;

		case 'm':
			trace(TRACE_INFO,
			      "main(): using SPECIAL_DELIVERY to mailbox");

			if (deliver_to_mailbox) {
				printf
				    ("Only one header field may be specified.\n");
				usage_error = 1;
			} else
				deliver_to_mailbox = optarg;

			break;

		case 'r':
			trace(TRACE_INFO,
			      "main(): using RETURN_PATH for bounces");

			/* Add argument onto the returnpath list. */
			if (list_nodeadd
			    (&returnpath, optarg,
			     strlen(optarg) + 1) == 0) {
				trace(TRACE_ERROR,
				      "main(): list_nodeadd reports out of memory"
				      " while adding to returnpath");
				exitcode = EX_TEMPFAIL;
				goto freeall;
			}

			break;

		case 'u':
			trace(TRACE_INFO,
			      "main(): using SPECIAL_DELIVERY to usernames");

			dsnuser_init(&dsnuser);
			dsnuser.address = dm_strdup(optarg);

			/* Add argument onto the users list. */
			if (list_nodeadd
			    (&dsnusers, &dsnuser,
			     sizeof(deliver_to_user_t)) == 0) {
				trace(TRACE_ERROR,
				      "main(): list_nodeadd reports out of memory"
				      " while adding usernames");
				exitcode = EX_TEMPFAIL;
				goto freeall;
			}

			break;

		case 'd':
			trace(TRACE_INFO,
			      "main(): using SPECIAL_DELIVERY to email addresses");

			dsnuser_init(&dsnuser);
			dsnuser.address = dm_strdup(optarg);

			/* Add argument onto the users list. */
			if (list_nodeadd
			    (&dsnusers, &dsnuser,
			     sizeof(deliver_to_user_t)) == 0) {
				trace(TRACE_ERROR,
				      "main(): list_nodeadd reports out of memory"
				      " while adding email addresses");
				exitcode = EX_TEMPFAIL;
				goto freeall;
			}

			break;

		/* Common command line options. */
		case 'f':
			if (optarg && strlen(optarg) > 0)
				configFile = optarg;
			else {
				fprintf(stderr,
					"dbmail-smtp: -f requires a filename\n\n" );
				return 1;
			}
			break;

		case 'v':
			verbose = 1;
			break;

		case 'V':
			/* We must return non-zero in case someone put -V
			 * into the mail server config and thus may lose mail. */
			printf("\n*** DBMAIL: dbmail-smtp version "
			       "$Revision$ %s\n\n", COPYRIGHT);
			return 1;

		default:
			usage_error = 1;
			break;
		}

		/* At the end of each round of options, check
		 * to see if there were any errors worth stopping for. */
		if (usage_error) {
			do_showhelp();
			trace(TRACE_DEBUG,
			      "main(): usage error; setting EX_USAGE and aborting");
			exitcode = EX_USAGE;
			goto freeall;
		}
	}

	/* ...or if there weren't any command line arguments at all. */
	if (argc < 2) {
		do_showhelp();
		trace(TRACE_DEBUG,
		      "main(): no arguments; setting EX_USAGE and aborting");
		exitcode = EX_USAGE;
		goto freeall;
	}

	/* Read in the config file; do it after getopt
	 * in case -f config.alt was specified. */
	if (config_read(configFile) == -1) {
		trace(TRACE_ERROR,
		      "main(): error reading alternate config file [%s]", configFile);
		exitcode = EX_TEMPFAIL;
		goto freeall;

	}
	SetTraceLevel("SMTP");
	GetDBParams(&_db_params);

	if (db_connect() != 0) {
		trace(TRACE_ERROR, "main(): database connection failed");
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}

	if (auth_connect() != 0) {
		trace(TRACE_ERROR,
		      "main(): authentication connection failed");
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}
	
	/* read the whole message */
	if (! (msg = dbmail_message_new_from_stream(stdin, DBMAIL_STREAM_PIPE))) {
		trace(TRACE_ERROR, "%s,%s: read_whole_message_pipe() failed",
		      __FILE__, __func__);
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}
	
	if (dbmail_message_get_hdrs_size(msg) > READ_BLOCK_SIZE) {
		trace(TRACE_ERROR,
		      "%s,%s: failed to read header because header is "
		      "too big (bigger than READ_BLOCK_SIZE (%llu))",
		      __FILE__, __func__, (u64_t) READ_BLOCK_SIZE);
		exitcode = EX_DATAERR;
		goto freeall;
	}

	/* parse the list and scan for field and content */
	if (mime_fetch_headers(dbmail_message_hdrs_to_string(msg), &mimelist) < 0) {
		trace(TRACE_ERROR,
		      "main(): mime_fetch_headers failed to read a header list");
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}

	/* parse returnpath from header */
	if (returnpath.total_nodes == 0)
		mail_address_build_list("Return-Path", &returnpath, &mimelist);
	if (returnpath.total_nodes == 0)
		mail_address_build_list("From", &returnpath, &mimelist);
	if (returnpath.total_nodes == 0)
		trace(TRACE_DEBUG, "main(): no return path found.");

	/* If the NORMAL delivery mode has been selected... */
	if (deliver_to_header != NULL) {
		/* parse for destination addresses */
		trace(TRACE_DEBUG, "main(): scanning for [%s]", deliver_to_header);
		if (mail_address_build_list(deliver_to_header, &users, &mimelist) != 0) {
			trace(TRACE_STOP, "main(): scanner found no email addresses (scanned for %s)",
			      deliver_to_header);
			exitcode = EX_NOUSER;
			goto freeall;
		}

		/* Loop through the users list, moving the entries into the dsnusers list. */
		for (tmp = list_getstart(&users); tmp != NULL;
		     tmp = tmp->nextnode) {
			deliver_to_user_t dsnuser;

			dsnuser_init(&dsnuser);
			dsnuser.address = dm_strdup((char *) tmp->data);

			list_nodeadd(&dsnusers, &dsnuser, sizeof(deliver_to_user_t));
		}
	}

	/* If the MAILBOX delivery mode has been selected... */
	if (deliver_to_mailbox != NULL) {
		trace(TRACE_DEBUG, "main(): setting mailbox for all deliveries to [%s]",
		      deliver_to_mailbox);
		/* Loop through the dsnusers list, setting the destination mailbox. */
		for (tmp = list_getstart(&dsnusers); tmp != NULL;
		     tmp = tmp->nextnode) {
			((deliver_to_user_t *)tmp->data)->mailbox = dm_strdup(deliver_to_mailbox);
		}
	}

	if (dsnuser_resolve_list(&dsnusers) == -1) {
		trace(TRACE_ERROR, "main(): dsnuser_resolve_list failed");
		/* Most likely a random failure... */
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}

	/* inserting messages into the database */
	if (insert_messages(msg, &mimelist, &dsnusers, &returnpath) == -1) {
		trace(TRACE_ERROR, "main(): insert_messages failed");
		/* Most likely a random failure... */
		exitcode = EX_TEMPFAIL;
	}

      freeall:			/* Goto's here! */
	
	/* If there wasn't already an EX_TEMPFAIL from insert_messages(),
	 * then see if one of the status flags was marked with an error. */
	if (!exitcode) {
		delivery_status_t final_dsn;

		/* Get one reasonable error code for everyone. */
		final_dsn = dsnuser_worstcase_list(&dsnusers);

		switch (final_dsn.class) {
		case DSN_CLASS_OK:
			exitcode = EX_OK;
			break;
		case DSN_CLASS_TEMP:
			exitcode = EX_TEMPFAIL;
			break;
		case DSN_CLASS_NONE:
		case DSN_CLASS_FAIL:
			/* If we're over-quota, say that,
			 * else it's a generic user error. */
			if (final_dsn.subject = 2)
				exitcode = EX_CANTCREAT;
			else
				exitcode = EX_NOUSER;
			break;
		}
	}

	trace(TRACE_DEBUG, "main(): freeing dsnuser list");
	dsnuser_free_list(&dsnusers);

	trace(TRACE_DEBUG, "main(): freeing all other lists");
	list_freelist(&mimelist.start);
	list_freelist(&returnpath.start);
	list_freelist(&users.start);

	dbmail_message_free(msg);

	trace(TRACE_DEBUG, "main(): they're all free. we're done.");

	db_disconnect();
	auth_disconnect();
	config_free();

	trace(TRACE_DEBUG, "main(): exit code is [%d].", exitcode);
	return exitcode;
}
