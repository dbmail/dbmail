/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2025 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

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
#define THIS_MODULE "util"
#define PNAME "dbmail/util"

extern DBParam_T db_params;
#define DBPFX db_params.pfx

/** list of tables used in dbmail, it is a duplicate found in dm_db.c*/
#define DB_NTABLES 24
const char *DB_TABLENAMES[DB_NTABLES] = {
	"acl",
	"aliases",
	"authlog",
	"auto_notifications",
	"auto_replies",
	"envelope",
	"filters",
	"header",
	"headername",
	"headervalue",
	"keywords",
	"mailboxes",
	"messages",
	"mimeparts",
	"partlists",
	"pbsp",
	"physmessage",
	"referencesfield",
	"replycache",
	"sievescripts",
	"subscription",
	"upgrade_steps",
	"usermap",
	"users"
};

/* Loudness and assumptions. */
int yes_to_all = 0;
int no_to_all = 0;
int verbose = 0;
/* Don't be helpful. */
int quiet = 0;
/* Don't print errors. */
int reallyquiet = 0;

extern char configFile[PATH_MAX];

int has_errors = 0;
int serious_errors = 0;

static int find_time(const char *timespec, TimeString_T *timestring);
static int do_move_old(int days, char * mbinbox_name, char * mbtrash_name);
static int do_erase_old(int days, char * mbtrash_name);
static int do_check_integrity(void);
static int do_purge_deleted(void);
static int do_set_deleted(void);
static int do_dangling_aliases(void);
static int do_header_cache(void);
static int do_check_iplog(const char *timespec);
static int do_check_replycache(const char *timespec);
static int do_vacuum_db(void);
static int do_rehash(void);
static int do_migrate(int migrate_limit);
static int do_check_empty_envelope(void);

int do_showhelp(void) {
	printf("*** dbmail-util ***\n");

	printf(
//	Try to stay under the standard 80 column width
//	0........10........20........30........40........50........60........70........80
	"Use this program to maintain your DBMail database.\n"
	"See the man page for more info. Summary:\n\n"
	"     -a, --all-checks         perform all checks\n"
	"                              (--check-body --set-deleted --purge-deleted\n"
	"                              --remove-invalid-aliases --test-integrity)\n"
	"     -c, --clean-database     clean up database (optimize/vacuum)\n"
	"     -t, --test-integrity     test for message integrity\n"
	"     -b, --check-body         body/header/envelope cache check\n"
	"     -e, --check-empty-cache  empty envelope cache check\n"
	"     -p, --purge-deleted      purge messages have the DELETE status set\n"
	"     -d, --set-deleted        set DELETE status for deleted messages\n"
	"     -s, --remove-invalid-aliases\n"
	"                              remove dangling/invalid aliases and forwards\n"
	"     -r, --clear-replycache time   clear the replycache used for autoreply/vacation\n"
	"     -l, --clear-iplog time   clear the IP log used for IMAP/POP-before-SMTP\n"
	"                              the time syntax is [<hours>h][<minutes>m]\n"
	"                              valid examples: 72h, 4h5m, 10m\n"
	"     -M, --migrate-legacy     migrate legacy 2.2.x messageblks to mimeparts table\n"
	"     -m, --migrate-limit limit\n"
	"                              limit migration to [limit] number of\n"
	"                              physmessages. Default 10000 per run\n"
	"     --rehash                 Rebuild hash keys for stored messages\n"
	"     --erase days             Delete messages older than date in INBOX/Trash \n"
	"     --move  days             Move messages from INBOX to INBOX/Trash\n"
	"     --inbox name             Inbox folder to move from, used in conjunction with --move\n"
	"     --trash name             Trash folder to move to, used in conjunction with --move\n"
	"\nCommon options for all DBMail utilities:\n"
	"     -f, --config file  specify an alternative config file\n"
	"               Default: %s\n"
	"     -q, --quiet        quietly skip interactive prompts\n"
	"                        use twice to suppress error messages\n"
	"     -n, --no           show the intended action but do not perform it,\n"
	"                        no to all\n"
	"     -y, --yes          perform all proposed actions, yes to all\n"
	"     -v, --verbose      verbose details\n"
	"     -V, --version      show the version\n"
	"     -h, --help         show this help message\n"
	, configFile);

	return 0;
}

