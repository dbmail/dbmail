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
 * This is the dbmail housekeeping program. 
 *	It checks the integrity of the database and does a cleanup of all
 *	deleted messages. 
 */

#include "dbmail.h"
#define THIS_MODULE "maintenance"
#define PNAME "dbmail/maintenance"

/* Loudness and assumptions. */
int yes_to_all = 0;
int no_to_all = 0;
int verbose = 0;
/* Don't be helpful. */
int quiet = 0;
/* Don't print errors. */
int reallyquiet = 0;

#define qverbosef(fmt, args...) (!verbose ? 0 : printf(fmt, ##args) )
#define qprintf(fmt, args...) ((quiet||reallyquiet) ? 0 : printf(fmt, ##args) )
#define qerrorf(fmt, args...) (reallyquiet ? 0 : fprintf(stderr, fmt, ##args) )

char *configFile = DEFAULT_CONFIG_FILE;

int has_errors = 0;
int serious_errors = 0;

/* set up database login data */
extern db_param_t _db_params;

static int find_time(const char *timespec, timestring_t *timestring);

static int do_check_integrity(void);
static int do_null_messages(void);
static int do_purge_deleted(void);
static int do_set_deleted(void);
static int do_dangling_aliases(void);
static int do_header_cache(void);
static int do_check_iplog(const char *timespec);
static int do_check_replycache(const char *timespec);
static int do_vacuum_db(void);

int do_showhelp(void) {
	printf("*** dbmail-util ***\n");

	printf(
//	Try to stay under the standard 80 column width
//	0........10........20........30........40........50........60........70........80
	"Use this program to maintain your DBMail database.\n"
	"See the man page for more info. Summary:\n\n"
	"     -a        perform all checks (in this release: -ctubpds)\n"
	"     -c        clean up database (optimize/vacuum)\n"
	"     -t        test for message integrity\n"
	"     -u        null message check\n"
	"     -b        body/header/envelope cache check\n"
	"     -p        purge messages have the DELETE status set\n"
	"     -d        set DELETE status for deleted messages\n"
	"     -s        remove dangling/invalid aliases and forwards\n"
	"     -r time   clear the replycache used for autoreply/vacation\n"
	"     -l time   clear the IP log used for IMAP/POP-before-SMTP\n"
	"               the time syntax is [<hours>h][<minutes>m]\n"
	"               valid examples: 72h, 4h5m, 10m\n"
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

	return 0;
}

int main(int argc, char *argv[])
{
	int check_integrity = 0;
	int check_iplog = 0, check_replycache = 0;
	char *timespec_iplog = NULL, *timespec_replycache = NULL;
	int null_messages = 0;
	int vacuum_db = 0, purge_deleted = 0, set_deleted = 0, dangling_aliases = 0;
	int show_help = 0;
	int do_nothing = 1;
	int is_header = 0;
	int opt;

	g_mime_init(0);
	openlog(PNAME, LOG_PID, LOG_MAIL);
	setvbuf(stdout, 0, _IONBF, 0);

	/* get options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt(argc, argv, "-acbtl:r:puds" "i" "f:qnyvVh")) != -1) {
		/* The initial "-" of optstring allows unaccompanied
		 * options and reports them as the optarg to opt 1 (not '1') */
		switch (opt) {
		case 'a':
			/* This list should be kept up to date. */
			vacuum_db = 1;
			purge_deleted = 1;
			set_deleted = 1;
			dangling_aliases = 1;
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

		case 's':
			dangling_aliases = 1;
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
				timespec_iplog = g_strdup(optarg);
			break;

		case 'r':
			check_replycache = 1;
			do_nothing = 0;
			if (optarg)
				timespec_replycache = g_strdup(optarg);
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
			PRINTF_THIS_IS_DBMAIL;
			return 1;

		default:
			printf("unrecognized option [%c]\n", optopt); 
			show_help = 1;
			break;
		}
	}

	if (do_nothing || show_help || (no_to_all && yes_to_all)) {
		do_showhelp();
		return 1;
	}

 	/* Don't make any changes unless specifically authorized. */
 	if (!yes_to_all) {
		qprintf("Choosing dry-run mode. No changes will be made at this time.\n");
		no_to_all = 1;
 	}

	config_read(configFile);
	SetTraceLevel("DBMAIL");
	GetDBParams(&_db_params);

	qverbosef("Opening connection to database... \n");
	if (db_connect() != 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		return -1;
	}

	qverbosef("Opening connection to authentication... \n");
	if (auth_connect() != 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		return -1;
	}

	qverbosef("Ok. Connected.\n");

	if (check_integrity) do_check_integrity();
	if (null_messages) do_null_messages();
	if (purge_deleted) do_purge_deleted();
	if (is_header) do_header_cache();
	if (set_deleted) do_set_deleted();
	if (dangling_aliases) do_dangling_aliases();
	if (check_iplog) do_check_iplog(timespec_iplog);
	if (check_replycache) do_check_replycache(timespec_replycache);
	if (vacuum_db) do_vacuum_db();

	if (!has_errors && !serious_errors) {
		qprintf("\nMaintenance done. No errors found.\n");
	} else {
		qerrorf("\nMaintenance done. Errors were found");
		if (serious_errors) {
			qerrorf(" but not fixed due to failures.\n");
			qerrorf("Please check the logs for further details, "
				"turning up the trace level as needed.\n");
			// Indicate that something went really wrong
			has_errors = 3;
		} else if (no_to_all) {
			qerrorf(" but not fixed.\n");
			qerrorf("Run again with the '-y' option to "
				"repair the errors.\n");
			// Indicate that the program should be run with -y
			has_errors = 2;
		} else if (yes_to_all) {
			qerrorf(" and fixed.\n");
			qerrorf("We suggest running dbmail-util again to "
				"confirm that all errors were repaired.\n");
			// Indicate that the program should be run again
			has_errors = 1;
		}
	}

	auth_disconnect();
	db_disconnect();
	config_free();
	g_mime_shutdown();
	
	return has_errors;
}

