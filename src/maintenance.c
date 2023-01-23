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
 * This is the dbmail housekeeping program. 
 *	It checks the integrity of the database and does a cleanup of all
 *	deleted messages. 
 */

#include "dbmail.h"
#define THIS_MODULE "maintenance"
#define PNAME "dbmail/maintenance"

extern DBParam_T db_params;
#define DBPFX db_params.pfx

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
static int do_upgrade_schema(void);

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
	"     -b        body/header/envelope cache check\n"
	"     -p        purge messages have the DELETE status set\n"
	"     -d        set DELETE status for deleted messages\n"
	"     -s        remove dangling/invalid aliases and forwards\n"
	"     -r time   clear the replycache used for autoreply/vacation\n"
	"     -l time   clear the IP log used for IMAP/POP-before-SMTP\n"
	"               the time syntax is [<hours>h][<minutes>m]\n"
	"               valid examples: 72h, 4h5m, 10m\n"
	"     -M        migrate legacy 2.2.x messageblks to mimeparts table\n"
	"     --rehash  Rebuild hash keys for stored messages\n"
	"     --erase days  Delete messages older than date in INBOX/Trash \n"
	"     --move  days  Move messages from INBOX to INBOX/Trash\n"
	"     --inbox name  Inbox folder to move from, used in conjunction with --move\n"
	"     --trash name  Trash folder to move to, used in conjunction with --move\n"
	"     -m limit  limit migration to [limit] number of physmessages. Default 10000 per run\n"
	"\nCommon options for all DBMail utilities:\n"
	"     -f file   specify an alternative config file\n"
	"     -q        quietly skip interactive prompts\n"
	"               use twice to suppress error messages\n"
	"     -n        show the intended action but do not perform it, no to all\n"
	"     -y        perform all proposed actions, as though yes to all\n"
	"     -v        verbose details\n"
	"     -V        show the version\n"
	"     -h        show this help message\n"
	"\nSpecific Tasks:\n"
	"     --upgrade-schema        upgrade sql schema\n"

	);

	return 0;
}