int main(int argc, char *argv[])
{
	int check_integrity = 0;
	int check_iplog = 0, check_replycache = 0;
	int check_empty_envelope = 0;
	char *timespec_iplog = NULL, *timespec_replycache = NULL;
	int vacuum_db = 0, purge_deleted = 0, set_deleted = 0, dangling_aliases = 0, rehash = 0, move_old = 0, erase_old = 0;
	int show_help = 0;
	int do_nothing = 1;
	int is_header = 0;
	int migrate = 0, migrate_limit = 10000;
	static struct option long_options[] = {
		{"all-checks", no_argument, NULL, 'a'},
		{"clean-database", no_argument, NULL, 'c'},
		{"test-integrity", no_argument, NULL, 't'},
		{"check-body", no_argument, NULL, 'b'},
		{"check-empty-cache", no_argument, NULL, 'e'},
		{"purge-deleted", no_argument, NULL, 'p'},
		{"set-deleted", no_argument, NULL, 'd'},
		{"remove-invalid-aliases", no_argument, NULL, 's'},
		{"clear-replycache", required_argument, NULL, 'r'},
		{"clear-iplog", required_argument, NULL, 't'},
		{"migrate-legacy", no_argument, NULL, 'M'},
		{"migrate-limit", required_argument, 0, 'm'},
		{"rehash", no_argument, NULL, 0},
		{"move", required_argument, NULL, 0},
		{"erase", required_argument, NULL, 0},
		{"trash", required_argument, NULL, 0},
		{"inbox", required_argument, NULL, 0},
		{"config",    required_argument, NULL, 'f'},
		{"quiet",     no_argument, NULL, 'q'},
		{"no",        no_argument, NULL, 'n'},
		{"yes",       no_argument, NULL, 'y'},
		{"help",      no_argument, NULL, 'h'},
		{"verbose",   no_argument, NULL, 'v'},
		{"version",   no_argument, NULL, 'V'},
		{0, 0, 0, 0}
	};
	int opt_index = 0;
	int opt;
	int days_move = 0 , days_erase = 0;
	char * mbtrash_name;
	char * mbinbox_name;

	g_mime_init();
	
	config_get_file();

	openlog(PNAME, LOG_PID, LOG_MAIL);
	setvbuf(stdout, 0, _IONBF, 0);

	/* get options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt_long(argc, argv, "-abcegtl:r:pudsMm:" "i" "f:qnyvVh", long_options, &opt_index)) != -1) {
		/* The initial "-" of optstring allows unaccompanied
		 * options and reports them as the optarg to opt 1 (not '1') */
		switch (opt) {
		case 0:
			do_nothing = 0;
			if (strcmp(long_options[opt_index].name,"rehash")==0)
				rehash = 1;

			if (strcmp(long_options[opt_index].name,"move")==0) {
				move_old = 1;
				days_move = atoi(optarg);
			}
			if (strcmp(long_options[opt_index].name,"erase")==0) {
				erase_old = 1;
				purge_deleted = 1;
				days_erase = atoi(optarg);
			}

			if (strcmp(long_options[opt_index].name,"trash")==0) {
				mbtrash_name = optarg;
			}

			if (strcmp(long_options[opt_index].name,"inbox")==0) {
				mbinbox_name = optarg;
			}
			break;
		case 'a':
			/* This list should be kept up to date. */
			vacuum_db = 1;
			purge_deleted = 1;
			set_deleted = 1;
			dangling_aliases = 1;
			check_integrity = 1;
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

		case 'e':
			check_empty_envelope = 1;
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
			/* deprecated */
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

		case 'M':
			migrate = 1;
			do_nothing = 0;
			break;
		case 'm':
			if (optarg)
				migrate_limit = atoi(optarg);
			break;

		case 'i':
			qprintf("Interactive console is not supported in this release.\n");
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
			if (optarg && strlen(optarg) > 0) {
				memset(configFile, 0, sizeof(configFile));
				strncpy(configFile, optarg, sizeof(configFile)-1);
			} else {
				qprintf("dbmail-util: -f requires a filename\n\n" );
				return 1;
			}
			break;

		case 'v':
			verbose = 1;
			break;

		case 'V':
			PRINTF_THIS_IS_DBMAIL;
			printf("Internal Version %ld\n\n", config_get_app_version());
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
		TRACE(TRACE_INFO, "Choosing dry-run mode. No changes will be made at this time.");
		no_to_all = 1;
 	}

	config_read(configFile);
	SetTraceLevel("UTIL");
	GetDBParams();

	qverbosef("Opening connection to database... \n");
	if (db_connect() != 0) {
		qprintf("Failed. An error occured. Please check log.\n");
		TRACE(TRACE_INFO, "Failed. An error occured. Please check log.");
		return -1;
	}

	qverbosef("Opening connection to authentication... \n");
	if (auth_connect() != 0) {
		qprintf("Failed. An error occured. Please check log.\n");
		TRACE(TRACE_INFO, "Failed. An error occured. Please check log.");
		return -1;
	}

	qverbosef("Ok. Connected.\n");
	TRACE(TRACE_INFO, "Ok. Connected.");

	if (erase_old) do_erase_old(days_erase, mbtrash_name);
	if (move_old) do_move_old(days_move, mbinbox_name, mbtrash_name);
	if (check_integrity) do_check_integrity();
	if (purge_deleted) do_purge_deleted();
	if (is_header) do_header_cache();
	if (set_deleted) do_set_deleted();
	if (dangling_aliases) do_dangling_aliases();
	if (check_iplog) do_check_iplog(timespec_iplog);
	if (check_replycache) do_check_replycache(timespec_replycache);
	if (vacuum_db) do_vacuum_db();
	if (rehash) do_rehash();
	if (migrate) do_migrate(migrate_limit);
	if (check_empty_envelope) do_check_empty_envelope();

	if (!has_errors && !serious_errors) {
		qprintf("\nMaintenance done. No errors found.\n");
		TRACE(TRACE_INFO, "Maintenance done. No errors found.");
	} else {
		qprintf("\nMaintenance done. Errors were found");
		TRACE(TRACE_INFO, "Maintenance done. Errors were found");
		if (serious_errors) {
			qprintf(" but not fixed due to failures.\n");
			qprintf("Please check the logs for further details, "
				"turning up the trace level as needed.\n");
			TRACE(TRACE_INFO, "Errors not fixed due to failures.");
			// Indicate that something went really wrong
			has_errors = 3;
		} else if (no_to_all) {
			qprintf(" but not fixed.\n");
			qprintf("Run again with the '-y' option to "
				"repair the errors.\n");
			TRACE(TRACE_INFO, "Errors found but not fixed");
			TRACE(TRACE_INFO,
				"Run again with the '-y' option to repair the errors."
			);
			// Indicate that the program should be run with -y
			has_errors = 2;
		} else if (yes_to_all) {
			qprintf(" and fixed.\n");
			qprintf("Recommend running dbmail-util again to "
				"confirm that all errors were repaired.\n");
			TRACE(TRACE_INFO, "Errors found and fixed");
			TRACE(TRACE_INFO, "Recommend running dbmail-util again to "
				"confirm that all errors were repaired.");
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

static int db_count_iplog(TimeString_T lasttokeep, uint64_t *rows)
{
	Connection_T c; ResultSet_T r; volatile int t = DM_SUCCESS;
	Field_T to_date_str;
	assert(rows != NULL);
	*rows = 0;

	char2date_str(lasttokeep, &to_date_str);

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT COUNT(*) FROM %spbsp WHERE since < %s", DBPFX, to_date_str);
		if (db_result_next(r))
			*rows = db_result_get_u64(r,0);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

static int db_cleanup_iplog(TimeString_T lasttokeep)
{
	Field_T to_date_str;
	char2date_str(lasttokeep, &to_date_str);
	return db_update("DELETE FROM %spbsp WHERE since < %s", DBPFX, to_date_str);
}

static int db_count_replycache(TimeString_T lasttokeep, uint64_t *rows)
{
	Connection_T c; ResultSet_T r; volatile int t = FALSE;
	Field_T to_date_str;
	assert(rows != NULL);
	*rows = 0;

	char2date_str(lasttokeep, &to_date_str);

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT COUNT(*) FROM %sreplycache WHERE lastseen < %s", DBPFX, to_date_str);
		if (db_result_next(r))
			*rows = db_result_get_u64(r,0);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

static int db_cleanup_replycache(TimeString_T lasttokeep)
{
	Field_T to_date_str;
	char2date_str(lasttokeep, &to_date_str);
	return db_update("DELETE FROM %sreplycache WHERE lastseen < %s", DBPFX, to_date_str);
}

static int db_count_deleted(uint64_t * rows)
{
	Connection_T c; ResultSet_T r; volatile int t = TRUE;
	assert(rows != NULL); *rows = 0;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT COUNT(*) FROM %smessages WHERE status = %d", DBPFX, MESSAGE_STATUS_DELETE);
		if (db_result_next(r))
			*rows = db_result_get_int(r,0);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

static int db_set_deleted(void)
{
	return db_update("UPDATE %smessages SET status = %d WHERE status = %d", DBPFX, MESSAGE_STATUS_PURGE, MESSAGE_STATUS_DELETE);
}

static int db_deleted_purge(void)
{
	return db_update("DELETE FROM %smessages WHERE status=%d", DBPFX, MESSAGE_STATUS_PURGE);
}

static int db_deleted_count(uint64_t * rows)
{
	Connection_T c; ResultSet_T r; volatile int t = FALSE;
	assert(rows); *rows = 0;

	c = db_con_get();
	r = db_query(c, "SELECT COUNT(*) FROM %smessages WHERE status=%d", DBPFX, MESSAGE_STATUS_PURGE);
	TRY
		if (db_result_next(r)) {
			*rows = db_result_get_int(r,0);
			t = TRUE;
		}
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int do_purge_deleted(void)
{
	uint64_t deleted_messages;

	if (no_to_all) {
		qprintf("\nCounting messages with DELETE status...\n");
		TRACE(TRACE_INFO, "Counting messages with DELETE status...");
		if (! db_deleted_count(&deleted_messages)) {
			qprintf ("Failed. An error occured. Please check log.\n");
			TRACE(TRACE_INFO, "Failed. An error occured. Please check log.");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%" PRIu64 "] messages have DELETE status.\n", deleted_messages);
		TRACE(TRACE_INFO, "Ok. [%" PRIu64 "] messages have DELETE status.", deleted_messages);
	}
	if (yes_to_all) {
		qprintf("\nDeleting messages with DELETE status...\n");
		TRACE(TRACE_INFO, "Deleting messages with DELETE status...");
		if (! db_deleted_purge()) {
			qprintf ("Failed. An error occured. Please check log.\n");
			TRACE(TRACE_INFO, "Failed. An error occured. Please check log");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. Messages deleted.\n");
		TRACE(TRACE_INFO, "Ok. Messages deleted.");
	}
	return 0;
}

int do_set_deleted(void)
{
	uint64_t messages_set_to_delete;

	if (no_to_all) {
		// TODO: Count messages to delete.
		qprintf("\nCounting deleted messages that need the DELETE status set...\n");
		TRACE(TRACE_INFO, "Counting deleted messages that need the DELETE status set...");
		if (! db_count_deleted(&messages_set_to_delete)) {
			qprintf ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%" PRIu64 "] messages need to be set for deletion.\n", messages_set_to_delete);
		TRACE(TRACE_INFO, "Ok. [%" PRIu64 "] messages need to be set for deletion.", messages_set_to_delete);
	}
	if (yes_to_all) {
		qprintf("\nSetting DELETE status for deleted messages...\n");
		TRACE(TRACE_INFO, "Setting DELETE status for deleted messages...");
		if (! db_set_deleted()) {
			qprintf ("Failed. An error occured. Please check log.\n");
			TRACE(TRACE_INFO, "Failed. An error occured. Please check log.");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. Messages set for deletion.\n");
		TRACE(TRACE_INFO, "Ok. Messages set for deletion.");
		qprintf("\nRe-calculating used quota for all users...\n");
		TRACE(TRACE_INFO, "Re-calculating used quota for all users...");
		if (dm_quota_rebuild() < 0) {
			qprintf ("Failed. An error occured. Please check log.\n");
			TRACE(TRACE_INFO, "Failed. An error occured. Please check log");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. Used quota updated for all users.\n");
		TRACE(TRACE_INFO, "Ok. Used quota updated for all users.");
	}
	return 0;
}

GList *find_dangling_aliases(const char * const name)
{
	int result;
	char *username;
	GList *uids = NULL;
	GList *fwds = NULL;
	GList *dangling = NULL;

	/* For each alias, figure out if it resolves to a valid user
	 * or some forwarding address. If neither, remove it. */

	result = auth_check_user_ext(name,&uids,&fwds,0);
	
	if (!result) {
		qprintf("Nothing found searching for [%s].\n", name);
		TRACE(TRACE_INFO, "Nothing found searching for [%s].", name);
		serious_errors = 1;
		return dangling;
	}

	uids = g_list_first(uids);
	while (uids) {
		username = auth_get_userid(*(uint64_t *)uids->data);
		if (!username)
			dangling = g_list_prepend(dangling, uids->data);

		g_free(username);
		if (! g_list_next(uids))
			break;
		uids = g_list_next(uids);
	}

	return dangling;
}

int do_dangling_aliases(void)
{
	int count = 0;
	int result = 0;
	GList *aliases = NULL;

	if (no_to_all) {
		qprintf("\nCounting aliases with nonexistent delivery userid's...\n");
		TRACE(TRACE_INFO, "nCounting aliases with nonexistent delivery userid's...");
	}
	if (yes_to_all) {
		qprintf("\nRemoving aliases with nonexistent delivery userid's...\n");
		TRACE(TRACE_INFO, "Removing aliases with nonexistent delivery userid's...");
	}

	aliases = auth_get_known_aliases();
	aliases = g_list_dedup(aliases, (GCompareFunc)strcmp, TRUE);
	aliases = g_list_first(aliases);
	while (aliases) {
		char deliver_to[21];
		GList *dangling = find_dangling_aliases(aliases->data);

		dangling = g_list_first(dangling);
		while (dangling) {
			count++;
			g_snprintf(deliver_to, 21, "%" PRIu64 "", *(uint64_t *)dangling->data);
			qverbosef("Dangling alias [%s] delivers to nonexistent user [%s]\n",
				(char *)aliases->data, deliver_to);
			TRACE(TRACE_INFO, "Dangling alias [%s] delivers to nonexistent user [%s]",
				(char *)aliases->data, deliver_to);
			if (yes_to_all) {
				if (auth_removealias_ext(aliases->data, deliver_to) < 0) {
					qprintf("Error: could not remove alias [%s] deliver to [%s]\n",
						(char *)aliases->data, deliver_to);
					TRACE(TRACE_INFO, "Error: could not remove alias [%s] deliver to [%s]",
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

	qprintf("Ok. Found [%d] dangling aliases.\n", count);
	TRACE(TRACE_INFO, "Ok. Found [%d] dangling aliases.", count);
	if (count > 0) {
		has_errors = 1;
	}

	return result;
}

int do_check_integrity(void)
{
	time_t start, stop;
	GList *lost = NULL;
	const char *action;
	gboolean cleanup;
	long count = 0;

	if (yes_to_all) {
		action = "Repairing";
		cleanup = TRUE;
	} else {
		action = "Checking";
		cleanup = FALSE;
	}

	qprintf("\n%s DBMAIL message integrity...\n", action);
	TRACE(TRACE_INFO, "%s DBMAIL message integrity...", action);

	/* This is what we do:
	 3. Check for loose physmessages
	 4. Check for loose partlists
	 5. Check for loose mimeparts
	 6. Check for loose headernames
	 7. Check for loose headervalues
	 */

	/* part 3 */
	time(&start);
	qprintf("\n%s DBMAIL physmessage integrity...\n", action);
	TRACE(TRACE_INFO, "%s DBMAIL physmessage integrity...", action);
	if ((count = db_icheck_physmessages(cleanup)) < 0) {
		qprintf("Failed. An error occurred. Please check log.\n");
		serious_errors = 1;
		return -1;
	}
	qprintf("Ok. Found [%ld] unconnected physmessages.\n", count);
	TRACE(TRACE_INFO, "Ok. Found [%ld] unconnected physmessages.", count);
	if (count > 0) {
		if (cleanup) {
			qprintf("Ok. Orphaned physmessages deleted.\n");
		}
	}

	time(&stop);
	qverbosef("--- %s unconnected physmessages took %g seconds\n",
		action, difftime(stop, start));
	/* end part 3 */

	/* part 4 */
	start = stop;
	qprintf("\n%s DBMAIL partlists integrity...\n", action);
	TRACE(TRACE_INFO, "%s DBMAIL partlists integrity...", action);
	if ((count = db_icheck_partlists(cleanup)) < 0) {
		qprintf("Failed. An error occurred. Please check log.\n");
		TRACE(TRACE_INFO, "Failed. An error occurred. Please check log.");
		serious_errors = 1;
		return -1;
	}
	qprintf("Ok. Found [%ld] unconnected partlists.\n", count);
	TRACE(TRACE_INFO, "Ok. Found [%ld] unconnected partlists.", count);
	if (count > 0) {
		if (cleanup) {
			qprintf("Ok. Orphaned partlists deleted.\n");
			TRACE(TRACE_INFO, "Ok. Orphaned partlists deleted.");
		}
	}

	time(&stop);
	qverbosef("--- %s unconnected partlists took %g seconds\n",
		action, difftime(stop, start));
	TRACE(TRACE_INFO, "--- %s unconnected partlists took %g seconds",
		action, difftime(stop, start));
	/* end part 4 */

	/*  part 5 */
	start = stop;
	qprintf("\n%s DBMAIL mimeparts integrity...\n", action);
	TRACE(TRACE_INFO, "%s DBMAIL mimeparts integrity...", action);
	if ((count = db_icheck_mimeparts(cleanup)) < 0) {
		qprintf("Failed. An error occurred. Please check log.\n");
		serious_errors = 1;
		return -1;
	}
	TRACE(TRACE_INFO, "Ok. Found [%ld] unconnected mimeparts.", count);
	qprintf("Ok. Found [%ld] unconnected mimeparts.\n", count);
	if (count > 0) {
		if (cleanup) {
			qprintf("Ok. Orphaned mimeparts deleted.\n");
			TRACE(TRACE_INFO, "Ok. Orphaned mimeparts deleted.");
		}
	}

	time(&stop);
	qverbosef("--- %s unconnected mimeparts took %g seconds\n",
		action, difftime(stop, start));
	TRACE(TRACE_INFO, "--- %s unconnected mimeparts took %g seconds\n",
		action, difftime(stop, start));
	/* end part 5 */

	/* part 6 */
	Field_T config;
	gboolean cache_readonly = true;
	config_get_value("header_cache_readonly", "DBMAIL", config);
	if (strlen(config)) {
		if (SMATCH(config, "false") || SMATCH(config, "no")) {
			cache_readonly = false;
		}
	}

	if (! cache_readonly) {
		start = stop;
		qprintf("\n%s DBMAIL headernames integrity...\n", action);
		TRACE(TRACE_INFO, "%s DBMAIL headernames integrity...", action);
		if ((count = db_icheck_headernames(cleanup)) < 0) {
			qprintf("Failed. An error occurred. Please check log.\n");
			TRACE(TRACE_INFO, "Failed. An error occurred. Please check log.");
			serious_errors = 1;
			return -1;
		}

		qprintf("Ok. Found [%ld] unconnected headernames.\n", count);
		TRACE(TRACE_INFO, "Ok. Found [%ld] unconnected headernames.", count);
		if (count > 0 && cleanup) {
			qprintf("Ok. Orphaned headernames deleted.\n");
		}

		time(&stop);
		qverbosef("--- %s unconnected headernames took %g seconds\n",
			action, difftime(stop, start));
		TRACE(TRACE_INFO, "--- %s unconnected headernames took %g seconds\n",
			action, difftime(stop, start));
	}
	/* end part 6 */

	/* part 7 */
	start = stop;
	qprintf("\n%s DBMAIL headervalues integrity...\n", action);
	TRACE(TRACE_INFO, "%s DBMAIL headervalues integrity...", action);
	if ((count = db_icheck_headervalues(cleanup)) < 0) {
		qprintf("Failed. An error occurred. Please check log.\n");
		TRACE(TRACE_INFO, "Failed. An error occurred. Please check log.");
		serious_errors = 1;
		return -1;
	}

	qprintf("Ok. Found [%ld] unconnected headervalues.\n", count);
	TRACE(TRACE_INFO, "Ok. Found [%ld] unconnected headervalues.\n", count);
	if (count > 0 && cleanup) {
		qprintf("Ok. Orphaned headervalues deleted.\n");
		TRACE(TRACE_INFO, "Ok. Orphaned headervalues deleted.");
	}

	time(&stop);
	qverbosef("--- %s unconnected headervalues took %g seconds\n",
		action, difftime(stop, start));
		TRACE(TRACE_INFO, "--- %s unconnected headervalues took %g seconds\n",
		action, difftime(stop, start));
	/* end part 7 */

	g_list_destroy(lost);
	lost = NULL;

	time(&stop);
	qverbosef("--- %s block integrity took %g seconds\n", action, difftime(stop, start));
	TRACE(TRACE_INFO, "--- %s block integrity took %g seconds\n", action, difftime(stop, start));

	return 0;
}

static int do_rfc_size(void)
{
	time_t start, stop;
	GList *lost = NULL;

	if (no_to_all) {
		qprintf("\nChecking DBMAIL for rfcsize field...\n");
		TRACE(TRACE_INFO, "Checking DBMAIL for rfcsize field...");
	}
	if (yes_to_all) {
		qprintf("\nRepairing DBMAIL for rfcsize field...\n");
		TRACE(TRACE_INFO, "Repairing DBMAIL for rfcsize field...");
	}
	time(&start);

	if (db_icheck_rfcsize(&lost) < 0) {
		qprintf("Failed. An error occured. Please check log.\n");
		TRACE(TRACE_INFO, "Failed. An error occured. Please check log.");
		serious_errors = 1;
		return -1;
	}

	TRACE(TRACE_INFO, "Ok. Found [%d] missing rfcsize values.", g_list_length(lost));
	qprintf("Ok. Found [%d] missing rfcsize values.\n", g_list_length(lost));
	TRACE(TRACE_INFO, "Ok. Found [%d] missing rfcsize values.\n", g_list_length(lost));
	if (g_list_length(lost) > 0) {
		has_errors = 1;
	}

	if (yes_to_all) {
		if (db_update_rfcsize(lost) < 0) {
			TRACE(TRACE_INFO, "Error setting the rfcsize values");
			qprintf("Error setting the rfcsize values");
			has_errors = 1;
		}
	}

	g_list_destroy(lost);

	time(&stop);
	qverbosef("--- checking rfcsize field took %g seconds\n",
	       difftime(stop, start));
	TRACE(TRACE_INFO, "--- checking rfcsize field took %g seconds\n",
	       difftime(stop, start));

	return 0;

}

static int do_envelope(void)
{
	time_t start, stop;
	GList *lost = NULL;

	if (no_to_all) {
		qprintf("\nChecking DBMAIL for cached envelopes...\n");
		TRACE(TRACE_INFO, "Checking DBMAIL for cached envelopes...");
	}
	if (yes_to_all) {
		qprintf("\nRepairing DBMAIL for cached envelopes...\n");
		TRACE(TRACE_INFO, "Repairing DBMAIL for cached envelopes...");
	}
	time(&start);

	if (db_icheck_envelope(&lost) < 0) {
		qprintf("Failed. An error occured. Please check log.\n");
		TRACE(TRACE_INFO, "Failed. An error occured. Please check log.");
		serious_errors = 1;
		return -1;
	}

	TRACE(TRACE_INFO, "Ok. Found [%d] missing envelope values.", g_list_length(lost));
	qprintf("Ok. Found [%d] missing envelope values.\n", g_list_length(lost));
	if (g_list_length(lost) > 0) {
		has_errors = 1;
	}

	if (yes_to_all) {
		if (db_set_envelope(lost) < 0) {
			qprintf("Error setting the envelope cache");
			TRACE(TRACE_INFO, "Error setting the envelope cache");
			has_errors = 1;
		}
	}

	g_list_destroy(lost);

	time(&stop);
	qverbosef("--- checking envelope cache took %g seconds\n",
	       difftime(stop, start));
	TRACE(TRACE_INFO, "--- checking envelope cache took %g seconds\n",
	       difftime(stop, start));

	return 0;

}

static int do_check_empty_envelope(void)
{
	time_t start, stop;
	GList *lost = NULL;

	if (no_to_all) {
		qprintf("\nChecking DBMAIL for empty cached envelopes...\n");
		TRACE(TRACE_INFO, "Checking DBMAIL for empty cached envelopes...");
	}
	if (yes_to_all) {
		qprintf("\nRepairing DBMAIL for empty cached envelopes...\n");
		TRACE(TRACE_INFO, "Repairing DBMAIL for empty cached envelopes...");
	}
	time(&start);

	if (db_icheck_empty_envelope(&lost) < 0) {
		qprintf("Failed. An error occured. Please check log.\n");
		TRACE(TRACE_INFO, "Failed. An error occured. Please check log.");
		serious_errors = 1;
		return -1;
	}

	qprintf("Ok. Found [%d] empty envelope values.\n", g_list_length(lost));
	TRACE(TRACE_INFO, "Ok. Found [%d] empty envelope values.", g_list_length(lost));
	if (g_list_length(lost) > 0) {
		has_errors = 1;
	}

	if (yes_to_all) {
		if (db_set_envelope(lost) < 0) {
			qprintf("Error setting the envelope cache");
			TRACE(TRACE_INFO, "Error setting the envelope cache");
			has_errors = 1;
		}
	}

	g_list_destroy(lost);

	time(&stop);
	qverbosef("--- checking empty envelope cache took %g seconds\n",
	       difftime(stop, start));
	TRACE(TRACE_INFO, "--- checking empty envelope cache took %g seconds\n",
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
	if (do_envelope()) {
		serious_errors = 1;
		return -1;
	}
	
	if (no_to_all) {
		qprintf("\nChecking DBMAIL for cached header values...\n");
		TRACE(TRACE_INFO, "Checking DBMAIL for cached header values...");
	}
	if (yes_to_all) {
		qprintf("\nRepairing DBMAIL for cached header values...\n");
		TRACE(TRACE_INFO, "Repairing DBMAIL for cached header values...");
	}

	time(&start);

	if (db_icheck_headercache(&lost) < 0) {
		qprintf("Failed. An error occured. Please check log.\n");
		TRACE(TRACE_INFO, "Failed. An error occured. Please check log.");
		serious_errors = 1;
		return -1;
	}

	TRACE(TRACE_INFO, "Ok. Found [%d] un-cached physmessages.", g_list_length(lost));
	qprintf("Ok. Found [%d] un-cached physmessages.\n", g_list_length(lost));
	TRACE(TRACE_INFO, "Ok. Found [%d] un-cached physmessages.\n", g_list_length(lost));
	if (g_list_length(lost) > 0) {
		has_errors = 1;
	}

	if (yes_to_all) {
		if (db_set_headercache(lost) < 0) {
			qprintf("Error caching the header values");
			TRACE(TRACE_INFO, "Error caching the header values");
			serious_errors = 1;
		}
	}

	g_list_free(lost);

	time(&stop);
	qverbosef("--- checking cached headervalues took %g seconds\n",
	       difftime(stop, start));
	TRACE(TRACE_INFO, "--- checking cached headervalues took %g seconds\n",
	       difftime(stop, start));

	return 0;
}


int do_check_iplog(const char *timespec)
{
	uint64_t log_count;
	TimeString_T timestring;

	if (find_time(timespec, &timestring) != 0) {
		qprintf("\nFailed to find a timestring: [%s] is not <hours>h<minutes>m.\n",
		       timespec);
		TRACE(TRACE_INFO, "Failed to find a timestring: [%s] is not <hours>h<minutes>m.",
		       timespec);
		serious_errors = 1;
		return -1;
	}

	if (no_to_all) {
		qprintf("\nCounting IP entries older than [%s]...\n", timestring);
		TRACE(TRACE_INFO, "Counting IP entries older than [%s]...", timestring);
		if (db_count_iplog(timestring, &log_count) < 0) {
			qprintf("Failed. An error occured. Check the log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%" PRIu64 "] IP entries are older than [%s].\n",
		    log_count, timestring);
		TRACE(TRACE_INFO, "Ok. [%" PRIu64 "] IP entries are older than [%s].",
		    log_count, timestring);
	}
	if (yes_to_all) {
		qprintf("\nRemoving IP entries older than [%s]...\n", timestring);
		TRACE(TRACE_INFO, "Removing IP entries older than [%s]...", timestring);
		if (! db_cleanup_iplog(timestring)) {
			qprintf("Failed. Please check the log.\n");
			TRACE(TRACE_INFO, "Failed. Please check the log.");
			serious_errors = 1;
			return -1;
		}

		qprintf("Ok. IP entries older than [%s] removed.\n",
		       timestring);
		TRACE(TRACE_INFO, "Ok. IP entries older than [%s] removed.\n",
		       timestring);
	}
	return 0;
}

int do_check_replycache(const char *timespec)
{
	uint64_t log_count;
	TimeString_T timestring;

	if (find_time(timespec, &timestring) != 0) {
		qprintf("\nFailed to find a timestring: [%s] is not <hours>h<minutes>m.\n",
		       timespec);
		TRACE(TRACE_INFO, "Failed to find a timestring: [%s] is not <hours>h<minutes>m.",
		       timespec);
		serious_errors = 1;
		return -1;
	}

	if (no_to_all) {
		qprintf("\nCounting RC entries older than [%s]...\n", timestring);
		TRACE(TRACE_INFO, "Counting RC entries older than [%s]...", timestring);
		if (db_count_replycache(timestring, &log_count) < 0) {
			qprintf("Failed. An error occured. Check the log.\n");
			TRACE(TRACE_INFO, "Failed. An error occured. Check the log.");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%" PRIu64 "] RC entries are older than [%s].\n",
		    log_count, timestring);
		TRACE(TRACE_INFO, "Ok. [%" PRIu64 "] RC entries are older than [%s].\n",
		    log_count, timestring);
	}
	if (yes_to_all) {
		qprintf("\nRemoving RC entries older than [%s]...\n", timestring);
		TRACE(TRACE_INFO, "Removing RC entries older than [%s]...", timestring);
		if (! db_cleanup_replycache(timestring)) {
			qprintf("Failed. Please check the log.\n");
			TRACE(TRACE_INFO, "Failed. Please check the log.");
			serious_errors = 1;
			return -1;
		}

		qprintf("Ok. RC entries were older than [%s] cleaned.\n", timestring);
		TRACE(TRACE_INFO, "Ok. RC entries were older than [%s] cleaned.\n", timestring);
	}
	return 0;
}

int do_vacuum_db(void)
{
	if (no_to_all) {
		qprintf("\nCleaning the database not performed.\n");
		TRACE(TRACE_INFO, "Vacuum and optimize not performed.");
	}
	if (yes_to_all) {
		qprintf("\nCleaning the database...\n");
		TRACE(TRACE_INFO, "Cleaning the database...");
		fflush(stdout);
		if (db_cleanup() < 0) {
			qprintf("Failed. Please check the log.\n");
			TRACE(TRACE_INFO, "Failed. Please check the log.");
			serious_errors = 1;
			return -1;
		}

		qprintf("Ok. Database cleaned up.\n");
		TRACE(TRACE_INFO, "Ok. Database cleaned up.");
		qprintf("\nPlease see https://dbmail.org/en/manage/database-tips/\n");
		qprintf("for more information.\n");
	}
	return 0;
}

int do_rehash(void)
{
	if (yes_to_all) {
		qprintf ("Rebuild hash keys for stored message chunks...\n");
		TRACE(TRACE_INFO, "Rebuild hash keys for stored message chunks...");
		if (db_rehash_store() == DM_EQUERY) {
			qprintf("Failed. Please check the log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf ("Ok. Hash keys rebuild successfully.\n");
		TRACE(TRACE_INFO, "Ok. Hash keys rebuild successfully.");
	}

	return 0;

}

int do_migrate(int migrate_limit)
{
	Connection_T c; ResultSet_T r;
	int id = 0;
	volatile int count = 0;
	DbmailMessage *m;
	
	qprintf ("Migrate legacy 2.2.x messageblks to mimeparts...\n");
	TRACE(TRACE_INFO, "Migrate legacy 2.2.x messageblks to mimeparts...");
	if (!yes_to_all) {
		c = db_con_get();
		TRY
			db_begin_transaction(c);
			r = db_query(c, "SELECT count(physmessage_id) FROM %smessageblks", DBPFX);
			while (db_result_next(r))
			{
				count = db_result_get_u64(r,0);
			}
			db_commit_transaction(c);
		CATCH(SQLException)
			LOG_SQLERROR;
			db_rollback_transaction(c);
			return -1;
		FINALLY
			db_con_close(c);
		END_TRY;

		qprintf ("There are %d messageblks available for migration.\n", count);
		TRACE(TRACE_INFO, "There are %d messageblks available for migration.", count);

		qprintf ("\tmigration skipped. Use -y option to perform migration.\n");
		TRACE(TRACE_INFO, "  migration skipped. Use -y option to perform migration.");

		return 0;
	}
	qprintf ("Preparing to migrate up to %d physmessages.\n", migrate_limit);
	TRACE(TRACE_INFO, "Preparing to migrate up to %d physmessages.", migrate_limit);

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		r = db_query(c, "SELECT DISTINCT(physmessage_id) FROM %smessageblks LIMIT %d", DBPFX, migrate_limit);
		while (db_result_next(r))
		{
			count++;
			id = db_result_get_u64(r,0);
			m = dbmail_message_new(NULL);
			m = dbmail_message_retrieve(m, id);
			if(! dm_message_store(m)) {
				if(verbose) qprintf ("%d ",id);
				db_update("DELETE FROM %smessageblks WHERE physmessage_id = %d", DBPFX, id);
			}
			dbmail_message_free(m);
		}
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		return -1;
	FINALLY
		db_con_close(c);
	END_TRY;
	
	qprintf ("Migration complete. Migrated %d physmessages.\n", count);
	TRACE(TRACE_INFO, "Migration complete. Migrated %d physmessages.", count);

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		r = db_query(c, "SELECT count(physmessage_id) FROM %smessageblks", DBPFX);
		while (db_result_next(r))
		{
			count = db_result_get_u64(r,0);
		}
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		return -1;
	FINALLY
		db_con_close(c);
	END_TRY;

	qprintf ("There are %d messageblks to be migrated.\n", count);
	TRACE(TRACE_INFO, "There are %d messageblks to be migrated.", count);
	return 0;
}

/* Makes a date/time string: YYYY-MM-DD HH:mm:ss
 * based on current time minus timespec
 * timespec contains: <n>h<m>m for a timespan of n hours, m minutes
 * hours or minutes may be absent, not both
 *
 * Returns NULL on error.
 */
int find_time(const char *timespec, TimeString_T *timestring)
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
	strftime((char *) timestring, sizeof(TimeString_T),
		 "%Y-%m-%d %H:%M:%S", &tm);

	return 0;
}

/* Delete message from mailbox if it is Trash and the message date less then passed date */
int do_erase_old(int days, char * mbtrash_name)
{
	Connection_T c; PreparedStatement_T s; ResultSet_T r;
	char expire [DEF_FRAGSIZE];
	memset(expire,0,sizeof(expire));
	snprintf(expire, DEF_FRAGSIZE-1, db_get_sql(SQL_EXPIRE), days);

	c = db_con_get();

	s = db_stmt_prepare(c,"SELECT msg.message_idnr FROM %smessages msg "
			      "JOIN %sphysmessage phys ON msg.physmessage_id = phys.id "
			      "JOIN %smailboxes mb ON msg.mailbox_idnr = mb.mailbox_idnr "
			      "WHERE mb.name = ? AND msg.status < %d "
			      "AND phys.internal_date < %s ",
			      DBPFX, DBPFX, DBPFX, MESSAGE_STATUS_DELETE, expire);

	db_stmt_set_str(s, 1, mbtrash_name);

	TRY
		r = db_stmt_query(s);
		while(db_result_next(r)) 
		{
			uint64_t id = db_result_get_u64(r, 0);
			qprintf("Deleting message id(%" PRIu64 ")\n", id);
			db_set_message_status(id,MESSAGE_STATUS_PURGE);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
	return -1;
	FINALLY
		db_con_close(c);
	END_TRY;

	return 0;
}

/* Move message to Trash if the message is in INBOX mailbox and date less then passed date. */
int do_move_old (int days, char * mbinbox_name, char * mbtrash_name)
{
	Connection_T c; ResultSet_T r; ResultSet_T r1; PreparedStatement_T s; PreparedStatement_T s1;
	int skip = 1;
	char expire [DEF_FRAGSIZE];
        uint64_t mailbox_to;
        uint64_t mailbox_from;

	memset(expire,0,sizeof(expire));
	snprintf(expire, DEF_FRAGSIZE-1, db_get_sql(SQL_EXPIRE), days);

	c = db_con_get();
	s = db_stmt_prepare(c,"SELECT msg.message_idnr, mb.owner_idnr, mb.mailbox_idnr FROM %smessages msg "
			      "JOIN %sphysmessage phys ON msg.physmessage_id = phys.id "
			      "JOIN %smailboxes mb ON msg.mailbox_idnr = mb.mailbox_idnr "
			      "WHERE mb.name = ? AND msg.status < %d "
			      "AND phys.internal_date < %s", 
			      DBPFX, DBPFX, DBPFX, MESSAGE_STATUS_DELETE, expire);
	s1 = db_stmt_prepare(c, "SELECT mailbox_idnr FROM %smailboxes WHERE owner_idnr = ? AND name = ?", DBPFX);

	db_stmt_set_str(s, 1, mbinbox_name);

	TRY
		r = db_stmt_query(s);
		while (db_result_next(r))
		{
			skip = 1;
			uint64_t id = db_result_get_u64(r, 0);
			uint64_t user_id = db_result_get_u64(r, 1);
			mailbox_from = db_result_get_u64(r, 2);

			db_stmt_set_u64(s1,1,user_id);
			db_stmt_set_str(s1,2,mbtrash_name);

			r1 = db_stmt_query(s1);
			if (db_result_next(r1)) {
				mailbox_to = db_result_get_u64(r1, 0);
				skip = 0;
			} 

			if (!skip) {
				db_move_message(id, mailbox_to);
				db_mailbox_seq_update(mailbox_to, 0);
				db_mailbox_seq_update(mailbox_from, 0);
			}
			else {
				qprintf("User(%" PRIu64 ") doesn't have mailbox(%s)\n", user_id, mbtrash_name);
				TRACE(TRACE_INFO, "User(%" PRIu64 ") doesn't have mailbox(%s)", user_id, mbtrash_name);
			}
		}

	CATCH(SQLException)
		LOG_SQLERROR;
		return -1;
	FINALLY
		db_con_close(c);
	END_TRY;

	return 0;

}