int do_purge_deleted(void)
{
	u64_t deleted_messages;

	if (no_to_all) {
		qprintf("\nCounting messages with DELETE status...\n");
		if (db_deleted_count(&deleted_messages) < 0) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
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
			serious_errors = 1;
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
		if (db_count_deleted(&messages_set_to_delete) == -1) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%llu] messages need to be set for deletion.\n",
		       messages_set_to_delete);
	}
	if (yes_to_all) {
		qprintf("\nSetting DELETE status for deleted messages...\n");
		if (db_set_deleted(&messages_set_to_delete) == -1) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%llu] messages set for deletion.\n",
		       messages_set_to_delete);
		qprintf("Re-calculating used quota for all users...\n");
		if (db_calculate_quotum_all() < 0) {
			qerrorf
			    ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. Used quota updated for all users.\n");
	}
	return 0;
}

GList *find_dangling_aliases(const char * const name)
{
	int result;
	char *username;
	struct dm_list uids;
	struct dm_list fwds;
	GList *userids = NULL;
	GList *dangling = NULL;

	/* For each alias, figure out if it resolves to a valid user
	 * or some forwarding address. If neither, remove it. */

	dm_list_init(&fwds);
	dm_list_init(&uids);
	result = auth_check_user_ext(name,&uids,&fwds,0);
	
	if (!result) {
		qerrorf("Nothing found searching for [%s].\n", name);
		serious_errors = 1;
		return dangling;
	}

	if (dm_list_getstart(&uids))
		userids = g_list_copy_list(userids,dm_list_getstart(&uids));

	userids = g_list_first(userids);
	while (userids) {
		username = auth_get_userid(*(u64_t *)userids->data);
		if (!username) {
			dangling = g_list_prepend(dangling, userids->data);
		}
		g_free(username);
		if (! g_list_next(userids))
			break;
		userids = g_list_next(userids);
	}

	return dangling;
}

