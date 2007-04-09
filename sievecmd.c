/*
 Copyright (C) 2003 Aaron Stone

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
 * This is dbmail-sievecmd, which provides
 * a command line interface to the sievescripts */

#include "dbmail.h"
#define THIS_MODULE "sievecmd"
#define PNAME "dbmail/sievecmd"

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

/* set up database login data */
extern db_param_t _db_params;

static int do_showhelp(void);
static int do_list(u64_t user_idnr);
static int do_activate(u64_t user_idnr, char *name);
static int do_deactivate(u64_t user_idnr, char *name);
static int do_remove(u64_t user_idnr, char *name);
static int do_insert(u64_t user_idnr, char *name, char *source);
static int do_cat(u64_t user_idnr, char *name);

int main(int argc, char *argv[])
{
	int res = 0, opt = 0, opt_prev = 0, act = 0;
	u64_t user_idnr = 0;
	char *user_name = NULL;
	char *script_name = NULL;
	char *script_source = NULL;
	extern char *optarg;
	extern int opterr;

	openlog(PNAME, LOG_PID, LOG_MAIL);
	setvbuf(stdout, 0, _IONBF, 0);

	/* get options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt(argc, argv,
		"-a:d:i:c::r:u:l" /* Major modes */
		/*"i"*/ "f:qnyvVh" /* Common options */ )) != -1) {
		/* The initial "-" of optstring allows unaccompanied
		 * options and reports them as the optarg to opt 1 (not '1') */
		if (opt == 1)
			opt = opt_prev;
		opt_prev = opt;

		switch (opt) {
		case -1:
			/* Break right away if this is the end of the args */
			break;
		case 'a':
		case 'd':
		case 'i':
		case 'r':
			if (act != 0 && opt != opt_prev)
				act = 'h';
			else
				act = opt;

			if (!script_name) {
				script_name = g_strdup(optarg);
			} else if (!script_source) {
				script_source = g_strdup(optarg);
			}
			break;
		case 'c':
			if (optarg)
				script_name = g_strdup(optarg);
			act = opt;
			break;
		case 'u':
			user_name = g_strdup(optarg);
			break;
		case 'l':
			if (act != 0)
				act = 'h';
			else
				act = opt;
			break;

		/* Common options */
		/*case 'i': FIXME: this is from user.c, but we're using -i for insertion.
			printf("Interactive console is not supported in this release.\n");
			return 1;*/

		case 'f':
			if (optarg && strlen(optarg) > 0)
				configFile = optarg;
			else {
				qerrorf("dbmail-users: -f requires a filename\n\n");
				return 1;
			}
			break;

		case 'h':
			act = 'h';
			break;

		case 'n':
			printf("-n switch is not supported in this "
			       "version.\n");
			return 1;

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

		case 'v':
			if (!quiet)
				verbose = 1;
			break;

		case 'V':
			/* Show the version and return non-zero. */
			printf("\n*** DBMAIL: dbmail-sievecmd version "
			       "$Revision$ %s\n\n", COPYRIGHT);
			return 0;
			break;

		default:
			act = 'h';
			break;
		}
	}

	if (act == 'h' || act == 0 || !user_name || (no_to_all && yes_to_all)) {
		do_showhelp();
		goto mainend;
	}

	qprintf("*** dbmail-sievecmd ***\n");

	/* read the config file */
        if (config_read(configFile) == -1) {
                qerrorf("Failed. Unable to read config file %s\n",
                        configFile);
                res = -1;
                goto mainend;
        }
                
	SetTraceLevel("DBMAIL");
	GetDBParams(&_db_params);

	/* Open database connection */
	qprintf("Opening connection to database...\n");
	if (db_connect() != 0) {
		qerrorf("Failed. Could not connect to database (check log)\n");
		g_free(user_name);
		return -1;
	}

	/* Open authentication connection */
	qprintf("Opening connection to authentication...\n");
	if (auth_connect() != 0) {
		qerrorf("Failed. Could not connect to authentication (check log)\n");
		g_free(user_name);
		return -1;
	}

	qprintf("Ok. Connected!\n");

	/* Retrieve the user ID number */
	switch (auth_user_exists(user_name, &user_idnr)) {
	case 0:
		qerrorf("User [%s] does not exist!\n", user_name);
		res = -1;
		goto mainend;
		break;
	case -1:
		qerrorf("Error retrieving User ID Number\n");
		res = -1;
		goto mainend;
	}

	switch (act) {
	case 'a':
		res = do_activate(user_idnr, script_name);
		break;
	case 'd':
		res = do_deactivate(user_idnr, script_name);
		break;
	case 'i':
		res = do_insert(user_idnr, script_name, script_source);
		break;
	case 'c':
		res = do_cat(user_idnr, script_name);
		break;
	case 'r':
		res = do_remove(user_idnr, script_name);
		break;
	case 'l':
		res = do_list(user_idnr);
		break;
	case 'h':
	default:
		res = do_showhelp();
		break;
	}

      mainend:
	g_free(user_name);
	g_free(script_name);
	g_free(script_source);
	db_disconnect();
	auth_disconnect();
	config_free();
	return res;
}


