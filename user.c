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
 * This is the dbmail-user program
 * It makes adding users easier */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "user.h"
#include "auth.h"
#include <stdio.h>
#include <termios.h>
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
char *bgetpwent(const char *filename, const char *name);
char *cget_salt(void);

/* database login data */
extern db_param_t _db_params;

/* valid characters for passwd/username */
const char ValidChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "_.!@#$%^&*()-+=~[]{}<>:;\\/";

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

#define null_strncpy(dst, src, len) \
	(src ? strncpy(dst, src, len) : 0 )

#define null_crypt(src, dst) \
	(src ? crypt(src, dst) : "" )

struct change_flags {
	unsigned int newuser         : 1;
	unsigned int newmaxmail      : 1;
	unsigned int newclientid     : 1;
	unsigned int newpasswd       : 1;
	unsigned int newpasswdfile   : 1;
	unsigned int newpasswdstdin  : 1;
	unsigned int newpasswdshadow : 1;
};

/* The prodigious use of const ensures that programming
 * mistakes inside of these functions don't cause us to
 * use incorrect values when calling auth_ and db_ internals.
 * */

/* Core operations */
int do_add(const char * const user,
           const char * const password,
           const char * const enctype,
           const u64_t maxmail, const u64_t clientid,
	   struct list * const alias_add,
	   struct list * const alias_del);
int do_delete(const char * const user);
int do_show(const char * const user);
int do_empty(const u64_t useridnr);
/* Change operations */
int do_username(const u64_t useridnr, const char *newuser);
int do_maxmail(const u64_t useridnr, const u64_t maxmail);
int do_clientid(const u64_t useridnr, const u64_t clientid);
int do_password(const u64_t useridnr,
                const char * const password,
                const char * const enctype);
int do_aliases(const u64_t useridnr,
               struct list * const alias_add,
               struct list * const alias_del);
/* External forwards */
int do_forwards(const char *alias, const u64_t clientid,
                struct list * const fwds_add,
                struct list * const fwds_del);

/* Helper functions */
int is_valid(const char * const str);
u64_t strtomaxmail(const char * const str);
int mkpassword(const char * const user, const char * const passwd,
               const char * const passwdtype, const char * const passwdfile,
               char ** password, char ** enctype);