int do_dangling_aliases(void)
{
	int count = 0;
	int result = 0;
	GList *aliases = NULL;

	if (no_to_all)
		qprintf("\nCounting aliases with nonexistent delivery userid's...\n");
	if (yes_to_all)
		qprintf("\nRemoving aliases with nonexistent delivery userid's...\n");

	aliases = auth_get_known_aliases();
	aliases = g_list_dedup(aliases);
	aliases = g_list_first(aliases);
	while (aliases) {
		char deliver_to[21];
		GList *dangling = find_dangling_aliases(aliases->data);

		dangling = g_list_first(dangling);
		while (dangling) {
			count++;
			g_snprintf(deliver_to, 21, "%llu", *(u64_t *)dangling->data);
			qverbosef("Dangling alias [%s] delivers to nonexistent user [%s]\n",
				(char *)aliases->data, deliver_to);
			if (yes_to_all) {
				if (auth_removealias_ext(aliases->data, deliver_to) < 0) {
					qerrorf("Error: could not remove alias [%s] deliver to [%s] \n",
						(char *)aliases->data, deliver_to);
					serious_errors = 1;
					result = -1;
				}
			}
			if (!g_list_next(dangling))
				break;
			dangling = g_list_next(dangling);
		}
		g_list_destroy(g_list_first(dangling));

		if (! g_list_next(aliases))
			break;
		aliases = g_list_next(aliases);
	}
	g_list_destroy(g_list_first(aliases));

	if (count > 0) {
		qerrorf("Ok. Found [%d] dangling aliases.\n", count);
		has_errors = 1;
	} else {
		qprintf("Ok. Found [%d] dangling aliases.\n", count);
	}

	return result;
}

int do_null_messages(void)
{
	time_t start, stop;
	GList *lost = NULL;
	u64_t *id;
	long count;

	if (no_to_all)
		qprintf("\nChecking DBMAIL for NULL messages...\n");
	if (yes_to_all)
		qprintf("\nRepairing DBMAIL for NULL messages...\n");

	time(&start);

	if (db_icheck_null_messages(&lost) < 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		g_list_destroy(lost);
		serious_errors = 1;
		return -1;
	}

	count = g_list_length(lost);
	if (count > 0)
		qerrorf("Ok. Found [%ld] NULL messages.\n", count);
	else
		qprintf("Ok. Found [%ld] NULL messages.\n", count);

	if (yes_to_all) {
		if (count > 0) {
			lost = g_list_first(lost);
			while (lost) {
				id = (u64_t *)lost->data;
				if (db_set_message_status(*id, MESSAGE_STATUS_ERROR) < 0)
					qerrorf("Warning: could not set status on message [%llu]. Check log.\n", *id);
				else
					qverbosef("[%llu] set to MESSAGE_STATUS_ERROR)\n", *id);
        
				if (! g_list_next(lost))
					break;
				lost = g_list_next(lost);
			}
		}
	}

	g_list_destroy(lost);
	lost = NULL;

	time(&stop);
	qverbosef("--- checking NULL messages took %g seconds\n", difftime(stop, start));
	qprintf("\nChecking DBMAIL for NULL physmessages...\n");
	time(&start);
	if (db_icheck_null_physmessages(&lost) < 0) {
		qerrorf("Failed, an error occured. Please check log.\n");
		serious_errors = 1;
		return -1;
	}

	count = g_list_length(lost);

	if (count > 0) {
		qerrorf("Ok. Found [%ld] physmessages without messageblocks.\n", count);

		lost = g_list_first(lost);
		while (lost) {
			id = (u64_t *) lost->data;

			if (db_delete_physmessage(*id) < 0)
				qerrorf("Warning: could not delete physmessage [%llu]. Check log.\n", *id);
			else
				qverbosef("[%llu] deleted.\n", *id);

			if (! g_list_next(lost))
				break;
			lost = g_list_next(lost);
		}

	} else {
		qprintf("Ok. Found [%ld] physmessages without messageblocks.\n", count);
	}

	g_list_destroy(lost);

	time(&stop);
	qverbosef("--- checking NULL physmessages took %g seconds\n", difftime(stop, start));
	
	return 0;
}

