/*
 Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

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
 * (c) 2000-2002 IC&S, The Netherlands
 * This is the dbmail-user program
 * It makes adding users easier */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "user.h"
#include "auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbmail.h"
#include "list.h"
#include "debug.h"
#include "db.h"
#include <time.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif
#include "dbmd5.h"

char *configFile = DEFAULT_CONFIG_FILE;

#define SHADOWFILE "/etc/shadow"

char *getToken(char **str, const char *delims);
char csalt[] = "........";
char *bgetpwent(char *filename, char *name);
char *cget_salt(void);

/* database login data */
extern db_param_t _db_params;

/* valid characters for passwd/username */
const char ValidChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "_.!@#$%^&*()-+=~[]{}<>:;\\/";

void show_help(void);
int quiet = 0;

int quiet_printf(const char *fmt, ...);

int do_add(int argc, char *argv[]);
int do_change(char *argv[]);
int do_delete(char *name);
int do_show(char *name);
int do_empty(char *name);
int do_make_alias(char *argv[]);
int do_remove_alias(char *argv[]);

int is_valid(const char *name);

int main(int argc, char *argv[])
{
	struct list sysItems;
	int result;
	int argidx = 0;

	openlog(PNAME, LOG_PID, LOG_MAIL);

	setvbuf(stdout, 0, _IONBF, 0);

	if (argc < 2) {
		show_help();
		return 0;
	}

	if (strcasecmp(argv[1], "quiet") == 0) {
		if (argc < 3) {
			show_help();
			return 0;
		}

		quiet = 1;
		argidx = 1;
	}

	ReadConfig("DBMAIL", configFile, &sysItems);
	SetTraceLevel(&sysItems);
	GetDBParams(&_db_params, &sysItems);

	quiet_printf("\n*** dbmail-adduser ***\n");

	/* open database connection */
	quiet_printf("Opening connection to database...\n");
	if (db_connect() != 0) {
		quiet_printf
		    ("Failed. Could not connect to database (check log)\n");
		return -1;
	}

	/* open authentication connection */
	quiet_printf("Opening connection to authentication...\n");
	if (auth_connect() != 0) {
		quiet_printf
		    ("Failed. Could not connect to authentication (check log)\n");
		return -1;
	}

	quiet_printf("Ok. Connected\n");
	configure_debug(TRACE_ERROR, 1, 0);

	switch (argv[argidx + 1][0]) {
	case 'a':
		result = do_add(argc - (2 + argidx), &argv[2 + argidx]);
		break;
	case 'c':
		result = do_change(&argv[2 + argidx]);
		break;
	case 'd':
		result = do_delete(argv[2 + argidx]);
		break;
	case 's':
		result = do_show(argv[2 + argidx]);
		break;
	case 'f':
		result = do_make_alias(&argv[2 + argidx]);
		break;
	case 'x':
		result = do_remove_alias(&argv[2 + argidx]);
		break;
	case 'e':
		result = do_empty(argv[2 + argidx]);
		break;
	default:
		show_help();
		db_disconnect();
		auth_disconnect();
		return 0;
	}


	db_disconnect();
	auth_disconnect();
	return result;
}



/* 
 * adds a single alias (not connected to any user) 
 */
int do_make_alias(char *argv[])
{
	int result;

	if (!argv[0] || !argv[1]) {
		quiet_printf
		    ("invalid arguments specified. Check the man page\n");
		return -1;
	}

	quiet_printf("Adding alias [%s] --> [%s]...", argv[0], argv[1]);
	switch ((result = db_addalias_ext(argv[0], argv[1], 0))) {
	case -1:
		quiet_printf("Failed\n\nCheck logs for details\n\n");
		break;

	case 0:
		quiet_printf("Ok alias added\n");
		break;

	case 1:
		quiet_printf("Already exists. no extra alias added\n");
		result = -1;	/* return error */
		break;

	}

	return result;
}

int do_remove_alias(char *argv[])
{
	if (!argv[0] || !argv[1]) {
		quiet_printf
		    ("invalid arguments specified. Check the man page\n");
		return -1;
	}

	quiet_printf("Removing alias [%s] --> [%s]...", argv[0], argv[1]);
	if (db_removealias_ext(argv[0], argv[1]) != 0) {
		quiet_printf("Failed\n\nCheck logs for details\n\n");
		return -1;
	}

	quiet_printf("Ok alias removed\n");
	return 0;
}

