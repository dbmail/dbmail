/* $Id$

 Copyright (C) 1999-2004 Aaron Stone aaron at serendipity dot cx

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

 *
 * Functions for running user defined sorting rules
 * on a message in the temporary store, usually
 * just delivering the message to the user's INBOX
 * ...unless they have fancy rules defined, that is :-)
 * 
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>
#include <ctype.h>
#include "db.h"
#include "auth.h"
#include "debug.h"
#include "list.h"
#include "dbmail.h"
#include "debug.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "dbmd5.h"
#include "misc.h"

#include "sortsieve.h"
#include "sort.h"
#include <sieve2.h>

extern struct dm_list smtpItems, sysItems;

/* Used by us to keep track of libSieve. */
struct my_context {
	char *s_buf;
	char *script;
	char *header;
	char *errormsg;
	u64_t user_idnr;
	u64_t headersize;
	u64_t messagesize;
	int error_runtime;
	int error_parse;
	int actiontaken;
	struct dm_list *actionlist;
};


/* typedef sort_action {
 *   int method,
 *   char *destination,
 *   char *message
 * } sort_action_t;
 * */

/* SIEVE CALLBACKS */

int my_notify(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;
	const char * const * options;
	int i;

	printf( "Action is NOTIFY: \n" );
	printf( "  ID \"%s\" is %s\n",
		sieve2_getvalue_string(s, "id"),
		sieve2_getvalue_string(s, "active"));
	printf( "    Method is %s\n",
		sieve2_getvalue_string(s, "method"));
	printf( "    Priority is %s\n",
		sieve2_getvalue_string(s, "priority"));
	printf( "    Message is %s\n",
		sieve2_getvalue_string(s, "message"));

	options = sieve2_getvalue_stringlist(s, "options");
	if (!options)
		return SIEVE2_ERROR_BADARGS;
	for (i = 0; options[i] != NULL; i++) {
		printf( "    Options are %s\n", options[i] );
	}

	m->actiontaken = 1;
	return SIEVE2_OK;
}

int my_vacation(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;
	int yn;

	/* Ask for the message hash, the days parameters, etc. */
	printf("Have I already responded to '%s' in the past %d days?\n",
		sieve2_getvalue_string(s, "hash"),
		sieve2_getvalue_int(s, "days") );

	yn = getchar();

	/* Check in our 'database' to see if there's a match. */
	if (yn == 'y' || yn == 'Y') {
		printf( "Ok, not sending a vacation response.\n" );
	}

	/* If so, do nothing. If not, send a vacation and log it. */
	printf("echo '%s' | mail -s '%s' '%s' for message '%s'\n",
		sieve2_getvalue_string(s, "message"),
		sieve2_getvalue_string(s, "subject"),
		sieve2_getvalue_string(s, "address"),
		sieve2_getvalue_string(s, "name") );

	m->actiontaken = 1;
	return SIEVE2_OK;
}

int my_redirect(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;

	printf( "Action is REDIRECT: \n" );
	printf( "  Destination is [%s]\n",
		sieve2_getvalue_string(s, "address"));

	m->actiontaken = 1;
	return SIEVE2_OK;
}

int my_reject(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;

	printf( "Action is REJECT: \n" );
	printf( "  Message is [%s]\n",
		sieve2_getvalue_string(s, "message"));

	m->actiontaken = 1;
	return SIEVE2_OK;
}

int my_discard(sieve2_context_t *s UNUSED, void *my)
{
	struct my_context *m = (struct my_context *)my;

	printf( "Action is DISCARD\n" );

	m->actiontaken = 1;
	return SIEVE2_OK;
}

int my_fileinto(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;
	const char * const * flags;
	int i;

	printf( "Action is FILEINTO: \n" );
	printf( "  Destination is %s\n",
		sieve2_getvalue_string(s, "mailbox"));
	flags = sieve2_getvalue_stringlist(s, "imapflags");
	if (flags) {
		printf( "  Flags are:");
		for (i = 0; flags[i]; i++)
			printf( " %s", flags[i]);
		printf( ".\n");
	} else {
			printf( "  No flags specified.\n");
	}

	m->actiontaken = 1;
	return SIEVE2_OK;
}

