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

/* $Id: maintenance.c 1897 2005-10-11 11:59:17Z paul $
 *
 * This is the dbmail housekeeping program. 
 *	It checks the integrity of the database and does a cleanup of all
 *	deleted messages. 
 */

#include "dbmail.h"

#define LEN 30
#define PNAME "dbmail/maintenance"


/* Loudness and assumptions. */
int yes_to_all = 0;
int no_to_all = 0;
int verbose = 0;
/* Don't be helpful. */
int quiet = 0;
/* Don't print errors. */
int reallyquiet = 0;

#define qverbosef(fmt, args...) \
	(!verbose ? 0 : printf(fmt, ##args) )

#define qerrorf(fmt, args...) \
	(reallyquiet ? 0 : fprintf(stderr, fmt, ##args) )

char *configFile = DEFAULT_CONFIG_FILE;

int has_errors = 0;

/* set up database login data */
extern db_param_t _db_params;

static void find_time(char *timestr, const char *timespec);

static int do_check_integrity(void);
static int do_null_messages(void);
static int do_purge_deleted(void);
static int do_set_deleted(void);
static int do_header_cache(void);
static int do_check_iplog(char *timestr, const char *timespec);
static int do_vacuum_db(void);

int do_showhelp(void) {
	printf("*** dbmail-util ***\n");

	printf("Use this program to maintain your DBMail database.\n");
	printf("See the man page for more info. Summary:\n\n");
	printf("     -a        perform all checks (in this release: -ctupd)\n");
	printf("     -c        clean up database (optimize/vacuum)\n");
	printf("     -t        test for message integrity\n");
	printf("     -u        null message check\n");
	printf("     -b        body/header check\n");
	printf("     -p        purge messages have the DELETE status set\n");
	printf("     -d        set DELETE status for deleted messages\n");
	printf("     -l time   clear the IP log used for IMAP/POP-before-SMTP\n"
	       "               the time is specified as <hours>h<minutes>m\n"
	       "               (don't include the angle brackets, though)\n");
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
	int check_integrity = 0, check_iplog = 0;
	int null_messages = 0;
	int vacuum_db = 0, purge_deleted = 0, set_deleted = 0;
	int show_help = 0;
	int do_nothing = 1;
	int is_header = 0;
	int opt;
	char timespec[LEN];
	char timestr[LEN];

	g_mime_init(0);
	openlog(PNAME, LOG_PID, LOG_MAIL);
	setvbuf(stdout, 0, _IONBF, 0);

	/* get options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt(argc, argv, "-acbtl:pud" "i" "f:qnyvVh")) != -1) {
		/* The initial "-" of optstring allows unaccompanied
		 * options and reports them as the optarg to opt 1 (not '1') */
		switch (opt) {
		case 'a':
			/* This list should be kept up to date. */
			vacuum_db = 1;
			purge_deleted = 1;
			set_deleted = 1;
			check_integrity = 1;
			null_messages = 1;
			is_header = 1;
			do_nothing = 0;
			break;

		case 'c':
			vacuum_db = 1;
			do_nothing = 0;
			break;

		case 'b':
			is_header = 1;
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

		case 't':
			check_integrity = 1;
			do_nothing = 0;
			break;

		case 'u':
			null_messages = 1;
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
			no_to_all = 1;
			break;

		case 'r':
		case 'y':
			yes_to_all = 1;
			break;

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
 			printf("DBMail: dbmail-util\n"
 			       "Version: %s\n"
 			       "$Revision: 1897 $\n"
 			       "Copyright: %s\n", VERSION, COPYRIGHT);
			return 1;

		default:
			/*printf("unrecognized option [%c], continuing...\n",optopt); */
			break;
		}
	}

	if (do_nothing || show_help || (no_to_all && yes_to_all)) {
		do_showhelp();
		return 1;
	}

 	/* Don't make any changes unless specifically authorized. */
 	if (!yes_to_all) no_to_all = 1;
 
	config_read(configFile);
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

	qprintf("Ok. Connected.\n");

	if (check_integrity) do_check_integrity();
	if (null_messages) do_null_messages();
	if (purge_deleted) do_purge_deleted();
	if (is_header) do_header_cache();
	if (set_deleted) do_set_deleted();
	if (check_iplog) do_check_iplog(timestr, timespec);
	if (vacuum_db) do_vacuum_db();

	if (!has_errors) {
		qprintf("\nMaintenance done. No errors found.\n");
	} else {
		qerrorf("\nMaintenance done. Errors were found");
		if (no_to_all) {
			qerrorf(" but not fixed.\n");
			qerrorf("Try running dbmail-util with the '-y' "
			    "option to repair the errors.\n");
		}
		if (yes_to_all) {
			qerrorf(" and fixed.\n");
			qerrorf("Try running dbmail-util again to confirm "
			    "that the errors were repaired.\n");
		}
	}

	auth_disconnect();
	db_disconnect();
	config_free();
	g_mime_shutdown();
	
	return 0;
}

int do_purge_deleted(void)
{
	u64_t deleted_messages;

	if (no_to_all) {
		qprintf("\nCounting messages with DELETE status...\n");
		if (db_deleted_count(&deleted_messages) < 0) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			return -1;
		}
		qprintf("Ok. [%llu] messages have DELETE status.\n",
		    deleted_messages);
	}
	if (yes_to_all) {
		qprintf("\nDeleting messages with DELETE status...\n");
		if (db_deleted_purge(&deleted_messages) < 0) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			return -1;
		}
		qprintf("Ok. [%llu] messages deleted.\n", deleted_messages);
	}
	return 0;
}

int do_set_deleted(void)
{
	u64_t messages_set_to_delete;

	if (no_to_all) {
		// TODO: Count messages to delete.
		qprintf("\nCounting deleted messages that need the DELETE status set...\n");
	}
	if (yes_to_all) {
		qprintf("\nSetting DELETE status for deleted messages...\n");
		if (db_set_deleted(&messages_set_to_delete) == -1) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			return -1;
		}
		qprintf("Ok. [%llu] messages set for deletion.\n",
		       messages_set_to_delete);
		qprintf("Re-calculating used quota for all users...\n");
		if (db_calculate_quotum_all() < 0) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			return -1;
		}
		qprintf("Ok. Used quota updated for all users.\n");
	}
	return 0;
}