int do_add(int argc, char *argv[])
{
	u64_t useridnr;
	int add_user_result;
	int i, result;
	char pw[50] = "";

	if (argc < 4) {
		quiet_printf
		    ("invalid number of options specified. Check the man page\n");
		return -1;
	}

	if (!is_valid(argv[0])) {
		quiet_printf
		    ("Error: invalid characters in username [%s] encountered\n",
		     argv[0]);
		return -1;
	}

	quiet_printf
	    ("Adding user %s with password %s, %s bytes mailbox limit and clientid %s...",
	     argv[0], argv[1], argv[3], argv[2]);

	/* check if we need to encrypt this pwd */
	if (strncasecmp(argv[1], "{crypt:}", strlen("{crypt:}")) == 0) {
		/* encrypt using crypt() */
		strcat(pw,
		       crypt(&argv[1][strlen("{crypt:}")], cget_salt()));
		add_user_result =
		    auth_adduser(argv[0], pw, "crypt", argv[2], argv[3],
				 &useridnr);
	} else if (strncasecmp(argv[1], "{crypt}", strlen("{crypt}")) == 0) {
		/* assume passwd is encrypted on command line */
		add_user_result =
		    auth_adduser(argv[0], &argv[1][strlen("{crypt}")],
				 "crypt", argv[2], argv[3], &useridnr);
	} else if (strncasecmp(argv[1], "{md5:}", strlen("{md5:}")) == 0) {
		/* encrypt using md5 crypt() */
		sprintf(pw, "%s%s%s", "$1$", cget_salt(), "$");
		strncpy(pw, crypt(&argv[1][strlen("{md5:}")], pw), 49);
		add_user_result = auth_adduser(argv[0], pw, "md5",
					       argv[2], argv[3],
					       &useridnr);
	} else if (strncasecmp(argv[1], "{md5}", strlen("{md5}")) == 0) {
		/* assume passwd is encrypted on command line */
		add_user_result =
		    auth_adduser(argv[0], &argv[1][strlen("{md5}")], "md5",
				 argv[2], argv[3], &useridnr);
	} else if (strncasecmp(argv[1], "{md5sum:}", strlen("{md5sum:}"))
		   == 0) {
		/* encrypt using md5 digest */
		strcat(pw, makemd5(&argv[1][strlen("{md5sum:}")]));
		add_user_result =
		    auth_adduser(argv[0], pw, "md5sum", argv[2], argv[3],
				 &useridnr);
	} else if (strncasecmp(argv[1], "{md5sum}", strlen("{md5sum}")) ==
		   0) {
		/* assume passwd is encrypted on command line */
		add_user_result =
		    auth_adduser(argv[0], &argv[1][strlen("{md5sum}")],
				 "md5sum", argv[2], argv[3], &useridnr);
	} else {
		add_user_result = auth_adduser(argv[0], argv[1], "",
					       argv[2], argv[3],
					       &useridnr);
	}

	if (add_user_result == -1) {
		/* check if existance of another user with the same name caused 
		   the failure */
		if (auth_user_exists(argv[0], &useridnr) == -1) {
			quiet_printf
			    ("Failed\n\nCheck logs for details\n\n");
			return -1;	/* database failure */
		}
		if (useridnr != 0)
			quiet_printf("Failed: user exists [%llu]\n",
				     useridnr);
		else {		/* useridnr is 0 ! */
			quiet_printf
			    ("Failed\n\nCheck logs for details\n\n");
			useridnr = -1;
		}
		return -1;
	}

	quiet_printf("Ok, user added id [%llu]\n", useridnr);

	for (i = 4, result = 0; i < argc; i++) {
		quiet_printf("Adding alias %s...", argv[i]);
		switch (db_addalias(useridnr, argv[i], atoi(argv[2]))) {
		case -1:
			quiet_printf("Failed\n");
			result = -1;
			break;

		case 0:
			quiet_printf("Ok, added\n");
			break;

		case 1:
			quiet_printf
			    ("Already exists. No extra alias added\n");
			result = -1;
			break;
		}
	}

	quiet_printf("adduser done\n");
	if (result != 0)
		quiet_printf
		    ("Warning: user added but not all the specified aliases\n");

	return result;
}


