/*
 Copyright (C) 2003 Aaron Stone
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

#define qverbosef(fmt, args...) if (verbose) printf(fmt, ##args)
#define qprintf(fmt, args...) if (! (quiet||reallyquiet)) printf(fmt, ##args) 
#define qerrorf(fmt, args...) if (! reallyquiet) fprintf(stderr, fmt, ##args) 

extern char configFile[PATH_MAX];

static int do_showhelp(void);
static int do_list(uint64_t user_idnr);
static int do_activate(uint64_t user_idnr, char *name);
static int do_deactivate(uint64_t user_idnr, char *name);
static int do_remove(uint64_t user_idnr, char *name);
static int do_edit(uint64_t user_idnr, char *name);
static int do_insert(uint64_t user_idnr, char *name, char *source);
static int do_cat(uint64_t user_idnr, char *name, FILE *out);

int main(int argc, char *argv[])
{
	int res = 0, opt = 0, opt_prev = 0;
	uint64_t user_idnr = 0;
	char *user_name = NULL;
	char *script_name = NULL;
	char *script_source = NULL;
	extern char *optarg;
	extern int opterr;

	int activate = 0, deactivate = 0, insert = 0;
	int remove = 0, list = 0, cat = 0, help = 0, edit = 0;

	config_get_file();
	openlog(PNAME, LOG_PID, LOG_MAIL);
	setvbuf(stdout, 0, _IONBF, 0);

	/* get options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt(argc, argv,
		"-a::d::i:c::r:u:le::" /* Major modes */
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
			if (optarg && strlen(optarg) > 0) {
				memset(configFile, 0, sizeof(configFile));
				strncpy(configFile, optarg, sizeof(configFile)-1);
			} else {
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
	GetDBParams();

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
	if (! auth_user_exists(user_name, &user_idnr)) {
		qerrorf("User [%s] does not exist!\n", user_name);
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


int do_activate(uint64_t user_idnr, char *name)
{
	if (!name) {
		qerrorf("Must give the name of a script to activate.\n");
		return -1;
	}

	if (! dm_sievescript_activate(user_idnr, name)) {
		qerrorf("Error activating script [%s].\n"
		       "It is possible that no script is currently active!\n",
		       name);
		return -1;
	}

	qprintf("Script [%s] is now active. All others are inactive.\n", name);

	return 0;
}


int do_deactivate(uint64_t user_idnr, char *name)
{
	char *scriptname = NULL;

	if (!name) {
		if (dm_sievescript_get(user_idnr, &scriptname)) {
			qerrorf("Database error when fetching active script.\n");
			return -1;
		}
		
		if (scriptname == NULL) {
			qerrorf("No active script found.\n");
			return -1;
		}

		name = scriptname;
	}

	if (! dm_sievescript_deactivate(user_idnr, name)) {
		qerrorf("Error deactivating script [%s].\n", name);
		return -1;
	}
	qprintf("Script [%s] is now deactivated."
		" No scripts are currently active.\n",
		name);

	g_free(scriptname);

	return 0;
}

int do_edit(uint64_t user_idnr, char *name)
{
	int ret = 0;
	int editor_val = 0;
	int old_yes_to_all = yes_to_all;
	char *tmp = NULL, *editor = NULL, *editor_cmd = NULL;
	FILE *ftmp = NULL;
	char *scriptname = NULL;
	struct stat stat_before, stat_after;

	if (!name) {
		if (dm_sievescript_get(user_idnr, &scriptname)) {
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

	if (fstat(fileno(ftmp), &stat_after) == -1) {
		int serr = errno;
		qerrorf("Stat failed: [%s]", strerror(serr));
		ret = 1;
		goto cleanup;
	}

	/* If the file does not appear to have changed, cancel insertion. */
	if ((stat_before.st_mtime == stat_after.st_mtime)
	 && (stat_before.st_size == stat_after.st_size)) {
		qprintf("File not modified, canceling.\n");
		ret = 0;
		goto cleanup;
	}

	/* do_insert the script (set yes_to_all as we will overwrite). */
	yes_to_all = 1;
	if (do_insert(user_idnr, name, tmp)) {
		char dbmail_invalid_file[] = "dbmail-invalid.xxx.sieve";
		struct stat stat_inv;
		int i;
		/* Save the script locally. */
		for (i = 0; i < 20; i++) {
			sprintf(dbmail_invalid_file, "dbmail-invalid.%d.sieve", i);
			if (stat(dbmail_invalid_file, &stat_inv)) {
				/* Try to rename the tmp file to this unused name. */
				if (rename(tmp, dbmail_invalid_file) == 0)
					break;
				else
					qerrorf("Could not save script to [%s]: %s\n", dbmail_invalid_file, strerror(errno));
			}
		}
		qerrorf("Saved script to [%s]\n", dbmail_invalid_file);
	}

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

int do_cat(uint64_t user_idnr, char *name, FILE *out)
{
	int res = 0;
	char *buf = NULL;
	char *scriptname = NULL;

	if (name)
		scriptname = name;
	else
		res = dm_sievescript_get(user_idnr, &scriptname);

	if (res != 0) {
		qerrorf("Database error when fetching active script!\n");
		return -1;
	}
	
	if (scriptname == NULL) {
		qerrorf("No active script found!\n");
		return -1;
	}

	res = dm_sievescript_getbyname(user_idnr, scriptname, &buf);

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

// FIXME: libevent this
static int read_from_stream(FILE * instream, char **m_buf, int maxlen)
{
        size_t f_len = 0;
        size_t f_pos = 0;
        char *f_buf = NULL;
        int c;

        /* Allocate a zero length string on length 0. */
        if (maxlen == 0) {
                *m_buf = g_strdup("");
                return 0;
        }

        /* Allocate enough space for everything we're going to read. */
        if (maxlen > 0) {
                f_len = maxlen + 1;
        }

        /* Start with a default size and keep reading until EOF. */
        if (maxlen == -1) {
                f_len = 1024;
                maxlen = INT_MAX;
        }

        f_buf = g_new0(char, f_len);

        while ((int)f_pos < maxlen) {
                if (f_pos + 1 >= f_len) {
                        f_buf = g_renew(char, f_buf, (f_len *= 2));
                }

                c = fgetc(instream);
                if (c == EOF)
                        break;
                f_buf[f_pos++] = (char)c;
        }

        if (f_pos)
                f_buf[f_pos] = '\0';

        *m_buf = f_buf;

        return 0;
}

int do_insert(uint64_t user_idnr, char *name, char *source)
{
	int res = 0, was_found = 0;
	char *buf = NULL;
	FILE *file = NULL;
	SortResult_T *sort_result;

	res = dm_sievescript_getbyname(user_idnr, name, &buf);
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
	fclose(file);
	if (res != 0) {
		qerrorf("Error reading in your script!\n");
		return -1;
	}

	/* Check if the script is valid */
	res = dm_sievescript_add(user_idnr, "@!temp-script!@", buf);
	g_free(buf);
	if (res != 0) {
		qerrorf("Error inserting temporary script into the database!\n");
		return -1;
	}

	sort_result = sort_validate(user_idnr, "@!temp-script!@");
	if (sort_result == NULL) {
		qprintf("Script could not be validated.\n");
		dm_sievescript_delete(user_idnr, "@!temp-script!@");
		return -1;
	}
	if (sort_get_error(sort_result) != 0) {
		qprintf("Script [%s] has errors: %s.\n",
			name, sort_get_errormsg(sort_result));
		dm_sievescript_delete(user_idnr, "@!temp-script!@");
		sort_free_result(sort_result);
		return -1;
	}
	sort_free_result(sort_result);

	res = dm_sievescript_rename(user_idnr, "@!temp-script!@", name);
	if (res == -3) {
		qprintf("Script [%s] already exists.\n", name);
		dm_sievescript_delete(user_idnr, "@!temp-script!@");
		return -1;
	} else if (res != 0) {
		qerrorf("Error inserting script [%s] into the database!\n",
		       name);
		dm_sievescript_delete(user_idnr, "@!temp-script!@");
		return -1;
	}

	if (was_found) {
		if (dm_sievescript_isactive_byname(user_idnr, name)) {
			qprintf("Script [%s] successfully updated and remains active!\n", name);
		} else {
			qprintf("Script [%s] successfully updated and remains inactive!\n", name);
		}
	} else {
		qprintf("Script [%s] successfully inserted and marked inactive!\n", name);
	}
	return 0;
}


int do_remove(uint64_t user_idnr, char *name)
{
	if (! dm_sievescript_delete(user_idnr, name)) {
		qerrorf("Error deleting script [%s].\n", name);
		return -1;
	}

	qprintf("Script [%s] deleted.\n", name);

	return 0;
}


int do_list(uint64_t user_idnr)
{
	GList *scriptlist = NULL;

	if (dm_sievescript_list(user_idnr, &scriptlist) < 0) {
		qerrorf("Error retrieving Sieve script list.\n");
		return -1;
	}

	if (g_list_length(scriptlist) > 0) {
		printf("Found %u scripts:\n",
		       g_list_length(scriptlist));
	} else
		qprintf("No scripts found!\n");

	scriptlist = g_list_first(scriptlist);
	while (scriptlist) {
		sievescript_info *info = (sievescript_info *) scriptlist->data;
		if (info->active == 1)
			printf("  + ");
		else
			printf("  - ");
		printf("%s\n", info->name);

		if (! g_list_next(scriptlist)) break;
		scriptlist = g_list_next(scriptlist);
	}

	g_list_destroy(scriptlist);

	return 0;
}


int do_showhelp(void)
{
	printf(
	"*** dbmail-sievecmd ***\n"
//	Try to stay under the standard 80 column width
//	0........10........20........30........40........50........60........70........80
	"Use this program to manage your users' Sieve scripts.\n"
	"See the man page for more info. Summary:\n\n"
	"     -u username            Username of script user \n"
	"     -l                     List scripts belonging to user \n"
	"     -a scriptname          Activate the named script \n"
	"                            (only one script can be active; \n"
	"                             deactivates any others) \n"
	"     -d [scriptname]        Deactivate the named script \n"
	"     -c [scriptname]        Print the contents of the named script\n"
	"     -e [scriptname]        Edit the contents of the named script\n"
	"                            (if no script is given, the active \n"
	"                             script is printed) \n"
	"     -i scriptname file     Insert the named script from file \n"
	"                            (a single dash, -, indicates input \n"
	"                             from STDIN) \n"
	"     -r scriptname          Remove the named script \n"
	"                            (if script was active, no script is \n"
	"                             active after deletion) \n"

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


