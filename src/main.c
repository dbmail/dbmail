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

/* 
 * 
 * main file for dbmail-deliver  */

#include "dbmail.h"

#define THIS_MODULE "deliver"

#define MESSAGEIDSIZE 100
#define NORMAL_DELIVERY 1
#define SPECIAL_DELIVERY 2

#define INDEX_DELIVERY_MODE 1

/* value for the size of the blocks to read from the input stream.
   this can be any value (so 8192 bytes is just a raw guess.. */
#define READ_CHUNK_SIZE 8192
/* syslog */
#define PNAME "dbmail/deliver"

extern char configFile[PATH_MAX];

int brute_force = 0;
char *deliver_to_header = NULL;
char *deliver_to_mailbox = NULL;

/* Loudness and assumptions. */
int verbose = 0;
/* Not used, but required to link with libdbmail.so */
int no_to_all = 0;
int yes_to_all = 0;
int reallyquiet = 0;
int quiet = 0;

void do_showhelp(void) {
	printf(
	"*** dbmail-deliver ***\n"
//	Try to stay under the standard 80 column width
//	0........10........20........30........40........50........60........70........80
	"Use this program to deliver mail from your MTA or on the command line.\n"
	"See the man page for more info. Summary:\n\n"
	"     -t [headerfield]   for normal deliveries (default is \"delivered-to\")\n"
	"     -d [addresses]     for delivery without using scanner\n"
	"     -u [usernames]     for direct delivery to users\n"
	"     -m \"mailbox\"     for delivery to a specific mailbox\n"
	"     -M \"mailbox\"     as -m, but skip permissions checks and Sieve scripts\n"
	"     -r return path     for address of bounces and other error reports\n"

	"\nCommon options for all DBMail utilities:\n"
	"     -f file   specify an alternative config file\n"
	"     -q        quietly skip interactive prompts\n"
	"               use twice to suppress error messages\n"
	"     -n        show the intended action but do not perform it, no to all\n"
	"     -y        perform all proposed actions, as though yes to all\n"
	"     -v        verbose details\n"
	"     -V        show the version\n"
	"     -h        show this help message\n"
	);
}

