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
static int do_edit(u64_t user_idnr, char *name);
static int do_insert(u64_t user_idnr, char *name, char *source);
static int do_cat(u64_t user_idnr, char *name, FILE *out);

int main(int argc, char *argv[])
{
	int res = 0, opt = 0, opt_prev = 0;
	u64_t user_idnr = 0;
	char *user_name = NULL;
	char *script_name = NULL;
	char *script_source = NULL;
	extern char *optarg;
	extern int opterr;

	int activate = 0, deactivate = 0, insert = 0;
	int remove = 0, list = 0, cat = 0, help = 0, edit = 0;

	openlog(PNAME, LOG_PID, LOG_MAIL);
	setvbuf(stdout, 0, _IONBF, 0);

	/* get options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt(argc, argv,
		"-a::d:i:c::r:u:le::" /* Major modes */
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
		case 'a': /* activate */
			activate = 1;
			goto major_script;
		case 'd': /* deactivate */
			deactivate = 1;
			goto major_script;
		case 'i': /* insert */
			insert = 1;
			goto major_script;
		case 'r': /* remove */
			remove = 1;
			goto major_script;

		major_script:
			if (!optarg) {
				/* Need optarg */
			} else if (!script_name) {
				script_name = g_strdup(optarg);
			} else if (!script_source) {
				script_source = g_strdup(optarg);
			}
			break;
		case 'e':
			edit = 1;

			if (optarg)
				script_name = g_strdup(optarg);

			break;
		case 'c':
			cat = 1;

			if (optarg)
				script_name = g_strdup(optarg);

			/* Don't print anything but the script. */
			quiet = 1;
			verbose = 0;

			break;
		case 'u':
			user_name = g_strdup(optarg);
			break;
		case 'l':
			list = 1;
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
			help = 1;
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
			PRINTF_THIS_IS_DBMAIL;
			return 1;

		default:
			help = 1;
			break;
		}
	}

/*
	if (insert) printf("got insert\n");
	if (remove) printf("got remove\n");
	if (list) printf("got list\n");
	if (cat) printf("got cat\n");
	if (activate) printf("got activate\n");
	if (deactivate) printf("got deactivate\n");
*/

	/* Only one major mode is allowed */
	if ((edit + insert + remove + list + cat > 1)
	/* Only insert/edit are allowed together with activate or deactivate */
	 || ((remove + list + cat == 1) && (activate + deactivate > 0))
	/* You may either activate or deactivate as a mode on its own*/
	 || (((edit + insert + remove + list + cat == 0) && (activate + deactivate != 1)))
	 || (help || !user_name || (no_to_all && yes_to_all))) {
		do_showhelp();
		goto mainend;
	}

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
	if (db_connect() != 0) {
		qerrorf("Failed. Could not connect to database (check log)\n");
		g_free(user_name);
		return -1;
	}

	/* Open authentication connection */
	if (auth_connect() != 0) {
		qerrorf("Failed. Could not connect to authentication (check log)\n");
		g_free(user_name);
		return -1;
	}

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

	if (insert)
		res = do_insert(user_idnr, script_name, script_source);
	if (remove)
		res = do_remove(user_idnr, script_name);
	if (edit)
		res = do_edit(user_idnr, script_name);
	if (activate)
		/* Don't activate the script if it wasn't inserted/edited */
		if (!(insert && res) && !(edit && res))
			res = do_activate(user_idnr, script_name);
	if (deactivate)
		res = do_deactivate(user_idnr, script_name);
	if (list)
		res = do_list(user_idnr);
	if (cat)
		res = do_cat(user_idnr, script_name, stdout);
	if (help)
		res = do_showhelp();

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

int do_edit(u64_t user_idnr, char *name)
{
	int ret = 0;
	int editor_val = 0;
	int old_yes_to_all = yes_to_all;
	char *tmp = NULL, *editor = NULL, *editor_cmd = NULL;
	FILE *ftmp = NULL;
	char *scriptname = NULL;
	struct stat stat_before, stat_after;

	if (!name) {
		if (db_get_sievescript_active(user_idnr, &scriptname)) {
			qerrorf("Database error when fetching active script!\n");
			ret = 1;
			goto cleanup;
		}
		
		if (scriptname == NULL) {
			qerrorf("No active script found!\n");
			ret = 1;
			goto cleanup;
		}

		name = scriptname;
	}

	/* Check for the EDITOR environment variable. */
	editor = getenv("EDITOR");

	if (!editor) {
		qerrorf("No EDITOR environment variable.\n");
		ret = 1;
		goto cleanup;
	}

	/* Open a temp file. */
	if (!(tmp = tempnam(NULL, "dbmail"))) {
		qerrorf("Could not make temporary file name: %s\n", strerror(errno));
		ret = 1;
		goto cleanup;
	}

	if (!(ftmp = fopen(tmp, "w+"))) {
		qerrorf("Could not open temporary file [%s]: %s\n", tmp, strerror(errno));
		ret = 1;
		goto cleanup;
	}

	/* do_cat the script. */
	if (do_cat(user_idnr, name, ftmp)) {
		qerrorf("Could not dump script [%s] to temporary file.\n", name);
		ret = 1;
		goto cleanup;
	}

	/* Make sure that the file is written before we call the editor. */
	fflush(ftmp);
	fstat(fileno(ftmp), &stat_before);

	/* Call the editor. */
	editor_cmd = g_strdup_printf("%s %s", editor, tmp);
	if ((editor_val = system(editor_cmd))) {
		qerrorf("Execution of EDITOR [%s] returned non-zero [%d].\n", editor, editor_val);
		ret = 1;
		goto cleanup;
	}

	fstat(fileno(ftmp), &stat_after);

	/* If the file does not appear to have changed, cancel insertion. */
	if ((stat_before.st_mtime == stat_after.st_mtime)
	 && (stat_before.st_size == stat_after.st_size)) {
		qprintf("File not modified, canceling.\n");
		ret = 0;
		goto cleanup;
	}

	/* do_insert the script (set yes_to_all as we will overwrite). */
	yes_to_all = 1;
	ret = do_insert(user_idnr, name, tmp);

	/* Ok, all done. */
cleanup:
	g_free(scriptname);
	g_free(editor_cmd);
	g_free(tmp);
	if (ftmp) fclose(ftmp);
	if (tmp) unlink(tmp);
	yes_to_all = old_yes_to_all;

	return ret;
}

int do_cat(u64_t user_idnr, char *name, FILE *out)
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

	fputs(buf, out);

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