int do_change(char *argv[])
{
	int i, result = 0, retval = 0;
	u64_t newsize, userid, newcid;
	u64_t client_id;
	char *endptr = NULL, *entry = NULL, *passwdfile = NULL;
	char pw[50] = "";

	/* verify the existence of this user */
	if (auth_user_exists(argv[0], &userid) == -1) {
		quiet_printf
		    ("Error verifying existence of user [%s]. Please check the log.\n",
		     argv[0]);
		return -1;
	}

	if (userid == 0) {
		quiet_printf("Error: user [%s] does not exist.\n",
			     argv[0]);
		return -1;
	}

	quiet_printf("Performing changes for user [%s]...", argv[0]);

	for (i = 1; argv[i]; i++) {
		if (argv[i][0] != '-' && argv[i][0] != '+'
		    && argv[i][0] != 'x' && argv[i][0] != 'd'
		    && argv[i][0] != 'D') {
			quiet_printf
			    ("Failed: invalid option specified. Check the man page\n");
			return -1;
		}

		switch (argv[i][1]) {
		case 'u':
			/* change the name */
			if (argv[i][0] != '-') {
				quiet_printf
				    ("Failed: invalid option specified. Check the man page\n");
				return -1;
			}
			if (!is_valid(argv[i + 1])) {
				quiet_printf
				    ("\nWarning: username contains invalid characters. Username not updated. ");
				retval = -1;
			}

			if (auth_change_username(userid, argv[i + 1]) != 0) {
				quiet_printf
				    ("\nWarning: could not change username ");
				retval = -1;
			}

			i++;
			break;

		case 'p':
			/* change the password */
			if (!is_valid(argv[i + 1])) {
				quiet_printf
				    ("\nWarning: password contains invalid characters. Password not updated. ");
				retval = -1;
			}

			switch (argv[i][0]) {
			case '+':
				/* +p will convert clear text into crypt hash value */
				strcat(pw,
				       crypt(argv[i + 1], cget_salt()));
				result =
				    auth_change_password(userid, pw,
							 "crypt");
				break;
			case '-':
				strncpy(pw, argv[i + 1], 49);
				result =
				    auth_change_password(userid, pw, "");
				break;
			case 'x':
				/* 'xp' will copy passwd from command line 
				   assuming that the supplied passwd is crypt encrypted 
				 */
				strncpy(pw, argv[i + 1], 49);
				result =
				    auth_change_password(userid, pw,
							 "crypt");
				break;
			default:
				quiet_printf
				    ("Failed: invalid option specified. Check the man page\n");
				return -1;
			}

			if (result != 0) {
				quiet_printf
				    ("\nWarning: could not change password ");
				retval = -1;
			}

			i++;
			break;

		case '5':
			/* md5 passwords */
			if (!is_valid(argv[i + 1])) {
				quiet_printf
				    ("\nWarning: password contains invalid characters. Password not updated. ");
				retval = -1;
			}
			switch (argv[i][0]) {
			case '-':
				/* -5 takes a md5 hash and saves it */
				strncpy(pw, argv[i + 1], 49);
				result =
				    auth_change_password(userid, pw,
							 "md5");
				break;
			case '+':
				/* +5 takes a plaintext password and saves as a md5 hash */
				sprintf(pw, "%s%s%s", "$1$", cget_salt(),
					"$");
				strncpy(pw, crypt(argv[i + 1], pw), 49);
				result =
				    auth_change_password(userid, pw,
							 "md5");
				break;
			case 'd':
				/* d5 takes a md5 digest and saves it */
				strncpy(pw, argv[i + 1], 49);
				result =
				    auth_change_password(userid, pw,
							 "md5sum");
				break;
			case 'D':
				/* D5 takes a plaintext password and saves as a md5 digest */
				strncat(pw, makemd5(argv[i + 1]), 49);
				result =
				    auth_change_password(userid, pw,
							 "md5sum");
				break;

			default:
				quiet_printf
				    ("Failed: invalid option specified. Check the man page\n");
				return -1;
			}

			if (result != 0) {
				quiet_printf
				    ("\nWarning: could not change password ");
				retval = -1;
			}

			i++;
			break;

		case 'P':
			/* -P will copy password from SHADOWFILE */
			/* -P:filename will copy password from filename */
			if (argv[i][0] != '-') {
				quiet_printf
				    ("Failed: invalid option specified. Check the man page\n");
				return -1;
			}
			if (argv[i][2] == ':')
				passwdfile = &argv[i][3];
			else
				passwdfile = SHADOWFILE;


			entry = bgetpwent(passwdfile, argv[0]);
			if (!entry) {
				quiet_printf
				    ("\nWarning: error finding password from [%s] - are you superuser?\n",
				     passwdfile);
				retval = -1;
				break;
			}

			strncat(pw, entry, 50);
			if (strcmp(pw, "") == 0) {
				quiet_printf
				    ("\n%s's password not found at \"%s\" !\n",
				     argv[0], passwdfile);
				retval = -1;
			} else {
				if (strncmp(pw, "$1$", 3)) {
					if (auth_change_password
					    (userid, pw, "crypt") != 0) {
						quiet_printf
						    ("\nWarning: could not change password");
						retval = -1;
					}
				} else {
					if (auth_change_password
					    (userid, pw, "md5") != 0) {
						quiet_printf
						    ("\nWarning: could not change password");
						retval = -1;
					}
				}
			}
			break;

		case 'c':
			if (argv[i][0] != '-') {
				quiet_printf
				    ("Failed: invalid option specified. Check the man page\n");
				return -1;
			}

			newcid = strtoull(argv[i + 1], 0, 10);

			if (auth_change_clientid(userid, newcid) != 0) {
				quiet_printf
				    ("\nWarning: could not change client id ");
				retval = -1;
			}

			i++;
			break;

		case 'q':
			if (argv[i][0] != '-') {
				quiet_printf
				    ("Failed: invalid option specified. Check the man page\n");
				return -1;
			}

			newsize = strtoull(argv[i + 1], &endptr, 10);
			switch (*endptr) {
			case 'm':
			case 'M':
				newsize *= (1024 * 1024);
				break;

			case 'k':
			case 'K':
				newsize *= 1024;
				break;
			}

			if (auth_change_mailboxsize(userid, newsize) != 0) {
				quiet_printf
				    ("\nWarning: could not change max mailboxsize ");
				retval = -1;
			}

			i++;
			break;

		case 'a':
			switch (argv[i][0]) {
			case '-':
				/* remove alias */
				if (db_removealias(userid, argv[i + 1]) <
				    0) {
					quiet_printf
					    ("\nWarning: could not remove alias [%s] ",
					     argv[i + 1]);
					retval = -1;
				}
				break;
			case '+':
				/* add alias */
				auth_getclientid(userid, &client_id);
				if (db_addalias
				    (userid, argv[i + 1], client_id) < 0) {
					quiet_printf
					    ("\nWarning: could not add alias [%s]",
					     argv[i + 1]);
					retval = -1;
				}
				break;
			default:
				quiet_printf
				    ("Failed: invalid option specified. Check the man page\n");
				return -1;
			}
			i++;
			break;

		default:
			quiet_printf
			    ("invalid option specified. Check the man page\n");
			return -1;
		}

	}

	quiet_printf("Done\n");

	return retval;
}