int do_showhelp(void) {
	printf("*** dbmail-users ***\n");

	printf("Use this program to manage your DBMail users.\n");
	printf("See the man page for more info. Modes of operation:\n\n");
//	add, flush, delete, change, modify, password, alias, forward, list
	printf("     -a user   add a user\n");
	printf("     -d user   delete a user\n");
	printf("     -c user   change details for a user\n");
	printf("     -e user   empty all mailboxes for a user\n");
	printf("     -l uspec  list information for matching users\n");
	printf("     -x alias  create an external forwarding address\n");
//	printf("     -i        enter an interactive user management console\n");

	printf("\nSummary of options for all modes:\n");
	printf("     -w passwd specify user's password on the command line\n");
	printf("     -W [file] read from a file or prompt for a user's password\n");
	printf("     -p pwtype password type may be one of the following:\n"
	       "               cleartext, crypt, md5-hash, md5-digest,\n"
	       "               crypt-raw, md5-hash-raw, md5-digest-raw\n");
	printf("     -P [file] pull encrypted password from the shadow file\n");
	printf("     -u user   new username (only useful for -c, change)\n");
	printf("     -g client assign the user to a client\n");
	printf("     -m max    set the maximum mail quota in <bytes>B,\n"
	       "               <kbytes>K, or <mbytes>M, default in bytes\n"
	       "               specify 0 to remove any mail quota limits\n");
	printf("     -s alia.. adds a list of recipient aliases\n");
	printf("     -S alia.. removes a list of recipient aliases (wildcards supported)\n");
	printf("     -t fwds.. adds a list of deliver-to forwards\n");
	printf("     -T fwds.. removes a list of deliver-to forwards (wildcards supported)\n");

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
	int opt = 0, opt_prev = 0;
	int show_help = 0;
	int result = 0, mode = 0, mode_toomany = 0;
	char *user = NULL, *newuser = NULL,
	     *userspec = NULL, *alias = NULL;
	char *passwd = NULL, *passwdtype = NULL,
	     *passwdfile = NULL;
	char *password = NULL, *enctype = NULL;
	u64_t useridnr = 0, clientid = 0, maxmail = 0;
	struct list alias_add, alias_del, fwds_add, fwds_del;
	struct change_flags change_flags;
	size_t len = 0;

	list_init(&alias_add);
	list_init(&alias_del);
	list_init(&fwds_add);
	list_init(&fwds_del);

	openlog(PNAME, LOG_PID, LOG_MAIL);
	setvbuf(stdout, 0, _IONBF, 0);

	/* Set all changes to false. */
	memset(&change_flags, 0, sizeof(change_flags));

	/* get options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt(argc, argv,
		"-a:d:c:e:l::x:" /* Major modes */
		"W::w:P::p:u:g:m:t:s:S:T:" /* Minor options */
		"i" "f:qnyvVh" /* Common options */ )) != -1) {
		/* The initial "-" of optstring allows unaccompanied
		 * options and reports them as the optarg to opt 1 (not '1') */
		if (opt == 1)
			opt = opt_prev;
		opt_prev = opt;

		switch (opt) {
		/* Major modes of operation
		 * (exactly one of these is required) */
		case 'a':
		case 'd':
		case 'c':
		case 'e':
			if (mode)
				mode_toomany = 1;
			mode = opt;
			if (optarg && strlen(optarg))
				user = optarg;
			break;

		case 'x':
			if (mode)
				mode_toomany = 1;
			mode = opt;
			if (optarg && strlen(optarg))
				alias = optarg;
			break;

		case 'l':
			/* It seems that the optional argument may
			 * be passed as a second instance of this flag. */
			if (mode != 0 && mode != 'l')
				mode_toomany = 1;
			mode = opt;
			if (optarg && strlen(optarg))
				userspec = optarg;
			break;

		case 'i':
			printf("Interactive console is not supported in this release.\n");
			return 1;


		/* Minor options */
		case 'w':
			change_flags.newpasswd = 1;
			passwd = optarg;
			break;

		case 'W':
			change_flags.newpasswd = 1;
			if (optarg && strlen(optarg)) {
				passwdfile = optarg;
				change_flags.newpasswdfile = 1;
			} else {
				change_flags.newpasswdstdin = 1;
			}
			break;

		case 'p':
			if (!passwdtype)
				passwdtype = optarg;
			// else
				// Complain about only one type allowed.
			break;

		case 'P':
			change_flags.newpasswdshadow = 1;
			if (optarg && strlen(optarg))
				passwdfile = optarg;
			else
				passwdfile = SHADOWFILE;
			passwdtype = "shadow";
			break;

		case 'g':
			change_flags.newclientid = 1;
			clientid = strtoull(optarg, NULL, 10);
			break;

		case 'm':
			change_flags.newmaxmail = 1;
			maxmail = strtomaxmail(optarg);
			break;

		case 's':
			// Add this item to the user's aliases.
			if (optarg && (len = strlen(optarg)))
				list_nodeadd(&alias_add, optarg, len+1);
			break;

		case 'S':
			// Delete this item from the user's aliases.
			if (optarg && (len = strlen(optarg)))
				list_nodeadd(&alias_del, optarg, len+1);
			break;

		case 't':
			// Add this item to the alias's forwards.
			if (optarg && (len = strlen(optarg)))
				list_nodeadd(&fwds_add, optarg, len+1);
			break;

		case 'T':
			// Delete this item from the alias's forwards.
			if (optarg && (len = strlen(optarg)))
				list_nodeadd(&fwds_del, optarg, len+1);
			break;

		/* Common options */
		case 'f':
			if (optarg && strlen(optarg) > 0)
				configFile = optarg;
			else {
				qerrorf("dbmail-users: -f requires a filename\n\n");
				result = 1;
			}
			break;

		case 'h':
			show_help = 1;
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
			printf("\n*** DBMAIL: dbmail-users version "
			       "$Revision$ %s\n\n", COPYRIGHT);
			result = 1;
			break;

		default:
			/* printf("unrecognized option [%c], continuing...\n",optopt); */
			break;
		}

		/* If there's a non-negative return code,
		 * it's time to free memory and bail out. */
		if (result)
			goto freeall;
	}	

	/* If nothing is happening, show the help text. */
	if (!mode || mode_toomany || show_help) {
		do_showhelp();
		result = 1;
		goto freeall;
	}

	/* read the config file */
	ReadConfig("DBMAIL", configFile);
	SetTraceLevel("DBMAIL");
	GetDBParams(&_db_params);

	/* open database connection */
	qprintf("Opening connection to database...\n");
	if (db_connect() != 0) {
		qerrorf
		    ("Failed. Could not connect to database (check log)\n");
		result = -1;
		goto freeall;
	}

	/* open authentication connection */
	qprintf("Opening connection to authentication...\n");
	if (auth_connect() != 0) {
		qerrorf
		    ("Failed. Could not connect to authentication (check log)\n");
		result = -1;
		goto freeall;
	}

	qprintf("Ok. Connected\n");
	configure_debug(TRACE_ERROR, 1, 0);

	switch (mode) {
	case 'c':
	case 'd':
	case 'e':
		/* Verify the existence of this user */
		if (auth_user_exists(user, &useridnr) == -1) {
			qerrorf("Error: cannot verify existence of user [%s].\n",
			     user);
			result = -1;
			goto freeall;
		}
		if (useridnr == 0) {
			qerrorf("Error: user [%s] does not exist.\n",
				     user);
			result = -1;
			goto freeall;
		}
	}

	/* Only get a password for those modes which require it. */
	switch (mode) {
	case 'a':
	case 'c':
		if (change_flags.newpasswdstdin) {
			char pw[50];
			struct termios oldattr, newattr;

			/* Get the current terminal state, then disable echo. */
			tcgetattr(fileno(stdin), &oldattr);
			newattr = oldattr;
			newattr.c_lflag &= ~ECHO;
			tcsetattr(fileno(stdin), TCSAFLUSH, &newattr);

			/* Prompt for a password and read until \n or EOF. */
			qprintf("Please enter a password (will not echo): ");
			fflush(stdout);
			fgets(pw, 50, stdin);

			/* We don't want the trailing newline. */
			len = strlen(pw);
			if (pw[len-1] == '\n')
			        pw[len-1] = '\0';
			/* fgets guarantees a nul terminated string. */
			passwd = strdup(pw);

			/* Restore the previous terminal state (with echo back on). */
			tcsetattr(fileno(stdin), TCSANOW, &oldattr);
			qprintf("\n");
		}

		/* Do we need the password for this mode? */
		if (mkpassword(user, passwd, passwdtype, passwdfile,
		               &password, &enctype)) {
			qerrorf("Error: unable to create a password.\n");
			result = -1;
			goto freeall;
		}
	}


	switch (mode) {
	case 'a':
		result = do_add(user, password, enctype, maxmail, clientid,
				&alias_add, &alias_del);
		break;
	case 'd':
		result = do_delete(user);
		break;
	case 'c':
		qprintf("Performing changes for user [%s]...\n", user);
		if (change_flags.newuser) {
			result |= do_username(useridnr, newuser);
		}
		if (change_flags.newpasswd) {
			result |= do_password(useridnr, password, enctype);
		}
		if (change_flags.newclientid) {
			result |= do_clientid(useridnr, clientid);
		}
		if (change_flags.newmaxmail) {
			result |= do_maxmail(useridnr, maxmail);
		}
		result |= do_aliases(useridnr, &alias_add, &alias_del);
		break;
	case 'e':
		result = do_empty(useridnr);
		break;
	case 'l':
		result = do_show(userspec);
		break;
	case 'x':
		result = do_forwards(alias, clientid, &fwds_add, &fwds_del);
		break;
	default:
		result = 1;
	}

	/* Here's where we free memory and quit.
	 * Be sure that all of these are NULL safe! */
