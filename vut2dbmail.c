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
/* $Id$ */

/**
 * \file vut2dbmail.c
 *
 * \brief converts a virtual user table to dbmail entries
 *        the input is read from inputFile
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include "db.h"
#include "auth.h"
#include "dbmail.h"

#define PNAME "dbmail/readvut"

#define MAXLINESIZE 1024
#define DEF_MAXMAILSIZE 1024

extern db_param_t _db_params;

FILE *inputFile = NULL;
char *configFile = DEFAULT_CONFIG_FILE;

/* Command line options. */
int quiet = 0, verbose = 0, no_to_all = 0, yes_to_all = 0;

char line[MAXLINESIZE];
int process_piece(char *left, char *right);

int do_showhelp(void) {
	printf("*** dbmail-readvut ***\n");

	printf("Use this program to copy a virtual user table among your DBMail users.\n");
	printf("See the man page for more info. Summary:\n\n");
	printf("     [file]    read the specified VUT file or stdin if not specified\n");

        printf("\nCommon options for all DBMail utilities:\n");
	printf("     -f file   specify an alternative config file\n");
	printf("     -q        quietly skip interactive prompts\n");
	printf("     -n        show the intended action but do not perform it, no to all\n");
	printf("     -y        perform all proposed actions, as though yes to all\n");
	printf("     -v        verbose details\n");
	printf("     -V        show the version\n");
	printf("     -h        show this help message\n");

	return 0;
}

int main(int argc, char *argv[])
{
	int opt;
	int i, result;
	char *left, *right, *tmp;

	openlog(PNAME, LOG_PID, LOG_MAIL);
	setvbuf(stdout, 0, _IONBF, 0);

	/* If we don't get an argument, use stdin. */
	inputFile = stdin;

	/* get options */
	opterr = 0;		/* suppress error message from getopt() */
	/* The optstring is: major modes, minor options, common options. */
	while ((opt = getopt(argc, argv, "-f:qnyvVhp::")) != -1) {
		switch (opt) {
		/* The filename isn't preceded by an option,
		 * so we're using the initial '-' in optstring
		 * to have it appear as an option to 1 (1, not '1') */
		case 1:
			if (optarg && strlen(optarg) > 0
			&& strcmp("-", optarg) != 0) {
				if (!(inputFile = fopen(optarg, "r"))) {
					fprintf(stderr, "Cannot open file [%s], error was [%s]\n",
						optarg, strerror(errno));
					return 1;
					}
				}
			break;
		/* Common options */
		case 'f':
			if (optarg && strlen(optarg) > 0)
				configFile = optarg;
			else {
				fprintf(stderr,
					"dbmail-readvut: -f requires a filename\n");
				return 1;
			}
			break;

		case 'h':
			do_showhelp();
			return 1;
			break;

		case 'n':
			if (!yes_to_all)
				no_to_all = 1;
			break;

		case 'y':
			if (!no_to_all)
				yes_to_all = 1;
			break;

		case 'q':
			if (!verbose)
				quiet = 1;
			break;

		case 'v':
			if (!quiet)
				verbose = 1;
			break;

		case 'V':
			printf("\n*** DBMAIL: dbmail-readvut version "
			       "$Revision$ %s\n\n", COPYRIGHT);
			return 0;

		default:
			/*printf("unrecognized option [%c], continuing...\n",optopt); */
			break;
		}
	}

	/* read the config file */
	ReadConfig("DBMAIL", configFile);
	SetTraceLevel("DBMAIL");
	GetDBParams(&_db_params);

	if (db_connect() != 0) {
		fprintf(stderr, "Could not connect to database\n");
		return 1;
	}

	if (auth_connect() != 0) {
		fprintf(stderr, "Could not connect to authentication\n");
		db_disconnect();
		return 1;
	}

	do {
		fgets(line, MAXLINESIZE, inputFile);

		if (ferror(inputFile) || feof(inputFile))
			break;

		if (line[0] != '#' && line[0] != '\n' && line[0]) {
			line[strlen(line) - 1] = 0;	/* remove trailing space */

			/* get left part of entry */
			for (i = 0; line[i] && !isspace(line[i]); i++);

			if (!line[i] || line[i] == '\n') {
				if (i > 0) {
					fprintf(stderr,
						"Found [%*s], don't know what to do with it\n",
						i - 1, line);
				}
				continue;
			}

			line[i] = 0;
			left = line;

			while (isspace(line[++i]));

			right = &line[i];

			do {
				tmp = strchr(right, ',');	/* find delimiter */

				if (tmp)
					*tmp = 0;	/* end string on delimiter position */

				if ((result =
				     process_piece(left, right)) < 0) {
					if (result == -1) {
						fprintf(stderr,
							"Error processing [%s] [%s]\n",
							left, right);
					}
				}

				if (tmp) {
					right = tmp + 1;
					while (isspace(*right))
						right++;
				}
			} while (tmp && *right);

		}
	} while (!feof(inputFile) && !ferror(inputFile));

	/* ok everything inserted. 
	 *
	 * the alias table should be cleaned up now..
	 */

	db_disconnect();
	auth_disconnect();
	config_free();
	return 0;
}


int process_piece(char *left, char *right)
{
	u64_t useridnr, clientidnr;

	/* check what right contains:
	 * username or email address
	 */

	if (strchr(right, '@') || strchr(right, '|')) {
		/* email
		 * add this alias if it doesn't already exist
		 */

		if (db_addalias_ext(left, right, 0) == -1)
			return -1;

		printf("alias [%s] --> [%s] created\n", left, right);
		return 0;
	} else {
		/* username
		 * check if this user exists
		 */

		if (auth_user_exists(right, &useridnr) < -1)
			return -1;

		if (useridnr == 0) {
			/* new user */
			if (auth_adduser
			    (right, "geheim", "", 0, 0,
			     &useridnr) == -1) {
				fprintf(stderr,
					"Could not add user [%s]\n",
					right);
				return -1;
			}
		}

		/* this user now exists, add alias */
		if (auth_getclientid(useridnr, &clientidnr) == -1) {
			fprintf(stderr,
				"Could not retrieve client id nr for user [%s] [id %llu]\n",
				right, useridnr);
			return -1;
		}

		if (db_addalias(useridnr, left, clientidnr) == -1) {
			fprintf(stderr,
				"Could not add alias [%s] for user [%s] [id %llu]\n",
				left, right, useridnr);
			return -1;
		}

		printf("alias [%s] --> [%s] created\n", left, right);
		return 0;
	}

}