int do_delete(char *name)
{
	int result;

	quiet_printf("Deleting user [%s]...", name);

	result = auth_delete_user(name);

	if (result < 0) {
		quiet_printf("Failed. Please check the log\n");
		return -1;
	}

	quiet_printf("Done\n");
	return 0;
}


int do_show(char *name)
{
	u64_t userid, cid, quotum, quotumused;
	struct list userlist;
	struct element *tmp;
	char *deliver_to;

	if (!name) {
		/* show all users */
		quiet_printf("Existing users:\n");

		auth_get_known_users(&userlist);

		tmp = list_getstart(&userlist);
		while (tmp) {
			quiet_printf("[%s]\n", (char *) tmp->data);
			tmp = tmp->nextnode;
		}

		if (userlist.start)
			list_freelist(&userlist.start);
	} else {
		quiet_printf("Info for user [%s]", name);

		if (auth_user_exists(name, &userid) == -1) {
			quiet_printf
			    ("\nError verifying existence of user [%s]. Please check the log.\n",
			     name);
			return -1;
		}

		if (userid == 0) {
			/* 'name' is not a user, try it as an alias */
			quiet_printf
			    ("..is not a user, trying as an alias");

			deliver_to = db_get_deliver_from_alias(name);

			if (!deliver_to) {
				quiet_printf
				    ("\nError verifying existence of alias [%s]. Please check the log.\n",
				     name);
				return -1;
			}

			if (deliver_to[0] == '\0') {
				quiet_printf("..is not an alias.\n");
				return 0;
			}

			userid = strtoul(deliver_to, NULL, 10);
			if (userid == 0) {
				quiet_printf
				    ("\n[%s] is an alias for [%s]\n", name,
				     deliver_to);
				my_free(deliver_to);
				return 0;
			}

			my_free(deliver_to);
			quiet_printf("\nFound user for alias [%s]:\n\n",
				     name);
		}

		auth_getclientid(userid, &cid);
		auth_getmaxmailsize(userid, &quotum);
		db_get_quotum_used(userid, &quotumused);

		quiet_printf("\n");
		quiet_printf("User ID         : %llu\n", userid);
		quiet_printf("Username        : %s\n",
			     auth_get_userid(userid));
		quiet_printf("Client ID       : %llu\n", cid);
		quiet_printf("Max. mailboxsize: %.02f MB\n",
			     (double) quotum / (1024.0 * 1024.0));
		quiet_printf("Quotum used     : %.02f MB (%.01f%%)\n",
			     (double) quotumused / (1024.0 * 1024.0),
			     (100.0 * quotumused) / (double) quotum);
		quiet_printf("\n");

		quiet_printf("Aliases:\n");
		db_get_user_aliases(userid, &userlist);

		tmp = list_getstart(&userlist);
		while (tmp) {
			quiet_printf("%s\n", (char *) tmp->data);
			tmp = tmp->nextnode;
		}

		quiet_printf("\n");
		if (userlist.start)
			list_freelist(&userlist.start);
	}

	return 0;
}