int do_check_integrity(void)
{
	time_t start, stop;
	GList *lost;
	const char *action;
	long count = 0;
	u64_t *id;

	if (yes_to_all)
		action = "Repairing";
	else
		action = "Checking";

	qprintf("\n%s DBMAIL messageblocks integrity...\n", action);

	time(&start);

	/* This is what we do:
	 1. Check for loose messageblks
	 2. Check for loose physmessages
	 3. Check for loose messages
	 4. Check for loose mailboxes
	 */

	/* first part */
	if (db_icheck_messageblks(&lost) < 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		serious_errors = 1;
		return -1;
	}

	count = g_list_length(lost);

	if (count > 0) {
		qerrorf("Ok. Found [%ld] unconnected messageblks:\n", count);

		lost = g_list_first(lost);

		while (lost) {
			id = (u64_t *) lost->data;
			if (no_to_all) {
				qerrorf("%llu ", *id);
			} else if (yes_to_all) {
				if (db_delete_messageblk(*id) < 0)
					qerrorf ("Warning: could not delete messageblock #%llu. Check log.\n", *id);
				else
					qerrorf ("%llu (removed from dbase)\n", *id);
			}

			if (! g_list_next(lost))
				break;

			lost = g_list_next(lost);
		}

		qerrorf("\n");
		has_errors = 1;
	} else {
		qprintf("Ok. Found [%ld] unconnected messageblks.\n", count);
	}

	g_list_destroy(lost);
	lost = NULL;

	time(&stop);
	qverbosef("--- %s block integrity took %g seconds\n", action, difftime(stop, start));

	/* second part */
	start = stop;
	qprintf("\n%s DBMAIL physmessage integrity...\n", action);
	if ((count = db_icheck_physmessages(FALSE)) < 0) {
		qerrorf("Failed. An error occurred. Please check log.\n");
		serious_errors = 1;
		return -1;
	}
	if (count > 0) {
		qerrorf("Ok. Found [%ld] unconnected physmessages", count);
		if (yes_to_all) {
			if (db_icheck_physmessages(TRUE) < 0)
				qerrorf("Warning: could not delete orphaned physmessages. Check log.\n");
			else
				qerrorf("Ok. Orphaned physmessages deleted.\n");
		}
	} else {
		qprintf("Ok. Found [%ld] unconnected physmessages.\n", count);
	}

	time(&stop);
	qverbosef("--- %s unconnected physmessages took %g seconds\n",
		action, difftime(stop, start));

	/* third part */
	start = stop;
	qprintf("\n%s DBMAIL message integrity...\n", action);

	if (db_icheck_messages(&lost) < 0) {
		qerrorf ("Failed. An error occured. Please check log.\n");
		serious_errors = 1;
		return -1;
	}

	count = g_list_length(lost);

	if (count > 0) {
		has_errors = 1;
		qerrorf("Ok. Found [%ld] unconnected messages:\n", count);
	} else {
		qprintf("Ok. Found [%ld] unconnected messages.\n", count);
	}

	if (yes_to_all) {
		if (count > 0) {
			lost = g_list_first(lost);
			
			while (lost) {
				id = (u64_t *) lost->data;
        
				if (no_to_all) {
					qerrorf("%llu ", *id);
				} else if (yes_to_all) {
					if (db_delete_message(*id) < 0)
						qerrorf ("Warning: could not delete message #%llu. Check log.\n", *id);
					else
						qerrorf ("%llu (removed from dbase)\n", *id);
				}
        
				if (! g_list_next(lost))
					break;
				lost = g_list_next(lost);
			}
			qerrorf("\n");
		}
	}
	g_list_destroy(lost);
	lost = NULL;

	time(&stop);
	qverbosef("--- %s message integrity took %g seconds\n",
	       action, difftime(stop, start));

	/* fourth part */
	qprintf("\n%s DBMAIL mailbox integrity...\n", action);
	start = stop;

	if (db_icheck_mailboxes(&lost) < 0) {
		qerrorf ("Failed. An error occured. Please check log.\n");
		serious_errors = 1;
		return -1;
	}

	count = g_list_length(lost);
	if (count > 0) {
		has_errors = 1;
		qerrorf("Ok. Found [%ld] unconnected mailboxes.\n", count);
	} else {
		qprintf("Ok. Found [%ld] unconnected mailboxes.\n", count);
	}

	if (yes_to_all) {
		if (count > 0) {
        
			lost = g_list_first(lost);
			while (lost) {
				id = (u64_t *) lost->data;
        
				if (no_to_all) {
					qerrorf("%llu ", *id);
				} else if (yes_to_all) {
					if (db_delete_mailbox(*id, 0, 0))
						qerrorf("Warning: could not delete mailbox #%llu. Check log.\n", *id);
					else
						qerrorf("%llu (removed from dbase)\n", *id);
				}
				if (! g_list_next(lost))
					break;
				lost = g_list_next(lost);
			}
			qerrorf("\n");
		}
	}

	g_list_destroy(lost);

	time(&stop);
	qverbosef("--- %s mailbox integrity took %g seconds\n",
	       action, difftime(stop, start));
	
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
		serious_errors = 1;
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

	g_list_free(g_list_first(lost));

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
		serious_errors = 1;
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
			qerrorf("Error setting the rfcsize values");
			has_errors = 1;
		}
	}

	g_list_destroy(lost);

	time(&stop);
	qverbosef("--- checking rfcsize field took %g seconds\n",
	       difftime(stop, start));
	
	return 0;

}