int do_null_messages(void)
{
	time_t start, stop;
	struct dm_list lostlist;
	struct element *el;
	u64_t id;

	if (no_to_all) {
		qprintf("\nChecking DBMAIL for NULL messages...\n");
	}
	if (yes_to_all) {
		qprintf("\nRepairing DBMAIL for NULL messages...\n");
	}
	time(&start);

	if (db_icheck_null_messages(&lostlist) < 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		return -1;
	}

	if (lostlist.total_nodes > 0) {
		qerrorf("Ok. Found [%ld] NULL messages.\n",
		       lostlist.total_nodes);
	} else {
		qprintf("Ok. Found [%ld] NULL messages.\n",
		       lostlist.total_nodes);
	}

	if (yes_to_all) {
		if (lostlist.total_nodes > 0) {
			el = lostlist.start;
			while (el) {
				id = *((u64_t *) el->data);
				if (db_set_message_status(id, MESSAGE_STATUS_ERROR) < 0)
					qerrorf("Warning: could not set status on message [%llu]. Check log.\n", id);
				else
					qverbosef("[%llu] set to MESSAGE_STATUS_ERROR)\n", id);
        
				el = el->nextnode;
			}
        
			dm_list_free(&lostlist.start);
		}
	}

	time(&stop);
	qverbosef("--- checking NULL messages took %g seconds\n",
	       difftime(stop, start));
	qprintf("\nChecking DBMAIL for NULL physmessages...\n");
	time(&start);
	if (db_icheck_null_physmessages(&lostlist) < 0) {
		qerrorf("Failed, an error occured. Please check log.\n");
		return -1;
	}

	if (lostlist.total_nodes > 0) {
		qerrorf("Ok. Found [%ld] physmessages without messageblocks.\n",
		     lostlist.total_nodes);

		el = lostlist.start;
		while (el) {
			id = *((u64_t *) el->data);
			if (db_delete_physmessage(id) < 0)
				qerrorf("Warning: could not delete physmessage [%llu]. Check log.\n", id);
			else
				qverbosef("[%llu] deleted.\n", id);
			el = el->nextnode;
		}
		dm_list_free(&lostlist.start);
	} else {
		qprintf("Ok. Found [%ld] physmessages without messageblocks.\n",
		    lostlist.total_nodes);
	}

	time(&stop);
	qverbosef("--- checking NULL physmessages took %g seconds\n",
		difftime(stop, start));
	
	return 0;
}