int do_activate(u64_t user_idnr, char *name)
{
	int res = 0;

	res = db_activate_sievescript(user_idnr, name);
	if (res == -3) {
		qerrorf("Script [%s] does not exist.\n", name);
		return -1;
	} else if (res != 0) {
		qerrorf("Error activating script [%s].\n"
		       "It is possible that no script is currently active!\n",
		       name);
		return -1;
	}
	qprintf("Script [%s] is now active. All others are inactive.\n",
	       name);

	return 0;
}


int do_deactivate(u64_t user_idnr, char *name)
{
	int res = 0;

	res = db_deactivate_sievescript(user_idnr, name);
	if (res == -3) {
		qerrorf("Script [%s] does not exist.\n", name);
		return -1;
	} else if (res != 0) {
		qerrorf("Error deactivating script [%s].\n", name);
		return -1;
	}
	qprintf("Script [%s] is now deactivated."
		" No scripts are currently active.\n",
		name);

	return 0;
}

int do_cat(u64_t user_idnr, char *name)
{
	int res = 0;
	char *buf = NULL;
	char *scriptname = NULL;

	if (name)
		scriptname = name;
	else
		res = db_get_sievescript_active(user_idnr, &scriptname);

	if (res != 0) {
		qerrorf("Database error when fetching active script!\n");
		return -1;
	}
	
	if (scriptname == NULL) {
		qerrorf("No active script found!\n");
		return -1;
	}

	res = db_get_sievescript_byname(user_idnr, scriptname, &buf);

	if (res != 0) {
		qerrorf("Database error when fetching script!\n");
		return -1;
	}
	
	if (buf == NULL) {
		qerrorf("Script not found!\n");
		return -1;
	}

	printf("%s", buf);

	g_free(buf);
	if (!name)
		g_free(scriptname);

	return 0;
}