int my_keep(sieve2_context_t *s UNUSED, void *my)
{
	struct my_context *m = (struct my_context *)my;

	printf( "Action is KEEP\n" );

	m->actiontaken = 1;
	return SIEVE2_OK;
}

int my_errparse(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;

	printf( "Error is PARSE: " );
	printf( "  Line is %d\n",
		sieve2_getvalue_int(s, "lineno"));
	printf( "  Message is %s\n",
		sieve2_getvalue_string(s, "message"));

	m->error_parse = 1;
	return SIEVE2_OK;
}

int my_errexec(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;

	printf( "Error is EXEC: " );
	printf( "  Message is %s\n",
		sieve2_getvalue_string(s, "message"));

	m->error_runtime = 1;
	return SIEVE2_OK;
}

int my_getscript(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;
	const char * path, * name;
	int res;

	/* Path could be :general, :personal, or empty. */
	path = sieve2_getvalue_string(s, "path");

	/* If no file is named, we're looking for the main file. */
	name = sieve2_getvalue_string(s, "name");

	if (path == NULL || name == NULL)
		return SIEVE2_ERROR_BADARGS;

	if (strlen(path) && strlen(name)) {
		printf("Include requested from '%s' named '%s'\n",
			path, name);
	} else
	if (!strlen(path) && !strlen(name)) {
		/* Read the script file given as an argument. */
		res = db_get_sievescript_byname(m->user_idnr, m->script, &m->s_buf);
		if (res != SIEVE2_OK) {
			printf("my_getscript: read_file() returns %d\n", res);
			return SIEVE2_ERROR_FAIL;
		}
		sieve2_setvalue_string(s, "script", m->s_buf);
	} else {
		return SIEVE2_ERROR_BADARGS;
	}

	return SIEVE2_OK;
}

// TODO: Use GMime to hand pre-parsed headers to libSieve
// on an as-needed basis using the my_getheader callback.
int my_getheaders(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;

	sieve2_setvalue_string(s, "allheaders", m->header);

	return SIEVE2_OK;
}

int my_getenvelope(sieve2_context_t *s UNUSED, void *my UNUSED)
{
	return SIEVE2_ERROR_UNSUPPORTED;
}

int my_getbody(sieve2_context_t *s UNUSED, void *my UNUSED)
{
	return SIEVE2_ERROR_UNSUPPORTED;
}

int my_getsize(sieve2_context_t *s, void *my)
{
	struct my_context *m = (struct my_context *)my;

	sieve2_setvalue_int(s, "size", m->messagesize);

	return SIEVE2_OK;
}

/* END OF CALLBACKS */


/* FIXME: Will this work outside of a function scope? */
sieve2_callback_t my_callbacks[] = {
	{ SIEVE2_ERRCALL_RUNTIME,       my_errexec     },
	{ SIEVE2_ERRCALL_PARSE,         my_errparse    },
	{ SIEVE2_ACTION_FILEINTO,       my_fileinto    },
	{ SIEVE2_ACTION_REDIRECT,       my_redirect    },
	{ SIEVE2_ACTION_REJECT,         my_reject      },
	{ SIEVE2_ACTION_NOTIFY,         my_notify      },
	{ SIEVE2_ACTION_VACATION,       my_vacation    },
	{ SIEVE2_ACTION_KEEP,           my_keep        },
	{ SIEVE2_SCRIPT_GETSCRIPT,      my_getscript   },
	/* We don't support one header at a time in this example. */
// TODO: Use GMime to hand pre-parsed headers to libSieve
// on an as-needed basis using the my_getheader callback.
	{ SIEVE2_MESSAGE_GETHEADER,     NULL            },
	/* libSieve can parse headers itself, so we'll use that. */
	{ SIEVE2_MESSAGE_GETALLHEADERS, my_getheaders  },
	{ SIEVE2_MESSAGE_GETENVELOPE,   my_getenvelope },
	{ SIEVE2_MESSAGE_GETBODY,       my_getbody     },
	{ SIEVE2_MESSAGE_GETSIZE,       my_getsize     },
	{ 0, 0 } };