freeall:

	/* Free the lists. */
	if (alias_del.start)
		list_freelist(&alias_del.start);
	if (alias_add.start)
		list_freelist(&alias_add.start);
	if (fwds_del.start)
		list_freelist(&alias_del.start);
	if (fwds_add.start)
		list_freelist(&alias_add.start);

	db_disconnect();
	auth_disconnect();
	config_free();

	if (result < 0)
		qerrorf("Command failed.\n");
	return result;
}

int do_add(const char * const user,
           const char * const password, const char * const enctype,
           const u64_t maxmail, const u64_t clientid,
	   struct list * const alias_add,
	   struct list * const alias_del)
{
	u64_t useridnr;
	u64_t mailbox_idnr;
	int add_user_result, result;

	if (!is_valid(user)) {
		qerrorf("Error: invalid characters in username [%s]\n",
		     user);
		return -1;
	}

	qprintf("Adding user %s with password type %s,"
	     "%llu bytes mailbox limit and clientid %llu... ",
	     user, enctype, maxmail, clientid);

	switch (auth_user_exists(user, &useridnr))
	{
	case -1:
		/* Database failure */
		qerrorf("Failed\n\nCheck logs for details\n\n");
		return -1;
	default:
		if (useridnr != 0) {
			qprintf("Failed: user exists [%llu]\n",
				     useridnr);
			return -1;
		} else {
			/* If useridnr is 0, create the user */
			add_user_result = auth_adduser(user, password, enctype,
				clientid, maxmail, &useridnr);
		}
		break;
	}