int do_insert(u64_t user_idnr, char *name, char *source)
{
	int res = 0, was_found = 0;
	char *buf = NULL;
	FILE *file = NULL;
	sort_result_t *sort_result;

	res = db_get_sievescript_byname(user_idnr, name, &buf);
	if (buf) {
		g_free(buf);
		was_found = 1;
	}
	if (res != 0) {
		qerrorf("Could not determine if a script by that name already exists.\n");
		return -1;
	}
	if (was_found && !yes_to_all) {
		qerrorf("A script by that name already exists. Use -y option to overwrite it.\n");
		return -1;
	}

	if (!source)
		file = stdin;
	else if (strcmp(source, "-") == 0)
		file = stdin;
	else 
		file = fopen(source, "r");

	if (!file) {
		qerrorf("Could not open file [%s]: %s\n",
			optarg, strerror(errno));
		return -1;
	}

	/* Read the file into a char array until EOF. */
	res = read_from_stream(file, &buf, -1);
	if (res != 0) {
		qerrorf("Error reading in your script!\n");
		return -1;
	}

	/* Check if the script is valid */
	res = db_add_sievescript(user_idnr, "@!temp-script!@", buf);
	g_free(buf);
	if (res != 0) {
		qerrorf("Error inserting temporary script into the database!\n");
		return -1;
	}

	sort_result = sort_validate(user_idnr, "@!temp-script!@");
	if (sort_result == NULL) {
		qprintf("Script could not be validated.\n");
		db_delete_sievescript(user_idnr, "@!temp-script!@");
		return -1;
	}
	if (sort_get_error(sort_result) != 0) {
		qprintf("Script [%s] has errors: %s.\n",
			name, sort_get_errormsg(sort_result));
		db_delete_sievescript(user_idnr, "@!temp-script!@");
		sort_free_result(sort_result);
		return -1;
	}
	sort_free_result(sort_result);

	res = db_rename_sievescript(user_idnr, "@!temp-script!@", name);
	if (res == -3) {
		qprintf("Script [%s] already exists.\n", name);
		db_delete_sievescript(user_idnr, "@!temp-script!@");
		return -1;
	} else if (res != 0) {
		qerrorf("Error inserting script [%s] into the database!\n",
		       name);
		db_delete_sievescript(user_idnr, "@!temp-script!@");
		return -1;
	}

	if (was_found) {
		if (db_check_sievescript_active_byname(user_idnr, name) == 0) {
			qprintf("Script [%s] successfully updated and remains active!\n", name);
		} else {
			qprintf("Script [%s] successfully updated and remains inactive!\n", name);
		}
	} else {
		qprintf("Script [%s] successfully inserted and marked inactive!\n", name);
	}
	return 0;
}


int do_remove(u64_t user_idnr, char *name)
{
	int res;

	res = db_delete_sievescript(user_idnr, name);
	if (res == -3) {
		qerrorf("Script [%s] does not exist.\n", name);
		return -1;
	} else if (res != 0) {
		qerrorf("Error deleting script [%s].\n", name);
		return -1;
	}

	qprintf("Script [%s] deleted.\n", name);

	return 0;
}


int do_list(u64_t user_idnr)
{
	struct dm_list scriptlist;
	struct element *tmp;

	if (db_get_sievescript_listall(user_idnr, &scriptlist) < 0) {
		qerrorf("Error retrieving Sieve script list.\n");
		return -1;
	}

	if (dm_list_length(&scriptlist) > 0) {
		printf("Found %ld scripts:\n",
		       dm_list_length(&scriptlist));
	} else
		qprintf("No scripts found!\n");

	tmp = dm_list_getstart(&scriptlist);
	while (tmp) {
		sievescript_info_t *info = (sievescript_info_t *) tmp->data;
		if (info->active == 1)
			printf("  + ");
		else
			printf("  - ");
		printf("%s\n", info->name);

		g_free(info->name);
		tmp = tmp->nextnode;
	}

	if (scriptlist.start)
		dm_list_free(&scriptlist.start);

	return 0;
}


int do_showhelp(void)
{
	printf("*** dbmail-sievecmd ***\n");

	printf("Use this program to manage your users' Sieve scripts.\n");
	printf("See the man page for more info. Summary:\n\n");
	printf("     -u username            Username of script user \n");
	printf("     -l                     List scripts belonging to user \n");
	printf("     -a scriptname          Activate the named script \n");
	printf("                            (only one script can be active; \n"
	       "                             deactivates any others) \n");
	printf("     -d scriptname          Deactivate the named script \n");
	printf("                            (no scripts will be active after this) \n");
	printf("     -i scriptname file     Insert the named script from file \n");
	printf("                            (a single dash, -, indicates input \n"
	       "                             from STDIN) \n");
	printf("     -c [scriptname]        Print the contents of the named script\n");
	printf("                            (if no script is given, the active \n"
	       "                             script is printed) \n");
	printf("     -r scriptname          Remove the named script \n");
	printf("                            (if script was active, no script is \n"
	       "                             active after deletion) \n");

	return 0;
}