int main(int argc, char *argv[])
{
	int check_integrity = 0;
	int check_iplog = 0, check_replycache = 0;
	char *timespec_iplog = NULL, *timespec_replycache = NULL;
	int vacuum_db = 0, purge_deleted = 0, set_deleted = 0, dangling_aliases = 0, rehash = 0, move_old = 0, erase_old = 0;
	int show_help = 0;
	int do_nothing = 1;
	int is_header = 0;
	int upgrade_schema = 0;
	int migrate = 0, migrate_limit = 10000;
	static struct option long_options[] = {
		{ "rehash", 0, 0, 0 },
		{ "move", 1, 0, 0 },
		{ "erase", 1, 0, 0 },
		{ "trash", 1, 0, 0 },
		{ "inbox", 1, 0, 0 },
		{ "upgrade-schema", 0, 0, 0 },
		{ "upgrade", 0, 0, 0 },
		{ 0, 0, 0, 0 }
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
	while ((opt = getopt_long(argc, argv, "-acgbtl:r:pudsMm:" "i" "f:qnyvVh", long_options, &opt_index)) != -1) {
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
			
			if (strcmp(long_options[opt_index].name,"upgrade-schema")==0) {
				upgrade_schema = 1;
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
			if (optarg && strlen(optarg) > 0) {
				memset(configFile, 0, sizeof(configFile));
				strncpy(configFile, optarg, sizeof(configFile)-1);
			} else {
				qerrorf("dbmail-util: -f requires a filename\n\n" );
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
		no_to_all = 1;
 	}

	config_read(configFile);
	SetTraceLevel("DBMAIL");
	GetDBParams();

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
	if (upgrade_schema) do_upgrade_schema();

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
		if (! db_deleted_count(&deleted_messages)) {
			qerrorf ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%" PRIu64 "] messages have DELETE status.\n", deleted_messages);
	}
	if (yes_to_all) {
		qprintf("\nDeleting messages with DELETE status...\n");
		if (! db_deleted_purge()) {
			qerrorf ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. Messages deleted.\n");
	}
	return 0;
}

int do_set_deleted(void)
{
	uint64_t messages_set_to_delete;

	if (no_to_all) {
		// TODO: Count messages to delete.
		qprintf("\nCounting deleted messages that need the DELETE status set...\n");
		if (! db_count_deleted(&messages_set_to_delete)) {
			qerrorf ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. [%" PRIu64 "] messages need to be set for deletion.\n", messages_set_to_delete);
	}
	if (yes_to_all) {
		qprintf("\nSetting DELETE status for deleted messages...\n");
		if (! db_set_deleted()) {
			qerrorf ("Failed. An error occured. Please check log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf("Ok. Messages set for deletion.\n");
		qprintf("Re-calculating used quota for all users...\n");
		if (dm_quota_rebuild() < 0) {
			qerrorf ("Failed. An error occured. Please check log.\n");
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
	GList *uids = NULL;
	GList *fwds = NULL;
	GList *dangling = NULL;

	/* For each alias, figure out if it resolves to a valid user
	 * or some forwarding address. If neither, remove it. */

	result = auth_check_user_ext(name,&uids,&fwds,0);
	
	if (!result) {
		qerrorf("Nothing found searching for [%s].\n", name);
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

	if (no_to_all)
		qprintf("\nCounting aliases with nonexistent delivery userid's...\n");
	if (yes_to_all)
		qprintf("\nRemoving aliases with nonexistent delivery userid's...\n");

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
	if ((count = db_icheck_physmessages(cleanup)) < 0) {
		qerrorf("Failed. An error occurred. Please check log.\n");
		serious_errors = 1;
		return -1;
	}
	if (count > 0) {
		qerrorf("Ok. Found [%ld] unconnected physmessages.\n", count);
		if (cleanup) {
			qerrorf("Ok. Orphaned physmessages deleted.\n");
		}
	} else {
		qprintf("Ok. Found [%ld] unconnected physmessages.\n", count);
	}

	time(&stop);
	qverbosef("--- %s unconnected physmessages took %g seconds\n",
		action, difftime(stop, start));
	/* end part 3 */

	/* part 4 */
	start = stop;
	qprintf("\n%s DBMAIL partlists integrity...\n", action);
	if ((count = db_icheck_partlists(cleanup)) < 0) {
		qerrorf("Failed. An error occurred. Please check log.\n");
		serious_errors = 1;
		return -1;
	}
	if (count > 0) {
		qerrorf("Ok. Found [%ld] unconnected partlists.\n", count);
		if (cleanup) {
			qerrorf("Ok. Orphaned partlists deleted.\n");
		}
	} else {
		qprintf("Ok. Found [%ld] unconnected partlists.\n", count);
	}

	time(&stop);
	qverbosef("--- %s unconnected partlists took %g seconds\n",
		action, difftime(stop, start));
	/* end part 4 */

	/*  part 5 */
	start = stop;
	qprintf("\n%s DBMAIL mimeparts integrity...\n", action);
	if ((count = db_icheck_mimeparts(cleanup)) < 0) {
		qerrorf("Failed. An error occurred. Please check log.\n");
		serious_errors = 1;
		return -1;
	}
	if (count > 0) {
		qerrorf("Ok. Found [%ld] unconnected mimeparts.\n", count);
		if (cleanup) {
			qerrorf("Ok. Orphaned mimeparts deleted.\n");
		}
	} else {
		qprintf("Ok. Found [%ld] unconnected mimeparts.\n", count);
	}

	time(&stop);
	qverbosef("--- %s unconnected mimeparts took %g seconds\n",
		action, difftime(stop, start));
	/* end part 5 */

	/* part 6 */
        Field_T config;
	gboolean cache_readonly = false;
	config_get_value("header_cache_readonly", "DBMAIL", config);
	if (strlen(config)) {
		if (SMATCH(config, "true") || SMATCH(config, "yes")) {
			cache_readonly = true;
		}
	}

        if (! cache_readonly) {
		start = stop;
		qprintf("\n%s DBMAIL headernames integrity...\n", action);
		if ((count = db_icheck_headernames(cleanup)) < 0) {
			qerrorf("Failed. An error occurred. Please check log.\n");
			serious_errors = 1;
			return -1;
		}

		qprintf("Ok. Found [%ld] unconnected headernames.\n", count);
		if (count > 0 && cleanup) {
			qerrorf("Ok. Orphaned headernames deleted.\n");
		}

		time(&stop);
		qverbosef("--- %s unconnected headernames took %g seconds\n",
			action, difftime(stop, start));
	}
	/* end part 6 */

	/* part 7 */
	start = stop;
	qprintf("\n%s DBMAIL headervalues integrity...\n", action);
	if ((count = db_icheck_headervalues(cleanup)) < 0) {
		qerrorf("Failed. An error occurred. Please check log.\n");
		serious_errors = 1;
		return -1;
	}

	qprintf("Ok. Found [%ld] unconnected headervalues.\n", count);
	if (count > 0 && cleanup) {
		qerrorf("Ok. Orphaned headervalues deleted.\n");
	}

	time(&stop);
	qverbosef("--- %s unconnected headervalues took %g seconds\n",
		action, difftime(stop, start));
	/* end part 7 */

	g_list_destroy(lost);
	lost = NULL;

	time(&stop);
	qverbosef("--- %s block integrity took %g seconds\n", action, difftime(stop, start));

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

	g_list_destroy(lost);

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
	uint64_t log_count;
	TimeString_T timestring;

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
		qprintf("Ok. [%" PRIu64 "] IP entries are older than [%s].\n",
		    log_count, timestring);
	}
	if (yes_to_all) {
		qprintf("\nRemoving IP entries older than [%s]...\n", timestring);
		if (! db_cleanup_iplog(timestring)) {
			qerrorf("Failed. Please check the log.\n");
			serious_errors = 1;
			return -1;
		}

		qprintf("Ok. IP entries older than [%s] removed.\n",
		       timestring);
	}
	return 0;
}

int do_check_replycache(const char *timespec)
{
	uint64_t log_count;
	TimeString_T timestring;

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
		qprintf("Ok. [%" PRIu64 "] RC entries are older than [%s].\n",
		    log_count, timestring);
	}
	if (yes_to_all) {
		qprintf("\nRemoving RC entries older than [%s]...\n", timestring);
		if (! db_cleanup_replycache(timestring)) {
			qerrorf("Failed. Please check the log.\n");
			serious_errors = 1;
			return -1;
		}

		qprintf("Ok. RC entries were older than [%s] cleaned.\n", timestring);
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

int do_rehash(void)
{
	if (yes_to_all) {
		qprintf ("Rebuild hash keys for stored message chunks...\n");
		if (db_rehash_store() == DM_EQUERY) {
			qerrorf("Failed. Please check the log.\n");
			serious_errors = 1;
			return -1;
		}
		qprintf ("Ok. Hash keys rebuild successfully.\n");
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
	if (!yes_to_all) {
		qprintf ("\tmigration skipped. Use -y option to perform migration.\n");
		return 0;
	}
	qprintf ("Preparing to migrate up to %d physmessages.\n", migrate_limit);

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
	return 0;
}

int do_upgrade_schema(void)
{
	const char *query = NULL;
	Connection_T c;	
	if (!yes_to_all) {
		qprintf ("\tupgrading skipped. Use -y option to perform migration.\n");
		return 0;
	}
	qprintf ("Preparing to upgrade \n");
	c = db_con_get();
	TRY
		db_begin_transaction(c);
		
		switch(db_params.db_driver) {
			case DM_DRIVER_SQLITE:
				query = DM_SQLITE_UPGRADE;
			break;
			case DM_DRIVER_MYSQL:
				query = DM_MYSQL_UPGRADE;
			break;
			case DM_DRIVER_POSTGRESQL:
				query = DM_PGSQL_UPGRADE;
			break;
			case DM_DRIVER_ORACLE:
				qprintf ("\tPlease upgrade Oracle manually.\n");
				return -1;
			break;
		}
		qprintf ("Executing\n%s\n...",query);
		db_exec(c, query);
	CATCH(SQLException)
		LOG_SQLERROR;
		db_rollback_transaction(c);
		return -1;
	FINALLY
		db_con_close(c);
	END_TRY;
	
	qprintf ("Upgrading complete.\n");
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
				qprintf("User(%" PRIu64 ") doesn't has mailbox(%s)\n", user_id, mbtrash_name);
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