	qprintf("Ok, user added id [%llu]\n", useridnr);

	/* Add an INBOX for the user. */
	qprintf("Adding INBOX for new user\n");
	switch(db_createmailbox("INBOX", useridnr, &mailbox_idnr)) {
	case -1:
		qprintf("Failed.. User is added but we failed to add "
			     "the mailbox INBOX for this user\n");
		result = -1;
		break;
	case 0:
	default:
		qprintf("Ok. added\n");
		result = 0;
		break;
	} 

	if(do_aliases(useridnr, alias_add, alias_del) < 0)
		result = -1;

	return result;
}

/* Change of username */
int do_username(const u64_t useridnr, const char * const newuser)
{
	int result = 0;

	if (newuser && is_valid(newuser)) {
		if (auth_change_username(useridnr, newuser) != 0) {
			qerrorf("Error: could not change username.\n");
			result = -1;
		}
	} else {
		qerrorf("Error: new username contains invalid characters.\n");
		result = -1;
	}

	return result;
}

/* Change of password */
int do_password(const u64_t useridnr,
                const char * const password, const char * const enctype)
{
	int result = 0;

	result = auth_change_password(useridnr, password, enctype);
	if (result != 0) {
		qerrorf("Error: could not change password.\n");
	}

	return result;
}

/* These are the available password types. */
typedef enum {
	PLAINTEXT = 0, PLAINTEXT_RAW, CRYPT, CRYPT_RAW,
	MD5_HASH, MD5_HASH_RAW, MD5_DIGEST, MD5_DIGEST_RAW,
	SHADOW, PWTYPE_NULL
} pwtype_t;

/* These are the easy text names. */
static const char * const pwtypes[] = {
	"plaintext", "plaintext-raw", "crypt", "crypt-raw",
	"md5", "md5-raw", "md5sum", "md5sum-raw",
	"md5-hash", "md5-hash-raw", "md5-digest", "md5-digest-raw",
	"shadow", NULL
};

/* These must correspond to the easy text names. */
static const pwtype_t pwtypecodes[] = {
	PLAINTEXT, PLAINTEXT_RAW, CRYPT, CRYPT_RAW,
	MD5_HASH, MD5_HASH_RAW, MD5_DIGEST, MD5_DIGEST_RAW,
	MD5_HASH, MD5_HASH_RAW, MD5_DIGEST, MD5_DIGEST_RAW,
	SHADOW, PWTYPE_NULL
};

int mkpassword(const char * const user, const char * const passwd,
               const char * const passwdtype, const char * const passwdfile,
	       char ** password, char ** enctype)
{

	pwtype_t pwtype;
	int pwindex = 0;
	int result = 0;
	char *entry = NULL;
	char pw[50];

	memset(pw, 0, 50);

	/* Only search if there's a string to compare. */
	if (passwdtype)
		/* Find a matching pwtype. */
		for (pwindex = 0; pwtypecodes[pwindex] < PWTYPE_NULL; pwindex++)
			if (strcasecmp(passwdtype, pwtypes[pwindex]) == 0)
				break;

	/* If no search took place, pwindex is 0, PLAINTEXT. */
	pwtype = pwtypecodes[pwindex];
	switch (pwtype) {
		case PLAINTEXT:
		case PLAINTEXT_RAW:
			null_strncpy(pw, passwd, 49);
			*enctype = "";
			break;
		case CRYPT:
			strcat(pw, null_crypt(passwd, cget_salt()));
			*enctype = "crypt";
			break;
		case CRYPT_RAW:
			null_strncpy(pw, passwd, 49);
			*enctype = "crypt";
			break;
		case MD5_HASH:
			sprintf(pw, "%s%s%s", "$1$", cget_salt(), "$");
			null_strncpy(pw, null_crypt(passwd, pw), 49);
			*enctype = "md5";
			break;
		case MD5_HASH_RAW:
			null_strncpy(pw, passwd, 49);
			*enctype = "md5";
			break;
		case MD5_DIGEST:
			strncat(pw, makemd5(passwd), 49);
			*enctype = "md5sum";
			break;
		case MD5_DIGEST_RAW:
			null_strncpy(pw, passwd, 49);
			*enctype = "md5sum";
			break;
		case SHADOW:
			entry = bgetpwent(passwdfile, user);
			if (!entry) {
				qerrorf("Error: cannot read file [%s], "
					"please make sure that you have "
					"permission to read this file.\n",
					passwdfile);
				result = -1;
				break;
			}
                
			strncat(pw, entry, 49);
			if (strcmp(pw, "") == 0) {
				qerrorf("Error: password for user [%s] not found in file [%s].\n",
				     user, passwdfile);
				result = -1;
				break;
			}

			/* Safe because we know pw is 50 long. */
			if (strncmp(pw, "$1$", 3) == 0) {
				*enctype = "md5";
			} else {
				*enctype = "crypt";
			}
			break;
		default:
			qerrorf("Error: password type not supported [%s].\n",
				passwdtype);
			result = -1;
			break;
	}

	/* Pass this out of the function. */
	*password = strdup(pw);

	return result;
}

