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
 * This is the dbmail housekeeping program. 
 *	It checks the integrity of the database and does a cleanup of all
 *	deleted messages. 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "maintenance.h"
#include "db.h"
#include "debug.h"
#include "dbmail.h"
#include "list.h"
#include "debug.h"
#include "auth.h"
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

/* Loudness and assumptions. */
int yes_to_all = 0;
int no_to_all = 0;
int verbose = 0;
/* Don't be helpful. */
int quiet = 0;
/* Don't print errors. */
int reallyquiet = 0;

#define qprintf(fmt, args...) \
	(quiet ? 0 : printf(fmt, ##args) )

#define qerrorf(fmt, args...) \
	(reallyquiet ? 0 : fprintf(stderr, fmt, ##args) )

char *configFile = DEFAULT_CONFIG_FILE;

/* set up database login data */
extern db_param_t _db_params;

static void find_time(char *timestr, const char *timespec);

int do_showhelp(void) {
	printf("*** dbmail-util ***\n");

	printf("Use this program to maintain your DBMail database.\n");
	printf("See the man page for more info. Summary:\n\n");
	printf("     -c        clean up unlinked message entries\n");
	printf("     -t        test for message integrity\n");
	printf("     -u        null message check\n");
	printf("     -r        repair any integrity problems\n");
	printf("     -p        purge messages have the DELETE status set\n");
	printf("     -d        set DELETE status for deleted messages\n");
	printf("     -l time   clear the IP log used for IMAP/POP-before-SMTP\n"
	       "               the time is specified as <hours>h<minutes>m\n"
	       "               (don't include the angle brackets, though)\n");
//	printf("     -i        enter an interactive message management console\n");

	printf("\nCommon options for all DBMail utilities:\n");
	printf("     -f file   specify an alternative config file\n");
	printf("     -q        quietly skip interactive prompts\n"
	       "               use twice to suppress error messages\n");
//	printf("     -n        show the intended action but do not perform it, no to all\n");
//	printf("     -y        perform all proposed actions, as though yes to all\n");
	printf("     -v        verbose details\n");
	printf("     -V        show the version\n");
	printf("     -h        show this help message\n");

	return 0;
}

int main(int argc, char *argv[])
{
	int should_fix = 0, check_integrity = 0, check_iplog = 0;
	int check_null_messages = 0;
	int show_help = 0, purge_deleted = 0, set_deleted = 0;
	int vacuum_db = 0;
	int do_nothing = 1;
	time_t start, stop;
	char timespec[LEN], timestr[LEN];
	int opt;
	struct list lostlist;
	struct element *el;
	u64_t id;
	u64_t deleted_messages;
	u64_t messages_set_to_delete;

	openlog(PNAME, LOG_PID, LOG_MAIL);
	setvbuf(stdout, 0, _IONBF, 0);

	/* get options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt(argc, argv, "-crtl:pud" "i" "f:qnyvVh")) != -1) {
		/* The initial "-" of optstring allows unaccompanied
		 * options and reports them as the optarg to opt 1 (not '1') */
		switch (opt) {
		case 'c':
			vacuum_db = 1;
			do_nothing = 0;
			break;

		case 'p':
			purge_deleted = 1;
			do_nothing = 0;
			break;

		case 'd':
			set_deleted = 1;
			do_nothing = 0;
			break;

		case 'r':
			check_integrity = 1;
			should_fix = 1;
			do_nothing = 0;
			break;

		case 't':
			check_integrity = 1;
			do_nothing = 0;
			break;

		case 'u':
			check_null_messages = 1;
			do_nothing = 0;
			break;

		case 'l':
			check_iplog = 1;
			do_nothing = 0;
			if (optarg)
				strncpy(timespec, optarg, LEN);
			else
				timespec[0] = 0;

			timespec[LEN] = 0;
			break;

		case 'i':
			qerrorf("Interactive console is not supported in this release.\n");
			return 1;

		/* Common options */
		case 'h':
			show_help = 1;
			do_nothing = 0;
			break;

		case 'n':
			qerrorf("-n option is not supported in this "
			       "release.\n");
			return 1;

		case 'y':
			qerrorf("-y option is not supported in this "
			       "release.\n");
			return 1;

		case 'q':
                        /* If we get q twice, be really quiet! */
                        if (quiet)
	                                reallyquiet = 1;
                        if (!verbose)
	                                quiet = 1;
			break;

		case 'f':
			if (optarg && strlen(optarg) > 0)
				configFile = optarg;
			else {
				qerrorf("dbmail-util: -f requires a filename\n\n" );
				return 1;
			}
			break;

		case 'v':
			verbose = 1;
			break;

		case 'V':
			qprintf("\n*** DBMAIL: dbmail-util version "
			       "$Revision$ %s\n\n", COPYRIGHT);
			return 1;

		default:
			/*printf("unrecognized option [%c], continuing...\n",optopt); */
			break;
		}
	}

	if (do_nothing || show_help) {
		do_showhelp();
		return 1;
	}

	ReadConfig("DBMAIL", configFile);
	SetTraceLevel("DBMAIL");
	GetDBParams(&_db_params);

	qprintf("Opening connection to database... \n");
	if (db_connect() != 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		return -1;
	}

	qprintf("Opening connection to authentication... \n");
	if (auth_connect() != 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		return -1;
	}

	qprintf("Ok. Connected\n");

	if (purge_deleted) {
		qprintf("Deleting messages with DELETE status... ");
		if (db_deleted_purge(&deleted_messages) < 0) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			db_disconnect();
			auth_disconnect();
			return -1;
		}
		qprintf("Ok. [%llu] messages deleted.\n", deleted_messages);
	}

	if (set_deleted) {
		qprintf("Setting DELETE status for deleted messages... ");
		if (db_set_deleted(&messages_set_to_delete) == -1) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			db_disconnect();
			auth_disconnect();
			return -1;
		}
		qprintf("Ok. [%llu] messages set for deletion.\n",
		       messages_set_to_delete);
		qprintf("Re-calculating used quota for all users... ");
		if (db_calculate_quotum_all() < 0) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			db_disconnect();
			auth_disconnect();
			return -1;
		}
		qprintf("Ok. Used quota updated for all users.\n");
	}

	if (check_null_messages) {
		qprintf("Now checking DBMAIL for NULL messages.. ");
		time(&start);

		if (db_icheck_null_messages(&lostlist) < 0) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			db_disconnect();
			auth_disconnect();
			return -1;
		}

		if (lostlist.total_nodes > 0) {
			qerrorf("Ok. Found [%ld] null messages:\n",
			       lostlist.total_nodes);

			el = lostlist.start;
			while (el) {
				id = *((u64_t *) el->data);
				if (db_set_message_status(id, MESSAGE_STATUS_ERROR) < 0)
					qerrorf
					    ("Warning: could not set message status #%llu. Check log.\n",
					     id);
				else
					qerrorf
					    ("%llu (status update to MESSAGE_STATUS_ERROR)\n",
					     id);

				el = el->nextnode;
			}

			list_freelist(&lostlist.start);

			qerrorf("\n");
		} else
			qprintf("Ok. Found 0 NULL messages.\n");

		time(&stop);
		qprintf("--- checking NULL messages took %g seconds\n",
		       difftime(stop, start));
		qprintf("Now checking DBMAIL for NULL physmessages..");
		time(&start);
		if (db_icheck_null_physmessages(&lostlist) < 0) {
			qerrorf
			    ("Failed, an error occured. Please check log.\n");
			db_disconnect();
			auth_disconnect();
			return -1;
		}

		if (lostlist.total_nodes > 0) {
			qerrorf
			    ("found %ld physmessages without messageblocks\n",
			     lostlist.total_nodes);
			el = lostlist.start;
			while (el) {
				id = *((u64_t *) el->data);
				if (db_delete_physmessage(id) < 0)
					qerrorf
					    ("Warning: couldn't delete physmessage");
				else
					qerrorf
					    ("deleted physmessage [%llu]\n",
					     id);
				el = el->nextnode;
			}
			list_freelist(&lostlist.start);
			qerrorf("\n");
		} else
			qprintf("found 0 physmessages without messageblks\n");

		time(&stop);
		qprintf("--- checking NULL physmessages took %g seconds\n",
			difftime(stop, start));
	}

	if (check_integrity) {
		qprintf("Now checking DBMAIL messageblocks integrity.. ");
		time(&start);

		/* this is what we do:
		 * First we're checking for loose messageblocks
		 * Secondly we're chekcing for loose messages
		 * Third we're checking for loose mailboxes 
		 */

		/* first part */
		if (db_icheck_messageblks(&lostlist) < 0) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			db_disconnect();
			auth_disconnect();
			return -1;
		}

		if (lostlist.total_nodes > 0) {
			qerrorf
			    ("Ok. Found [%ld] unconnected messageblks:\n",
			     lostlist.total_nodes);

			el = lostlist.start;
			while (el) {
				id = *((u64_t *) el->data);
				if (should_fix == 0)
					qerrorf("%llu ", id);
				else {
					if (db_delete_messageblk(id) < 0)
						qerrorf
						    ("Warning: could not delete messageblock #%llu. Check log.\n",
						     id);
					else
						qerrorf
						    ("%llu (removed from dbase)\n",
						     id);
				}

				el = el->nextnode;
			}

			list_freelist(&lostlist.start);

			qerrorf("\n");
			if (should_fix == 0) {
				qerrorf
				    ("Try running dbmail-util with the '-f' option "
				     "in order to fix these problems\n\n");
			}
		} else
			qprintf("Ok. Found 0 unconnected messageblks.\n");


		time(&stop);
		qprintf("--- checking block integrity took %g seconds\n",
		       difftime(stop, start));
		qprintf("--- checking block integrity took %g seconds\n",
			difftime(stop, start));

		/* second part */
		start = stop;
		qprintf("Now checking DBMAIL message integrity.. ");

		if (db_icheck_messages(&lostlist) < 0) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			db_disconnect();
			auth_disconnect();
			return -1;
		}

		if (lostlist.total_nodes > 0) {
			qerrorf("Ok. Found [%ld] unconnected messages:\n",
			       lostlist.total_nodes);

			el = lostlist.start;
			while (el) {
				id = *((u64_t *) el->data);

				if (should_fix == 0)
					qerrorf("%llu ", id);
				else {
					if (db_delete_message(id) < 0)
						qerrorf
						    ("Warning: could not delete message #%llu. Check log.\n",
						     id);
					else
						qerrorf
						    ("%llu (removed from dbase)\n",
						     id);
				}

				el = el->nextnode;
			}

			qerrorf("\n");
			if (should_fix == 0) {
				qerrorf
				    ("Try running dbmail-util with the '-f' option "
				     "in order to fix these problems\n\n");
			}
			list_freelist(&lostlist.start);

		} else
			qprintf("Ok. Found 0 unconnected messages.\n");

		time(&stop);
		qprintf("--- checking message integrity took %g seconds\n",
		       difftime(stop, start));
		qprintf("--- checking message integrity took %g seconds\n",
			difftime(stop, start));

		/* third part */
		qprintf("Now checking DBMAIL mailbox integrity.. ");
		start = stop;

		if (db_icheck_mailboxes(&lostlist) < 0) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			db_disconnect();
			auth_disconnect();
			return -1;
		}

		if (lostlist.total_nodes) {
			qerrorf("Ok. Found [%ld] unconnected mailboxes:\n",
			       lostlist.total_nodes);

			el = lostlist.start;
			while (el) {
				id = *((u64_t *) el->data);

				if (should_fix == 0)
					qerrorf("%llu ", id);
				else {
					if (db_delete_mailbox(id, 0, 0) <
					    0)
						qerrorf
						    ("Warning: could not delete mailbox #%llu. Check log.\n",
						     id);
					else
						qerrorf
						    ("%llu (removed from dbase)\n",
						     id);
				}

				el = el->nextnode;
			}

			qerrorf("\n");
			if (should_fix == 0) {
				qerrorf
				    ("Try running dbmail-util with the '-f' option "
				     "in order to fix these problems\n\n");
			}

			list_freelist(&lostlist.start);
		} else
			qprintf("Ok. Found 0 unconnected mailboxes.\n");

		time(&stop);
		qprintf("--- checking mailbox integrity took %g seconds\n",
		       difftime(stop, start));
		qprintf("--- checking mailbox integrity took %g seconds\n",
			difftime(stop, start));
	}

	if (check_iplog) {
		find_time(timestr, timespec);
		qprintf("Cleaning up IP log... ");

		if (timestr[0] == 0) {
			qerrorf("Failed. Invalid argument [%s] specified\n",
			       timespec);
			db_disconnect();
			auth_disconnect();
			return -1;
		}

		if (db_cleanup_iplog(timestr) < 0) {
			qerrorf("Failed. Please check the log.\n");
			db_disconnect();
			auth_disconnect();
			return -1;
		}

		qprintf("Ok. All entries before [%s] have been removed.\n",
		       timestr);
	}

	if (vacuum_db) {
		qprintf("Cleaning up database structure... ");
		fflush(stdout);
		if (db_cleanup() < 0) {
			qerrorf("Failed. Please check the log.\n");
			db_disconnect();
			auth_disconnect();
			return -1;
		}

		qprintf("Ok. Database cleaned up.\n");
	}

	qprintf("Maintenance done.\n\n");

	db_disconnect();
	auth_disconnect();
	
	config_free();

	return 0;
}