int do_check_integrity(void)
{
	time_t start, stop;
	struct dm_list lostlist;
	struct element *el;
	u64_t id;

	if (no_to_all) {
		qprintf("\nChecking DBMAIL messageblocks integrity...\n");
	}
	if (yes_to_all) {
		qprintf("\nRepairing DBMAIL messageblocks integrity...\n");
	}
	time(&start);

	/* this is what we do:
	 * First we're checking for loose messageblocks
	 * Secondly we're chekcing for loose messages
	 * Third we're checking for loose mailboxes 
	 */

	/* first part */
	if (db_icheck_messageblks(&lostlist) < 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		return -1;
	}

	if (lostlist.total_nodes > 0) {
		qerrorf("Ok. Found [%ld] unconnected messageblks:\n",
		     lostlist.total_nodes);

		el = lostlist.start;
		while (el) {
			id = *((u64_t *) el->data);
			if (no_to_all) {
				qerrorf("%llu ", id);
			} else if (yes_to_all) {
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

		dm_list_free(&lostlist.start);

		qerrorf("\n");
		has_errors = 1;
	} else {
		qprintf("Ok. Found [%ld] unconnected messageblks.\n",
		     lostlist.total_nodes);
	}


	time(&stop);
	qverbosef("--- checking block integrity took %g seconds\n",
	       difftime(stop, start));
	qverbosef("--- checking block integrity took %g seconds\n",
		difftime(stop, start));

	/* second part */
	start = stop;
	qprintf("\nChecking DBMAIL message integrity...\n");

	if (db_icheck_messages(&lostlist) < 0) {
		qerrorf
		    ("Failed. An error occured. Please check log.\n");
		return -1;
	}

	if (lostlist.total_nodes > 0) {
		has_errors = 1;
		qerrorf("Ok. Found [%ld] unconnected messages:\n",
		       lostlist.total_nodes);
	} else {
		qprintf("Ok. Found [%ld] unconnected messages.\n",
		       lostlist.total_nodes);
	}

	if (yes_to_all) {
		if (lostlist.total_nodes > 0) {
			el = lostlist.start;
			while (el) {
				id = *((u64_t *) el->data);
        
				if (no_to_all) {
					qerrorf("%llu ", id);
				} else if (yes_to_all) {
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
			dm_list_free(&lostlist.start);
        
		}
	}

	time(&stop);
	qverbosef("--- checking message integrity took %g seconds\n",
	       difftime(stop, start));
	qverbosef("--- checking message integrity took %g seconds\n",
		difftime(stop, start));

	/* third part */
	qprintf("\nChecking DBMAIL mailbox integrity...\n");
	start = stop;

	if (db_icheck_mailboxes(&lostlist) < 0) {
		qerrorf
		    ("Failed. An error occured. Please check log.\n");
		return -1;
	}

	if (lostlist.total_nodes > 0) {
		has_errors = 1;
		qerrorf("Ok. Found [%ld] unconnected mailboxes.\n",
		    lostlist.total_nodes);
	} else {
		qprintf("Ok. Found [%ld] unconnected mailboxes.\n",
		    lostlist.total_nodes);
	}

	if (yes_to_all) {
		if (lostlist.total_nodes > 0) {
        
			el = lostlist.start;
			while (el) {
				id = *((u64_t *) el->data);
        
				if (no_to_all) {
					qerrorf("%llu ", id);
				} else if (yes_to_all) {
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
			dm_list_free(&lostlist.start);
		}
	}

	time(&stop);
	qverbosef("--- checking mailbox integrity took %g seconds\n",
	       difftime(stop, start));
	qverbosef("--- checking mailbox integrity took %g seconds\n",
		difftime(stop, start));
	
	return 0;
}

static int do_is_header(void)
{
	time_t start, stop;
	GList *lost = NULL;

	if (no_to_all) {
		qprintf("\nChecking DBMAIL for incorrect is_header flags...\n");
	}
	if (yes_to_all) {
		qprintf("\nRepairing DBMAIL for incorrect is_header flags...\n");
	}
	time(&start);

	if (db_icheck_isheader(&lost) < 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		return -1;
	}

	if (g_list_length(lost) > 0) {
		qerrorf("Ok. Found [%d] incorrect is_header flags.\n", g_list_length(lost));
		has_errors = 1;
	} else {
		qprintf("Ok. Found [%d] incorrect is_header flags.\n", g_list_length(lost));
	}

	if (yes_to_all) {
		if (db_set_isheader(lost) < 0) {
			qerrorf("Error setting the is_header flags");
			has_errors = 1;
		}
	}

	g_list_free(lost);

	time(&stop);
	qverbosef("--- checking is_header flags took %g seconds\n",
	       difftime(stop, start));
	
	return 0;
}

static int do_rfc_size(void)
{
	time_t start, stop;
	GList *lost = NULL;

	if (no_to_all) {
		qprintf("\nChecking DBMAIL for rfcsize field...\n");
	}
	if (yes_to_all) {
		qprintf("\nRepairing DBMAIL for rfcsize field...\n");
	}
	time(&start);

	if (db_icheck_rfcsize(&lost) < 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		return -1;
	}

	if (g_list_length(lost) > 0) {
		qerrorf("Ok. Found [%d] missing rfcsize values.\n", g_list_length(lost));
		has_errors = 1;
	} else {
		qprintf("Ok. Found [%d] missing rfcsize values.\n", g_list_length(lost));
	}

	if (yes_to_all) {
		if (db_update_rfcsize(lost) < 0) {
			qerrorf("Error setting the is_header flags");
			has_errors = 1;
		}
	}

	g_list_free(lost);

	time(&stop);
	qverbosef("--- checking rfcsize field took %g seconds\n",
	       difftime(stop, start));
	
	return 0;

}

int do_header_cache(void)
{
	time_t start, stop;
	GList *lost = NULL;
	
	if (do_is_header())
		return -1;
	
	if (do_rfc_size())
		return -1;
	
	if (no_to_all) 
		qprintf("\nChecking DBMAIL for cached header values...\n");
	if (yes_to_all) 
		qprintf("\nRepairing DBMAIL for cached header values...\n");
	
	time(&start);

	if (db_icheck_headercache(&lost) < 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		return -1;
	}

	if (g_list_length(lost) > 0) {
		qerrorf("Ok. Found [%d] un-cached physmessages.\n", g_list_length(lost));
		has_errors = 1;
	} else {
		qprintf("Ok. Found [%d] un-cached physmessages.\n", g_list_length(lost));
	}

	if (yes_to_all) {
		if (db_set_headercache(lost) < 0) {
			qerrorf("Error caching the header values ");
			has_errors = 1;
		}
	}

	g_list_free(lost);

	time(&stop);
	qverbosef("--- checking cached headervalues took %g seconds\n",
	       difftime(stop, start));

	return 0;
}


int do_check_iplog(char *timestr, const char *timespec)
{
	u64_t log_count;

	find_time(timestr, timespec);
	if (timestr[0] == 0) {
		qerrorf("\nFailed to find a timestring: [%s] is not <hours>h<minutes>m.\n",
		       timespec);
		return -1;
	}

	if (no_to_all) {
		qprintf("\nCounting IP entries older than [%s]...\n", timestr);
		if (db_count_iplog(timestr, &log_count) < 0) {
			qerrorf("Failed. An error occured. Check the log.\n");
			return -1;
		}
		qprintf("Ok. [%llu] IP entries are older than [%s].\n",
		    log_count, timestr);
	}
	if (yes_to_all) {
		qprintf("\nRemoving IP entries older than [%s]...\n", timestr);
		if (db_cleanup_iplog(timestr, &log_count) < 0) {
			qerrorf("Failed. Please check the log.\n");
			return -1;
		}

		qprintf("Ok. [%llu] IP entries were older than [%s].\n",
		       log_count, timestr);
	}
	return 0;
}

int do_vacuum_db(void)
{
	if (no_to_all) {
		qprintf("\nVacuum and optimize not performed.\n");
	}
	if (yes_to_all) {
		qprintf("\nVacuuming and optimizing database...\n");
		fflush(stdout);
		if (db_cleanup() < 0) {
			qerrorf("Failed. Please check the log.\n");
			return -1;
		}

		qprintf("Ok. Database cleaned up.\n");
	}
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