/* Change of client id. */
int do_clientid(u64_t useridnr, u64_t clientid)
{	
	int result = 0;

	if (auth_change_clientid(useridnr, clientid) != 0) {
		qprintf("\nWarning: could not change client id ");
		result = -1;
	}

	return result;
}

/* Change of quota / max mail. */
int do_maxmail(u64_t useridnr, u64_t maxmail)
{
	int result = 0;

	if (auth_change_mailboxsize(useridnr, maxmail) != 0) {
		qerrorf("Error: could not change max mail size.\n");
		result = -1;
	}

	return result;
}

int do_forwards(const char * const alias, const u64_t clientid,
                struct list * const fwds_add,
                struct list * const fwds_del)
{
	int result = 0;
	char *forward;
	struct element *tmp;

	/* Delete aliases for the user. */
	if (fwds_del) {
		tmp = list_getstart(fwds_del);
		while (tmp) {
			forward = (char *)tmp->data;

			qprintf("[%s]\n", forward);

			if (db_removealias_ext(alias, forward) < 0) {
				qerrorf("Error: could not remove forward [%s] \n",
				     forward);
				result = -1;
			}
			tmp = tmp->nextnode;
		}
	}

	/* Add aliases for the user. */
	if (fwds_add) {
		tmp = list_getstart(fwds_add);
		while (tmp) {
			forward = (char *)tmp->data;
			qprintf("[%s]\n", forward);

			if (db_addalias_ext(alias, forward, clientid) < 0) {
				qerrorf("Error: could not add forward [%s]\n",
				     alias);
				result = -1;
			}
			tmp = tmp->nextnode;
		}
	}

	/*
	qprintf("Adding alias [%s] --> [%s]...", alias, forward);
	switch ((result = db_addalias_ext(alias, forward, 0))) {
	case -1:
		qerrorf("Error: cannot add forwarding address.\n");
		break;

	case 0:
		qprintf("Ok. Forwarding address added.\n");
		break;

	case 1:
		qprintf("Already exists. no extra alias added\n");
		result = -1;	/ * return error * /
		break;

	}
	*/

	qprintf("Done\n");

	return result;
}

int do_aliases(const u64_t useridnr,
               struct list * const alias_add,
               struct list * const alias_del)
{
	int result = 0;
	u64_t clientid;

	auth_getclientid(useridnr, &clientid);

	/* Delete aliases for the user. */
	if (alias_del) {
		char *alias;
		struct element *tmp;

		tmp = list_getstart(alias_del);
		while (tmp) {
			alias = (char *)tmp->data;

			qprintf("[%s]\n", alias);

			if (db_removealias(useridnr, alias) <
			    0) {
				qerrorf("Error: could not remove alias [%s] \n",
				     alias);
				result = -1;
			}
			tmp = tmp->nextnode;
		}
	}

	/* Add aliases for the user. */
	if (alias_add) {
		char *alias;
		struct element *tmp;
		

		tmp = list_getstart(alias_add);
		while (tmp) {
			alias = (char *)tmp->data;
			qprintf("[%s]\n", alias);

			if (db_addalias
			    (useridnr, alias, clientid) < 0) {
				qerrorf("Error: could not add alias [%s]\n",
				     alias);
				result = -1;
			}
			tmp = tmp->nextnode;
		}
	}