/*
 * empties the mailbox associated with user 'name'
 */
int do_empty(char *name)
{
	u64_t userid;
	int result;

	if (auth_user_exists(name, &userid) == -1) {
		quiet_printf("Error verifying existence of user [%s]. "
			     "Please check the log.\n", name);
		return -1;
	}

	if (userid == 0) {
		quiet_printf("User [%s] does not exist.\n", name);
		return -1;
	}

	quiet_printf("Emptying mailbox...");
	fflush(stdout);

	result = db_empty_mailbox(userid);
	if (result != 0)
		quiet_printf("Error. Please check the log.\n", name);
	else
		quiet_printf("Ok.\n");

	return result;
}


int is_valid(const char *name)
{
	int i;

	for (i = 0; name[i]; i++)
		if (strchr(ValidChars, name[i]) == NULL)
			return 0;

	return 1;
}


void show_help()
{
	printf("\n*** dbmail-adduser ***\n");

	printf
	    ("Use this program to manage the users for your dbmail system.\n");
	printf("See the man page for more info. Summary:\n\n");
	printf
	    ("dbmail-adduser [quiet] <a|d|c|s|f|x|e> [username] [options...]\n\n");

}


int quiet_printf(const char *fmt, ...)
{
	va_list argp;
	int r;

	if (quiet)
		return 0;

	va_start(argp, fmt);
	r = vprintf(fmt, argp);
	va_end(argp);

	return r;
}


/*eddy
  This two function was base from "cpu" by Blake Matheny <matheny@dbaseiv.net>
  bgetpwent : get hash password from /etc/shadow
  cget_salt : generate salt value for crypt
*/
char *bgetpwent(char *filename, char *name)
{
	FILE *passfile = NULL;
	char pass_char[512];
	int pass_size = 511;
	char *pw = NULL;
	char *user = NULL;

	if ((passfile = fopen(filename, "r")) == NULL)
		return NULL;

	while (fgets(pass_char, pass_size, passfile) != NULL) {
		char *m = pass_char;
		int num_tok = 0;
		char *toks;

		while (m != NULL && *m != 0) {
			toks = getToken(&m, ":");
			if (num_tok == 0)
				user = toks;
			else if (num_tok == 1)
				/*result->pw_passwd = toks; */
				pw = toks;
			else
				break;
			num_tok++;
		}
		if (strcmp(user, name) == 0)
			return pw;

	}
	return "";
}

char *cget_salt()
{
	unsigned long seed[2];
	const char *const seedchars =
	    "./0123456789ABCDEFGHIJKLMNOPQRST"
	    "UVWXYZabcdefghijklmnopqrstuvwxyz";
	int i;

	seed[0] = time(NULL);
	seed[1] = getpid() ^ (seed[0] >> 14 & 0x30000);
	for (i = 0; i < 8; i++)
		csalt[i] = seedchars[(seed[i / 5] >> (i % 5) * 6) & 0x3f];

	return csalt;
}


/*
  This function was base on function of "cpu"
        by Blake Matheny <matheny@dbaseiv.net>
  getToken : break down username and password from a file
*/
char *getToken(char **str, const char *delims)
{
	char *token;

	if (*str == NULL) {
		/* No more tokens */
		return NULL;
	}

	token = *str;
	while (**str != '\0') {
		if (strchr(delims, **str) != NULL) {
			**str = '\0';
			(*str)++;
			return token;
		}
		(*str)++;
	}

	/* There is no other token */
	*str = NULL;
	return token;
}
