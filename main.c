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

/* $Id: main.c 2058 2006-04-03 11:57:18Z paul $
 * 
 * main file for dbmail-smtp  */

#include "dbmail.h"

#define MESSAGEIDSIZE 100
#define NORMAL_DELIVERY 1
#define SPECIAL_DELIVERY 2

#define INDEX_DELIVERY_MODE 1

/* value for the size of the blocks to read from the input stream.
   this can be any value (so 8192 bytes is just a raw guess.. */
#define READ_CHUNK_SIZE 8192
/* syslog */
#define PNAME "dbmail/smtp"

struct dm_list dsnusers;		/* list of deliver_to_user_t structs */
struct dm_list users;		/* list of email addresses in message */
struct element *tmp;

char *configFile = DEFAULT_CONFIG_FILE;

extern db_param_t _db_params;	/* set up database login data */

deliver_to_user_t dsnuser;

//char *header = NULL;
char *deliver_to_header = NULL;
char *deliver_to_mailbox = NULL;

/* loudness and assumptions */
static int verbose = 0;

int do_showhelp(void) {
	printf("*** dbmail-smtp ***\n");

	printf("Use this program to deliver mail from your MTA or on the command line.\n");
	printf("See the man page for more info. Summary:\n\n");
	printf("     -t [headerfield]   for normal deliveries (default is \"delivered-to\")\n");
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
	char *returnpath = NULL;
	GList *userlist = NULL;
	
	g_mime_init(0);
	
	openlog(PNAME, LOG_PID, LOG_MAIL);

	dm_list_init(&users);
	dm_list_init(&dsnusers);

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
			trace(TRACE_INFO, "%s,%s: using NORMAL_DELIVERY", __FILE__, __func__);

			if (optarg) {
				if (deliver_to_header) {
					printf("Only one header field may be specified.\n");
					usage_error = 1;
				} else {
					deliver_to_header = optarg;
				}
			} else
				deliver_to_header = "delivered-to";
			

			break;

		case 'm':
			trace(TRACE_INFO, "%s,%s: using SPECIAL_DELIVERY to mailbox", __FILE__, __func__);

			if (deliver_to_mailbox) {
				printf("Only one mailbox name may be specified.\n");
				usage_error = 1;
			} else
				deliver_to_mailbox = optarg;

			break;

		case 'r':
			trace(TRACE_INFO, "%s,%s: using RETURN_PATH for bounces", __FILE__, __func__);

			/* Add argument onto the returnpath list. */
			returnpath = dm_strdup(optarg);
			break;

		case 'u':
			trace(TRACE_INFO, "%s,%s: using SPECIAL_DELIVERY to usernames", __FILE__, __func__);

			dsnuser_init(&dsnuser);
			dsnuser.address = dm_strdup(optarg);
			dsnuser.source = BOX_COMMANDLINE;

			/* Add argument onto the users list. */
			if (dm_list_nodeadd (&dsnusers, &dsnuser, sizeof(deliver_to_user_t)) == 0) {
				trace(TRACE_ERROR, "%s,%s: out of memory while adding usernames", __FILE__, __func__);
				exitcode = EX_TEMPFAIL;
				goto freeall;
			}

			break;

		case 'd':
			trace(TRACE_INFO, "%s,%s: using SPECIAL_DELIVERY to email addresses", __FILE__, __func__);

			dsnuser_init(&dsnuser);
			dsnuser.address = dm_strdup(optarg);
			dsnuser.source = BOX_COMMANDLINE;

			/* Add argument onto the users list. */
			if (dm_list_nodeadd (&dsnusers, &dsnuser, sizeof(deliver_to_user_t)) == 0) {
				trace(TRACE_ERROR, "%s,%s: out of memory while adding email addresses", __FILE__, __func__);
				exitcode = EX_TEMPFAIL;
				goto freeall;
			}

			break;

		/* Common command line options. */
		case 'f':
			if (optarg && strlen(optarg) > 0)
				configFile = optarg;
			else {
				fprintf(stderr, "dbmail-smtp: -f requires a filename\n\n" );
				return 1;
			}
			break;

		case 'v':
			verbose = 1;
			break;

		case 'V':
			/* We must return non-zero in case someone put -V
			 * into the mail server config and thus may lose mail. */
			printf("\n*** DBMAIL: dbmail-smtp version $Revision: 2058 $ %s\n\n", COPYRIGHT);
			return 1;

		default:
			usage_error = 1;
			break;
		}

		/* At the end of each round of options, check
		 * to see if there were any errors worth stopping for. */
		if (usage_error) {
			do_showhelp();
			trace(TRACE_DEBUG, "%s,%s: usage error; setting EX_USAGE and aborting", __FILE__, __func__);
			exitcode = EX_USAGE;
			goto freeall;
		}
	}

	/* ...or if there weren't any command line arguments at all. */
	if (argc < 2) {
		do_showhelp();
		trace(TRACE_DEBUG, "%s,%s: no arguments; setting EX_USAGE and aborting", __FILE__, __func__);
		exitcode = EX_USAGE;
		goto freeall;
	}

	/* Read in the config file; do it after getopt
	 * in case -f config.alt was specified. */
	if (config_read(configFile) == -1) {
		trace(TRACE_ERROR, "%s,%s: error reading alternate config file [%s]", __FILE__, __func__, configFile);
		exitcode = EX_TEMPFAIL;
		goto freeall;

	}
	SetTraceLevel("SMTP");
	GetDBParams(&_db_params);

	if (db_connect() != 0) {
		trace(TRACE_ERROR, "%s,%s: database connection failed", __FILE__, __func__);
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}

	if (auth_connect() != 0) {
		trace(TRACE_ERROR, "%s,%s: authentication connection failed", __FILE__, __func__);
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}
	
        if (db_check_version() != 0) {
                exitcode = EX_TEMPFAIL;
                goto freeall;
        }

	/* read the whole message */
	if (! (msg = dbmail_message_new_from_stream(stdin, DBMAIL_STREAM_PIPE))) {
		trace(TRACE_ERROR, "%s,%s: error reading message",
		      __FILE__, __func__);
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}
	
	if (dbmail_message_get_hdrs_size(msg, FALSE) > READ_BLOCK_SIZE) {
		trace(TRACE_ERROR, "%s,%s: failed to read header because header is "
		      "too big (larger than READ_BLOCK_SIZE (%llu))",
		      __FILE__, __func__, (u64_t) READ_BLOCK_SIZE);
		exitcode = EX_DATAERR;
		goto freeall;
	}

	/* Use the -r flag to set the Return-Path header,
	 * or leave an existing value,
	 * or copy the From header,
	 * debug message if all fails. */
	if (returnpath) {
		dbmail_message_set_header(msg, "Return-Path", returnpath);
	} else if (dbmail_message_get_header(msg, "Return-Path")) {
		// Do nothing.
	} else if (dbmail_message_get_header(msg, "From")) {
		// FIXME: This might not be a valid address;
		// mail_address_build_list used to fix that, I think.
		dbmail_message_set_header(msg, "Return-Path", dbmail_message_get_header(msg, "From"));
	} else {
		trace(TRACE_DEBUG, "%s,%s: no return path found", __FILE__, __func__);
	}

	/* If the NORMAL delivery mode has been selected... */
	if (deliver_to_header != NULL) {
		/* parse for destination addresses */
		trace(TRACE_DEBUG, "%s,%s: scanning for [%s]",
				__FILE__, __func__, deliver_to_header);
		if (! (userlist = dbmail_message_get_header_addresses(msg, deliver_to_header))) {
			trace(TRACE_MESSAGE, "%s,%s: no email addresses (scanned for %s)",
					__FILE__, __func__, deliver_to_header);
			exitcode = EX_NOUSER;
			goto freeall;
		}

		/* Loop through the users list, moving the entries into the dsnusers list. */
		userlist = g_list_first(userlist);
		while (1) {
			dsnuser_init(&dsnuser);
			dsnuser.address = dm_strdup((char *) userlist->data);

			if (! dm_list_nodeadd(&dsnusers, &dsnuser, sizeof(deliver_to_user_t))) {
				trace(TRACE_ERROR,"%s,%s: out of memory in dm_list_nodeadd",
						__FILE__, __func__);
				exitcode = EX_TEMPFAIL;
				goto freeall;
			}
			if (! g_list_next(userlist))
				break;
			userlist = g_list_next(userlist);
		}
	}

	/* If the MAILBOX delivery mode has been selected... */
	if (deliver_to_mailbox != NULL) {
		trace(TRACE_DEBUG, "%s,%s: setting mailbox for all deliveries to [%s]", __FILE__, __func__, deliver_to_mailbox);
		/* Loop through the dsnusers list, setting the destination mailbox. */
		for (tmp = dm_list_getstart(&dsnusers); tmp != NULL; tmp = tmp->nextnode) {
			((deliver_to_user_t *)tmp->data)->mailbox = dm_strdup(deliver_to_mailbox);
			((deliver_to_user_t *)tmp->data)->source = BOX_COMMANDLINE;
		}
	}

	if (dsnuser_resolve_list(&dsnusers) == -1) {
		trace(TRACE_ERROR, "%s,%s: dsnuser_resolve_list failed", __FILE__, __func__);
		/* Most likely a random failure... */
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}

	/* inserting messages into the database */
	if (insert_messages(msg, &dsnusers) == -1) {
		trace(TRACE_ERROR, "%s,%s: insert_messages failed", __FILE__, __func__);
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
		case DSN_CLASS_QUOTA:
		case DSN_CLASS_FAIL:
			/* If we're over-quota, say that,
			 * else it's a generic user error. */
			if (final_dsn.subject == 2)
				exitcode = EX_CANTCREAT;
			else
				exitcode = EX_NOUSER;
			break;
		}
	}

	dbmail_message_free(msg);
	dsnuser_free_list(&dsnusers);
	dm_list_free(&users.start);
	dm_free(returnpath);
	g_list_foreach(userlist, (GFunc)g_free, NULL);
	g_list_free(userlist);

	trace(TRACE_DEBUG, "%s,%s: they're all free. we're done.", __FILE__, __func__);

	db_disconnect();
	auth_disconnect();
	config_free();

	g_mime_shutdown();

	trace(TRACE_DEBUG, "%s,%s: exit code is [%d].", __FILE__, __func__, exitcode);
	return exitcode;
}