	qprintf("Done\n");

	return result;
}


int do_delete(const char * const name)
{
	int result;

	qprintf("Deleting user [%s]...", name);

	result = auth_delete_user(name);

	if (result < 0) {
		qprintf("Failed. Please check the log\n");
		return -1;
	}

	qprintf("Done\n");
	return 0;
}

int do_show(const char * const name)
{
	u64_t useridnr, cid, quotum, quotumused;
	struct list userlist;
	struct element *tmp;
	char *deliver_to;

	if (!name) {
		/* show all users */
		qprintf("Existing users:\n");

		auth_get_known_users(&userlist);

		tmp = list_getstart(&userlist);
		while (tmp) {
			qprintf("[%s]\n", (char *) tmp->data);
			tmp = tmp->nextnode;
		}

		if (userlist.start)
			list_freelist(&userlist.start);
	} else {
		qprintf("Info for user [%s]", name);

		if (auth_user_exists(name, &useridnr) == -1) {
			qerrorf("Error: cannot verify existence of user [%s].\n",
			     name);
			return -1;
		}

		if (useridnr == 0) {
			/* 'name' is not a user, try it as an alias */
			qprintf
			    ("..is not a user, trying as an alias");

			deliver_to = db_get_deliver_from_alias(name);

			if (!deliver_to) {
				qerrorf("Error: cannot verify existence of alias [%s].\n",
				     name);
				return -1;
			}

			if (deliver_to[0] == '\0') {
				qprintf("..is not an alias.\n");
				return 0;
			}

			useridnr = strtoul(deliver_to, NULL, 10);
			if (useridnr == 0) {
				qprintf
				    ("\n[%s] is an alias for [%s]\n", name,
				     deliver_to);
				my_free(deliver_to);
				return 0;
			}

			my_free(deliver_to);
			qprintf("\nFound user for alias [%s]:\n\n",
				     name);
		}

		auth_getclientid(useridnr, &cid);
		auth_getmaxmailsize(useridnr, &quotum);
		db_get_quotum_used(useridnr, &quotumused);

		qprintf("\n");
		qprintf("User ID         : %llu\n", useridnr);
		qprintf("Username        : %s\n",
			     auth_get_userid(useridnr));
		qprintf("Client ID       : %llu\n", cid);
		qprintf("Max. mailboxsize: %.02f MB\n",
			     (double) quotum / (1024.0 * 1024.0));
		qprintf("Quotum used     : %.02f MB (%.01f%%)\n",
			     (double) quotumused / (1024.0 * 1024.0),
			     (100.0 * quotumused) / (double) quotum);
		qprintf("\n");

		qprintf("Aliases:\n");
		db_get_user_aliases(useridnr, &userlist);

		tmp = list_getstart(&userlist);
		while (tmp) {
			qprintf("%s\n", (char *) tmp->data);
			tmp = tmp->nextnode;
		}

		qprintf("\n");
		if (userlist.start)
			list_freelist(&userlist.start);
	}

	return 0;
}


/*
 * empties the mailbox associated with user 'name'
 */
int do_empty(u64_t useridnr)
{
	int result;

	qprintf("Emptying mailbox...");
	fflush(stdout);

	result = db_empty_mailbox(useridnr);
	if (result != 0)
		qerrorf("Error. Please check the log.\n");
	else
		qprintf("Ok.\n");

	return result;
}


int is_valid(const char *str)
{
	int i;

	for (i = 0; str[i]; i++)
		if (strchr(ValidChars, str[i]) == NULL)
			return 0;

	return 1;
}

/*eddy
  This two function was base from "cpu" by Blake Matheny <matheny@dbaseiv.net>
  bgetpwent : get hash password from /etc/shadow
  cget_salt : generate salt value for crypt
*/
char *bgetpwent(const char *filename, const char *name)
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

u64_t strtomaxmail(const char * const str)
{
	u64_t maxmail;
	char *endptr = NULL;

	maxmail = strtoull(str, &endptr, 10);
	switch (*endptr) {
	case 'g':
	case 'G':
		maxmail *= (1024 * 1024 * 1024);
		break;

	case 'm':
	case 'M':
		maxmail *= (1024 * 1024);
		break;

	case 'k':
	case 'K':
		maxmail *= 1024;
		break;
	}

	return maxmail;
}