int main(int argc, char *argv[])
{
#define READ_SIZE 1024
	int exitcode = 0;
	int c, c_prev = 0, usage_error = 0;
	ssize_t n = 0;
	GString *raw = NULL;
	DbmailMessage *msg = NULL;
	char buf[READ_SIZE], *returnpath = NULL;
	GList *userlist = NULL;
	Mempool_T pool = mempool_open();
	List_T dsnusers = p_list_new(pool);
	Delivery_T *dsnuser;
	
	g_mime_init();
	
	config_get_file();

	openlog(PNAME, LOG_PID, LOG_MAIL);

	/* Check for commandline options.
	 * The initial '-' means that arguments which are not associated
	 * with an immediately preceding option are return with option 
	 * value '1'. We will use this to allow for multiple values to
	 * follow after each of the supported options. */
	while ((c = getopt(argc, argv, "-t::m:M:u:d:r: f:qnyvVh")) != EOF) {
		/* Received an n-th value following the last option,
		 * so recall the last known option to be used in the switch. */
		if (c == 1)
			c = c_prev;
		c_prev = c;
		/* Do something with this option. */
		switch (c) {
		case 't':
			TRACE(TRACE_INFO, "using NORMAL_DELIVERY");

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

		case 'M':
			TRACE(TRACE_INFO, "using BRUTE FORCE delivery");

			if (brute_force) {
				printf("Only one mailbox name may be specified.\n");
				usage_error = 1;
			} else
				brute_force = 1;
			/* Fall through. */
		case 'm':
			TRACE(TRACE_INFO, "using SPECIAL_DELIVERY to mailbox");

			if (deliver_to_mailbox) {
				printf("Only one mailbox name may be specified.\n");
				usage_error = 1;
			} else
				deliver_to_mailbox = optarg;

			break;

		case 'r':
			TRACE(TRACE_INFO, "using RETURN_PATH for bounces");

			/* Add argument onto the returnpath list. */
			returnpath = g_strdup(optarg);
			break;

		case 'u':
			TRACE(TRACE_INFO, "using SPECIAL_DELIVERY to usernames");

			dsnuser = g_new0(Delivery_T,1);
			dsnuser_init(dsnuser);
			dsnuser->address = g_strdup(optarg);
			dsnuser->source = BOX_COMMANDLINE;

			dsnusers = p_list_prepend(dsnusers, dsnuser);

			break;

		case 'd':
			TRACE(TRACE_INFO, "using SPECIAL_DELIVERY to email addresses");

			dsnuser = g_new0(Delivery_T,1);
			dsnuser_init(dsnuser);
			dsnuser->address = g_strdup(optarg);
			dsnuser->source = BOX_COMMANDLINE;

			dsnusers = p_list_prepend(dsnusers, dsnuser);

			break;

		/* Common command line options. */
		case 'f':
			if (optarg && strlen(optarg) > 0) {
				memset(configFile, 0, sizeof(configFile));
				strncpy(configFile, optarg, sizeof(configFile)-1);
			} else {
				fprintf(stderr, "dbmail-deliver: -f requires a filename\n\n" );
				return 1;
			}
			break;

		case 'v':
			verbose = 1;
			break;

		case 'V':
			/* We must return non-zero in case someone put -V
			 * into the mail server config and thus may lose mail. */
			PRINTF_THIS_IS_DBMAIL;
			return 1;

		default:
			usage_error = 1;
			break;
		}

		/* At the end of each round of options, check
		 * to see if there were any errors worth stopping for. */
		if (usage_error) {
			do_showhelp();
			TRACE(TRACE_DEBUG, "usage error; setting EX_USAGE and aborting");
			exitcode = EX_USAGE;
			goto freeall;
		}
	}

	/* ...or if there weren't any command line arguments at all. */
	if (argc < 2) {
		do_showhelp();
		TRACE(TRACE_DEBUG, "no arguments; setting EX_USAGE and aborting");
		exitcode = EX_USAGE;
		goto freeall;
	}

	/* Read in the config file; do it after getopt
	 * in case -f config.alt was specified. */
	if (config_read(configFile) == -1) {
		TRACE(TRACE_ERR, "error reading alternate config file [%s]", configFile);
		exitcode = EX_TEMPFAIL;
		goto freeall;

	}
	SetTraceLevel("SMTP");
	GetDBParams();

	if (db_connect() != 0) {
		TRACE(TRACE_ERR, "database connection failed");
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}

	if (auth_connect() != 0) {
		TRACE(TRACE_ERR, "authentication connection failed");
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}
	
	/* read the whole message */
	memset(buf, 0, sizeof(buf));
	while ( (n = read(fileno(stdin), (void *)buf, READ_SIZE-1)) > 0) {
		if (! raw)
			raw = g_string_new(buf);
		else
			raw = g_string_append(raw, buf);
		memset(buf, 0, sizeof(buf));
	}

	msg = dbmail_message_new(NULL);
	if (! raw) {
		TRACE(TRACE_ERR, "Please supply the message");
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}
	if (! (msg = dbmail_message_init_with_string(msg, raw->str))) {
		TRACE(TRACE_ERR, "error reading message");
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}

	/* Use the -r flag to set the Return-Path header,
	 * or leave an existing value,
	 * or copy the From header,
	 * debug message if all fails. */
	if (returnpath) {
		TRACE(TRACE_DEBUG, "Setting provided Return-Path: [%s]", returnpath);
		dbmail_message_set_header(msg, "Return-Path", returnpath);
	} else if (dbmail_message_get_header(msg, "Return-Path")) {
		TRACE(TRACE_DEBUG, "Return-Path provided so doing nothing");
		// Do nothing.
	} else if (dbmail_message_get_header(msg, "From")) {
		TRACE(TRACE_DEBUG, "Setting Return-Path using From");
		// FIXME: This might not be a valid address;
		// mail_address_build_list used to fix that, I think.
		dbmail_message_set_header(msg, "Return-Path", dbmail_message_get_header(msg, "From"));
	} else {
		TRACE(TRACE_DEBUG, "no return path found");
	}

	/* If the NORMAL delivery mode has been selected... */
	if (deliver_to_header != NULL) {
		/* parse for destination addresses */
		TRACE(TRACE_DEBUG, "scanning for [%s]", deliver_to_header);
		if (! (userlist = dbmail_message_get_header_addresses(msg, deliver_to_header))) {
			TRACE(TRACE_NOTICE, "no email addresses (scanned for %s)", deliver_to_header);
			exitcode = EX_NOUSER;
			goto freeall;
		}

		/* Loop through the users list, moving the entries into the dsnusers list. */
		userlist = g_list_first(userlist);
		while (userlist) {
			dsnuser = g_new0(Delivery_T,1);
			dsnuser_init(dsnuser);
			dsnuser->address = g_strdup((char *) userlist->data);
	
			dsnusers = p_list_prepend(dsnusers, dsnuser);

			if (! g_list_next(userlist))
				break;
			userlist = g_list_next(userlist);
		}
	}

	/* If the MAILBOX delivery mode has been selected... */
	if (deliver_to_mailbox != NULL) {
		TRACE(TRACE_DEBUG, "setting mailbox for all deliveries to [%s]", deliver_to_mailbox);
		/* Loop through the dsnusers list, setting the destination mailbox. */
		dsnusers = p_list_first(dsnusers);
		while (dsnusers) {
			((Delivery_T *)p_list_data(dsnusers))->mailbox = g_strdup(deliver_to_mailbox);
			if (brute_force) {
				((Delivery_T *)p_list_data(dsnusers))->source = BOX_BRUTEFORCE;
			} else {
				((Delivery_T *)p_list_data(dsnusers))->source = BOX_COMMANDLINE;
			}
			if (! p_list_next(dsnusers))
				break;
			dsnusers = p_list_next(dsnusers);
		}
	}

	if (dsnuser_resolve_list(dsnusers) == -1) {
		TRACE(TRACE_ERR, "dsnuser_resolve_list failed");
		/* Most likely a random failure... */
		exitcode = EX_TEMPFAIL;
		goto freeall;
	}

	/* inserting messages into the database */
	if (insert_messages(msg, dsnusers) == -1) {
		TRACE(TRACE_ERR, "insert_messages failed");
		/* Most likely a random failure... */
		exitcode = EX_TEMPFAIL;
	}

	freeall:	/* Goto's here! */
	
	/* If there wasn't already an EX_TEMPFAIL from insert_messages(),
	 * then see if one of the status flags was marked with an error. */
	if (!exitcode) {
		const char *class, *subject, *detail;
		delivery_status_t final_dsn;
		set_dsn(&final_dsn, 0, 0, 0);

		/* Get one reasonable error code for everyone.
		 * This is an inherently unreasonable process,
		 * and can lead to repeated attempts to deliver mail
		 * when just one of several recipients has a problem. */
		final_dsn.class = dsnuser_worstcase_list(dsnusers);
		if (final_dsn.class == 6) /* Hack for DSN_CLASS_QUOTA */
			set_dsn(&final_dsn, 5, 2, 2);
		dsn_tostring(final_dsn, &class, &subject, &detail);

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
			if (final_dsn.subject == 2) /* Mailbox Status */
				exitcode = EX_CANTCREAT;
			else
				exitcode = EX_NOUSER;
			break;
		}

		/* Unfortunately, dbmail-deliver only gets to return a single worst-case code. */
		TRACE(TRACE_NOTICE, "exit code [%d] from DSN [%d%d%d  %s %s %s]",
			exitcode,
			final_dsn.class, final_dsn.subject, final_dsn.detail,
			class, subject, detail);
	} else {
		/* Something went wrong earlier on, get louder about it. */
		TRACE(TRACE_WARNING, "exit code [%d] because something went wrong;"
			" turn up trace level for more detail", exitcode);
	}

	if (raw)
		g_string_free(raw, TRUE);

	dbmail_message_free(msg);
	dsnuser_free_list(dsnusers);
	g_list_destroy(userlist);
	g_free(returnpath);
	mempool_close(&pool);

	db_disconnect();
	auth_disconnect();
	config_free();
	g_mime_shutdown();

	return exitcode;
}