static int do_envelope(void)
{
	time_t start, stop;
	GList *lost = NULL;

	if (no_to_all) {
		qprintf("\nChecking DBMAIL for cached envelopes...\n");
	}
	if (yes_to_all) {
		qprintf("\nRepairing DBMAIL for cached envelopes...\n");
	}
	time(&start);

	if (db_icheck_envelope(&lost) < 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		serious_errors = 1;
		return -1;
	}

	if (g_list_length(lost) > 0) {
		qerrorf("Ok. Found [%d] missing envelope values.\n", g_list_length(lost));
		has_errors = 1;
	} else {
		qprintf("Ok. Found [%d] missing envelope values.\n", g_list_length(lost));
	}

	if (yes_to_all) {
		if (db_set_envelope(lost) < 0) {
			qerrorf("Error setting the envelope cache");
			has_errors = 1;
		}
	}

	g_list_free(g_list_first(lost));

	time(&stop);
	qverbosef("--- checking envelope cache took %g seconds\n",
	       difftime(stop, start));
	
	return 0;

}


int do_header_cache(void)
{
	time_t start, stop;
	GList *lost = NULL;
	
	if (do_rfc_size()) {
		serious_errors = 1;
		return -1;
	}

	if (do_is_header()) {
		serious_errors = 1;
		return -1;
	}
	
	if (do_envelope()) {
		serious_errors = 1;
		return -1;
	}
	
	if (no_to_all) 
		qprintf("\nChecking DBMAIL for cached header values...\n");
	if (yes_to_all) 
		qprintf("\nRepairing DBMAIL for cached header values...\n");
	
	time(&start);

	if (db_icheck_headercache(&lost) < 0) {
		qerrorf("Failed. An error occured. Please check log.\n");
		serious_errors = 1;
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
			serious_errors = 1;
		}
	}

	g_list_free(lost);

	time(&stop);
	qverbosef("--- checking cached headervalues took %g seconds\n",
	       difftime(stop, start));

	return 0;
}