/* Return 0 on script OK, 1 on script error, 2 on misc error. */
int sortsieve_script_validate(u64_t user_idnr, char *scriptname, char **errormsg)
{
	int res, exitcode = 0;
	struct my_context *my_context;
	sieve2_context_t *sieve2_context;

	/* The contents of this function are taken from
	 * the libSieve distribution, sv_test/example.c,
	 * and are provided under an "MIT style" license.
	 * */

	/* This is the locally-defined structure that will be
	 * passed as the user context into the sieve calls.
	 * It will be passed by libSieve into each callback.*/
	my_context = malloc(sizeof(struct my_context));
	if (!my_context) {
		exitcode = 2;
		goto endnofree;
	}
	memset(my_context, 0, sizeof(struct my_context));

	my_context->script = scriptname;
	my_context->user_idnr = user_idnr;

	res = sieve2_alloc(&sieve2_context);
	if (res != SIEVE2_OK) {
		printf("Error %d when calling sieve2_alloc: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
		goto freesieve;
	}

	res = sieve2_callbacks(sieve2_context, my_callbacks);
	if (res != SIEVE2_OK) {
		printf("Error %d when calling sieve2_callbacks: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
		goto freesieve;
	}

	res = sieve2_validate(sieve2_context, my_context);
	if (res != SIEVE2_OK) {
		printf("Error %d when calling sieve2_validate: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
	}

	/* At this point the callbacks are called from within libSieve. */

	exitcode |= my_context->error_parse;
	exitcode |= my_context->error_runtime;
	*errormsg = my_context->errormsg;

freesieve:
	res = sieve2_free(&sieve2_context);
	if (res != SIEVE2_OK) {
		printf("Error %d when calling sieve2_free: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
	}

	if (my_context->s_buf) free(my_context->s_buf);

	if (my_context) free(my_context);

endnofree:
	return exitcode;
}

/* Pull up the relevant sieve scripts for this
 * user and begin running them against the header
 * and possibly the body of the message.
 *
 * Returns 0 on success, -1 on failure,
 * and +1 on success but with memory leaking.
 * In the +1 case, if called from a daemon
 * such as dbmail-lmtpd, the daemon should
 * finish storing the message and restart.
 * */
int sortsieve_msgsort(u64_t user_idnr, char *header, u64_t headersize,
		      u64_t messagesize, struct dm_list *actions)
{
	int res, exitcode = 0;
	struct my_context *my_context;
	sieve2_context_t *sieve2_context;

	/* The contents of this function are taken from
	 * the libSieve distribution, sv_test/example.c,
	 * and are provided under an "MIT style" license.
	 * */

	/* This is the locally-defined structure that will be
	 * passed as the user context into the sieve calls.
	 * It will be passed by libSieve into each callback.*/
	my_context = malloc(sizeof(struct my_context));
	if (!my_context) {
		exitcode = 2;
		goto endnofree;
	}
	memset(my_context, 0, sizeof(struct my_context));

	my_context->user_idnr = user_idnr;
	my_context->header = header;
	my_context->headersize = headersize;
	my_context->messagesize = messagesize;
	my_context->actionlist = actions;

	res = db_get_sievescript_active(user_idnr, &my_context->script);
	if (res != 0) {
		printf("Error %d when calling db_getactive_sievescript\n", res);
		exitcode = 1;
		goto freesieve;
	}

	res = sieve2_alloc(&sieve2_context);
	if (res != SIEVE2_OK) {
		printf("Error %d when calling sieve2_alloc: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
		goto freesieve;
	}

	res = sieve2_callbacks(sieve2_context, my_callbacks);
	if (res != SIEVE2_OK) {
		printf("Error %d when calling sieve2_callbacks: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
		goto freesieve;
	}

	res = sieve2_execute(sieve2_context, my_context);
	if (res != SIEVE2_OK) {
		printf("Error %d when calling sieve2_execute: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
	}
	if (!my_context->actiontaken) {
		printf("  no actions taken; keeping message.\n");
		my_keep(NULL, my_context);
	}

	/* At this point the callbacks are called from within libSieve. */

	exitcode |= my_context->error_parse;
	exitcode |= my_context->error_runtime;

freesieve:
	res = sieve2_free(&sieve2_context);
	if (res != SIEVE2_OK) {
		printf("Error %d when calling sieve2_free: %s\n",
			res, sieve2_errstr(res));
		exitcode = 1;
	}

	if (my_context->s_buf) free(my_context->s_buf);

	if (my_context) free(my_context);

endnofree:
	return exitcode;
}