/* 
 * makes a date/time string: YYYY-MM-DD HH:mm:ss
 * based on current time minus timespec
 * timespec contains: <n>h<m>m for a timespan of n hours, m minutes
 * hours or minutes may be absent, not both
 *
 * upon error, timestr[0] = 0
 */
void find_time(char *timestr, const char *timespec)
{
	time_t td;
	struct tm tm;
	int min = -1, hour = -1;
	long tmp;
	char *end;

	time(&td);		/* get time */

	timestr[0] = 0;
	if (!timespec)
		return;

	/* find first num */
	tmp = strtol(timespec, &end, 10);
	if (!end)
		return;

	if (tmp < 0)
		return;

	switch (*end) {
	case 'h':
	case 'H':
		hour = tmp;
		break;

	case 'm':
	case 'M':
		hour = 0;
		min = tmp;
		if (end[1])	/* should end here */
			return;

		break;

	default:
		return;
	}


	/* find second num */
	if (timespec[end - timespec + 1]) {
		tmp = strtol(&timespec[end - timespec + 1], &end, 10);
		if (end) {
			if ((*end != 'm' && *end != 'M') || end[1])
				return;

			if (tmp < 0)
				return;

			if (min >= 0)	/* already specified minutes */
				return;

			min = tmp;
		}
	}

	if (min < 0)
		min = 0;

	/* adjust time */
	td -= (hour * 3600L + min * 60L);

	tm = *localtime(&td);	/* get components */
	strftime(timestr, LEN, "%Y-%m-%d %H:%M:%S", &tm);

	return;
}