int do_check_iplog(const char *timespec)
{
	u64_t log_count;
	timestring_t timestring;

	if (find_time(timespec, &timestring) != 0) {
		qerrorf("\nFailed to find a timestring: [%s] is not <hours>h<minutes>m.\n",
		       timespec);
		serious_errors = 1;
		return -1;
	}

	if (no_to_all) {
		qprintf("\nCounting IP entries older than [%s]...\n", timestring);
		if (db_count_iplog(timestring, &log_count) < 0) {
			qerrorf("Failed. An error occured. Check the log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%llu] IP entries are older than [%s].\n",
		    log_count, timestring);
	}
	if (yes_to_all) {
		qprintf("\nRemoving IP entries older than [%s]...\n", timestring);
		if (db_cleanup_iplog(timestring, &log_count) < 0) {
			qerrorf("Failed. Please check the log.\n");
			serious_errors = 1;
			return -1;
		}

		qprintf("Ok. [%llu] IP entries were older than [%s].\n",
		       log_count, timestring);
	}
	return 0;
}

int do_check_replycache(const char *timespec)
{
	u64_t log_count;
	timestring_t timestring;

	if (find_time(timespec, &timestring) != 0) {
		qerrorf("\nFailed to find a timestring: [%s] is not <hours>h<minutes>m.\n",
		       timespec);
		serious_errors = 1;
		return -1;
	}

	if (no_to_all) {
		qprintf("\nCounting RC entries older than [%s]...\n", timestring);
		if (db_count_replycache(timestring, &log_count) < 0) {
			qerrorf("Failed. An error occured. Check the log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%llu] RC entries are older than [%s].\n",
		    log_count, timestring);
	}
	if (yes_to_all) {
		qprintf("\nRemoving RC entries older than [%s]...\n", timestring);
		if (db_cleanup_replycache(timestring, &log_count) < 0) {
			qerrorf("Failed. Please check the log.\n");
			serious_errors = 1;
			return -1;
		}

		qprintf("Ok. [%llu] RC entries were older than [%s].\n",
		       log_count, timestring);
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
			serious_errors = 1;
			return -1;
		}

		qprintf("Ok. Database cleaned up.\n");
	}
	return 0;
}

/* Makes a date/time string: YYYY-MM-DD HH:mm:ss
 * based on current time minus timespec
 * timespec contains: <n>h<m>m for a timespan of n hours, m minutes
 * hours or minutes may be absent, not both
 *
 * Returns NULL on error.
 */
int find_time(const char *timespec, timestring_t *timestring)
{
	time_t td;
	struct tm tm;
	int min = -1, hour = -1;
	long tmp;
	char *end = NULL;

	time(&td);		/* get time */

	if (!timespec) {
		serious_errors = 1;
		return -1;
	}

	/* find first num */
	tmp = strtol(timespec, &end, 10);
	if (!end) {
		serious_errors = 1;
		return -1;
	}

	if (tmp < 0) {
		serious_errors = 1;
		return -1;
	}

	switch (*end) {
	case 'h':
	case 'H':
		hour = tmp;
		break;

	case 'm':
	case 'M':
		hour = 0;
		min = tmp;
		if (end[1]) {	/* should end here */
			serious_errors = 1;
			return -1;
		}

		break;

	default:
		serious_errors = 1;
		return -1;
	}


	/* find second num */
	if (timespec[end - timespec + 1]) {
		tmp = strtol(&timespec[end - timespec + 1], &end, 10);
		if (end) {
			if ((*end != 'm' && *end != 'M') || end[1]) {
				serious_errors = 1;
				return -1;
			}

			if (tmp < 0) {
				serious_errors = 1;
				return -1;
			}

			if (min >= 0) {	/* already specified minutes */
				serious_errors = 1;
				return -1;
			}

			min = tmp;
		}
	}

	if (min < 0)
		min = 0;

	/* adjust time */
	td -= (hour * 3600L + min * 60L);

	tm = *localtime(&td);	/* get components */
	strftime((char *) timestring, sizeof(timestring_t),
		 "%Y-%m-%d %H:%M:%S", &tm);

	return 0;
}

