/*
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
 * This is the dbmail-user program
 * It makes adding users easier */

#include "dbmail.h"

extern char configFile[PATH_MAX];

#define SHADOWFILE "/etc/shadow"
#define THIS_MODULE "user"

static char *getToken(char **str, const char *delims);
static char csalt[] = "........";
static char *bgetpwent(const char *filename, const char *name);
static char *cget_salt(void);

int verbose = 0;
int no_to_all = 0;
int yes_to_all = 0;
int reallyquiet = 0;
int quiet = 0;

int do_add(const char * const user,
           const char * const password, const char * const enctype,
           const uint64_t maxmail, const uint64_t clientid,
	   GList * alias_add,
	   GList * alias_del)
{
	uint64_t useridnr;
	uint64_t mailbox_idnr;
	int result;

	if (no_to_all) {
		qprintf("Pretending to add user %s with password type %s, %" PRIu64 " bytes mailbox limit and clientid %" PRIu64 "\n",
			user, enctype, maxmail, clientid);
		return 1;
	}

	TRACE(TRACE_DEBUG, "Adding user %s with password type %s,%" PRIu64 " "
		"bytes mailbox limit and clientid %" PRIu64 "... ", 
		user, enctype, maxmail, clientid);

	if ((result = auth_user_exists(user, &useridnr))) {
		qerrorf("Failed: user name already exists\n");
		return result;
	}

	if (auth_adduser(user, password, enctype, clientid, maxmail, &useridnr) < 0) {
		qerrorf("Failed: unable to create user\n");
		return -1;
	}

	TRACE(TRACE_DEBUG, "Ok, user added id [%" PRIu64 "]\n", useridnr);

	/* Add an INBOX for the user. */
	qprintf("Adding INBOX for new user... ");

	if (db_createmailbox("INBOX", useridnr, &mailbox_idnr) < 0) {
		qprintf("failed... removing user... ");
		if (auth_delete_user(user)) {
			qprintf("failed also.\n");
		} else {
			qprintf("done.\n");
		}
		return -1;
	}
	qprintf("ok.\n");

	TRACE(TRACE_DEBUG, "Ok. INBOX created for user.\n");

	if(do_aliases(useridnr, alias_add, alias_del) < 0)
		result = -1;

	do_show(user);

	return result;
}

/* Change of username */
int do_username(const uint64_t useridnr, const char * const newuser)
{
	int result = 0;

	assert(newuser);

	if (no_to_all) {
		qprintf("Pretending to change username of user id number [%" PRIu64 "] to [%s]\n",
			useridnr, newuser);
		return 1;
	}

	if (! (result = auth_change_username(useridnr, newuser)))
		qerrorf("Error: could not change username.\n");

	return result;
}

/* Change of password */
int do_password(const uint64_t useridnr,
                const char * const password, const char * const enctype)
{
	int result = 0;
	
	if (no_to_all) {
		qprintf("Pretending to change password for user id number [%" PRIu64 "]\n",
			useridnr);
		return 1;
	}

	if (! (result = auth_change_password(useridnr, password, enctype)))
		qerrorf("Error: could not change password.\n");

	return result;
}

int mkpassword(const char * const user, const char * const passwd,
               const char * const passwdtype, const char * const passwdfile,
	       char ** password, char ** enctype)
{

	pwtype pwdtype;
	int pwindex = 0;
	int result = 0;
	char *entry = NULL;
	char hashstr[FIELDSIZE];
	char pw[130];

	/* These are the easy text names. */
	const char * const pwtypes[] = {
		"plaintext",	"plaintext-raw",	"crypt",	"crypt-raw",
		"md5", 		"md5-raw",		"md5sum",	"md5sum-raw", 
		"md5-hash",	"md5-hash-raw",		"md5-digest",	"md5-digest-raw",
		"md5-base64",	"md5-base64-raw",	"md5base64",	"md5base64-raw",
		"shadow", 	"",			"whirlpool",	"sha512",
		"sha256",	"sha1",			"tiger",	NULL
	};

	/* These must correspond to the easy text names. */
	const pwtype pwtypecodes[] = {
		PLAINTEXT, 	PLAINTEXT_RAW, 		CRYPT,		CRYPT_RAW,
		MD5_HASH, 	MD5_HASH_RAW,		MD5_DIGEST,	MD5_DIGEST_RAW,
		MD5_HASH,	MD5_HASH_RAW,		MD5_DIGEST,	MD5_DIGEST_RAW,
		MD5_BASE64,	MD5_BASE64_RAW,		MD5_BASE64,	MD5_BASE64_RAW,
		SHADOW,		PLAINTEXT,		DM_WHIRLPOOL,	DM_SHA512,
		DM_SHA256,	DM_SHA1,		DM_TIGER,	PWTYPE_NULL
	};

	memset(pw, 0, 130);

	/* Only search if there's a string to compare. */
	if (passwdtype)
		/* Find a matching pwtype. */
		for (pwindex = 0; pwtypecodes[pwindex] != PWTYPE_NULL; pwindex++)
			if (strcasecmp(passwdtype, pwtypes[pwindex]) == 0)
				break;

	/* If no search took place, pwindex is 0, PLAINTEXT. */
	pwdtype = pwtypecodes[pwindex];
	switch (pwdtype) {
		case PLAINTEXT:
		case PLAINTEXT_RAW:
			null_strncpy(pw, passwd, 129);
			*enctype = "";
			break;
		case CRYPT:
			strncpy(pw, null_crypt(passwd, cget_salt()), 129);
			*enctype = "crypt";
			break;
		case CRYPT_RAW:
			null_strncpy(pw, passwd, 129);
			*enctype = "crypt";
			break;
		case MD5_HASH:
			sprintf(pw, "%s%s%s", "$1$", cget_salt(), "$");
			null_strncpy(pw, null_crypt(passwd, pw), 49);
			*enctype = "md5";
			break;
		case MD5_HASH_RAW:
			null_strncpy(pw, passwd, 129);
			*enctype = "md5";
			break;
		case MD5_DIGEST:
			memset(hashstr, 0, sizeof(hashstr));
			dm_md5(passwd, hashstr);
			strncpy(pw, hashstr, 129);
			*enctype = "md5sum";
			break;
		case MD5_DIGEST_RAW:
			null_strncpy(pw, passwd, 129);
			*enctype = "md5sum";
			break;
		case MD5_BASE64:
			dm_md5_base64(passwd, hashstr);
			strncpy(pw, hashstr, 129);
			*enctype = "md5base64";
			break;
		case MD5_BASE64_RAW:
			null_strncpy(pw, passwd, 129);
			*enctype = "md5base64";
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
                
			null_strncpy(pw, entry, 129);
			if (strcmp(pw, "") == 0) {
				qerrorf("Error: password for user [%s] not found in file [%s].\n",
				     user, passwdfile);
				result = -1;
				break;
			}

			/* Safe because we know pw is 130 long. */
			if (strncmp(pw, "$1$", 3) == 0) {
				*enctype = "md5";
			} else {
				*enctype = "crypt";
			}
			break;
		case DM_WHIRLPOOL:
			dm_whirlpool(passwd, hashstr);
			strncpy(pw, hashstr, 129);
			*enctype = "whirlpool";
			break;
		case DM_SHA512:
			dm_sha512(passwd, hashstr);
			strncpy(pw, hashstr, 129);
			*enctype = "sha512";
			break;
		case DM_SHA256:
			dm_sha256(passwd, hashstr);
			strncpy(pw, hashstr, 129);
			*enctype = "sha256";
			break;
		case DM_SHA1:
			dm_sha1(passwd, hashstr);
			strncpy(pw, hashstr, 129);
			*enctype = "sha1";
			break;
		case DM_TIGER:
			dm_tiger(passwd, hashstr);
			strncpy(pw, hashstr, 129);
			*enctype = "tiger";
			break;
		default:
			qerrorf("Error: password type not supported [%s].\n",
				passwdtype);
			result = -1;
			break;
	}

	/* Pass this out of the function. */
	*password = g_strdup(pw);

	return result;
}

/* Change of client id. */
int do_clientid(uint64_t useridnr, uint64_t clientid)
{	
	int result = 0;

	if (no_to_all) {
		qprintf("Pretending to change client for user id number [%" PRIu64 "] to client id number [%" PRIu64 "]\n",
			useridnr, clientid);
		return 1;
	}

	if ((result = auth_change_clientid(useridnr, clientid)))
		qerrorf("Warning: could not change client id\n");

	return result;
}

/* Change of quota / max mail. */
int do_maxmail(uint64_t useridnr, uint64_t maxmail)
{
	int result = 0;

	if (no_to_all) {
		qprintf("Pretending to change mail quota for user id number [%" PRIu64 "] to [%" PRIu64 "] bytes\n",
			useridnr, maxmail);
		return 1;
	}

	if (! (result = auth_change_mailboxsize(useridnr, maxmail)))
		qerrorf("Error: could not change max mail size.\n");

	return result;
}

int do_forwards(const char * const alias, const uint64_t clientid,
                GList * fwds_add,
                GList * fwds_del)
{
	int result = 0;
	char *forward;
	GList *current_fwds, *matching_fwds, *matching_fwds_del;

	if (no_to_all) {
		qprintf("Pretending to remove forwards for alias [%s]\n",
			alias);
		if (fwds_del) {
			fwds_del = g_list_first(fwds_del);
			while (fwds_del) {
				qprintf("  [%s]\n", (char *)fwds_del->data);
				fwds_del = g_list_next(fwds_del);
			}
		}
		qprintf("Pretending to add forwards for alias [%s]\n",
			alias);
		if (fwds_add) {
			fwds_add = g_list_first(fwds_add);
			while (fwds_add) {
				qprintf("  [%s]\n", (char *)fwds_add->data);
				fwds_add = g_list_next(fwds_add);
			}
		}
		return 1;
	}

	current_fwds = auth_get_aliases_ext(alias);

	/* Delete aliases for the user. */
	if (fwds_del) {
		fwds_del = g_list_first(fwds_del);
		while (fwds_del) {
			forward = (char *)fwds_del->data;

			// Look for a wildcard character
			if (strchr(forward, '?') || strchr(forward, '*')) {
				qprintf("[%s] matches:\n", forward);

				matching_fwds = match_glob_list(forward, current_fwds);

				matching_fwds_del = g_list_first(matching_fwds);
				while (matching_fwds_del) {
					forward = (char *)matching_fwds_del->data;

					qprintf("  [%s]\n", forward);

					if (auth_removealias_ext(alias, forward) < 0) {
						qerrorf("Error: could not remove forward [%s] \n", forward);
						result = -1;
					}
					if (! g_list_next(matching_fwds_del))
						break;
					matching_fwds_del = g_list_next(matching_fwds_del);
				}
			// Nope, just a standard single forward removal
			} else {
				qprintf("[%s]\n", forward);

				if (auth_removealias_ext(alias, forward) < 0) {
					qerrorf("Error: could not remove forward [%s] \n",
					     forward);
					result = -1;
				}
			}

			if (! g_list_next(fwds_del))
				break;
			fwds_del = g_list_next(fwds_del);
		}
	}

	/* Add aliases for the user. */
	if (fwds_add) {
		fwds_add = g_list_first(fwds_add);
		while (fwds_add) {
			forward = (char *)fwds_add->data;
			qprintf("[%s]\n", forward);

			if (auth_addalias_ext(alias, forward, clientid) < 0) {
				qerrorf("Error: could not add forward [%s]\n",
				     alias);
				result = -1;
			}
			if (! g_list_next(fwds_add))
				break;
			fwds_add = g_list_next(fwds_add);
		}
	}

	qprintf("Done\n");

	return result;
}

int do_aliases(const uint64_t useridnr,
               GList * alias_add,
               GList * alias_del)
{
	int result = 0;
	char *alias;
	uint64_t clientid;
	GList *current_aliases, *matching_aliases, *matching_alias_del;

	if (no_to_all) {
		if (alias_del) {
			qprintf("Pretending to remove aliases for user id number [%" PRIu64 "]\n",
				useridnr);
			alias_del = g_list_first(alias_del);
			while (alias_del) {
				qprintf("  [%s]\n", (char *)alias_del->data);
				alias_del = g_list_next(alias_del);
			}
		}
		if (alias_add) {
			qprintf("Pretending to add aliases for user id number [%" PRIu64 "]\n",
				useridnr);
			alias_add = g_list_first(alias_add);
			while (alias_add) {
				qprintf("  [%s]\n", (char *)alias_add->data);
				alias_add = g_list_next(alias_add);
			}
		}
		return 1;
	}

	auth_getclientid(useridnr, &clientid);

	current_aliases = auth_get_user_aliases(useridnr);

	/* Delete aliases for the user. */
	if (alias_del) {
		alias_del = g_list_first(alias_del);
		while (alias_del) {
			alias = (char *)alias_del->data;

			// Look for a wildcard character
			if (strchr(alias, '?') || strchr(alias, '*')) {
				qprintf("[%s] matches:\n", alias);

				matching_aliases = match_glob_list(alias, current_aliases);

				matching_alias_del = g_list_first(matching_aliases);
				while (matching_alias_del) {
					alias = (char *)matching_alias_del->data;

					qprintf("  [%s]\n", alias);

					if (auth_removealias(useridnr, alias) < 0) {
						qerrorf("Error: could not remove alias [%s] \n", alias);
						result = -1;
					}
					if (! g_list_next(matching_alias_del))
						break;
					matching_alias_del = g_list_next(matching_alias_del);
				}
			// Nope, just a standard single alias removal
			} else {
				qprintf("[%s]\n", alias);

				if (auth_removealias(useridnr, alias) < 0) {
					qerrorf("Error: could not remove alias [%s] \n", alias);
					result = -1;
				}
			}

			if (! g_list_next(alias_del))
				break;
			alias_del = g_list_next(alias_del);
		}
	}

	/* Add aliases for the user. */
	if (alias_add) {
		alias_add = g_list_first(alias_add);
		while (alias_add) {
			alias = (char *)alias_add->data;
			qprintf("[%s]\n", alias);

			if (strchr(alias, '?') || strchr(alias, '*')) {
				// Has wildcard!
			}

			if (auth_addalias (useridnr, alias, clientid) < 0) {
				qerrorf("Error: could not add alias [%s]\n", alias);
				result = -1;
			}
			if (! g_list_next(alias_add))
				break;
			alias_add = g_list_next(alias_add);
		}
	}

	qprintf("Done\n");

	return result;
}

int do_spasswd(const uint64_t useridnr, const char * const spasswd)
{
	if (no_to_all) {
		qprintf("Pretending to set security password for user [%" PRIu64 "] to [%s]\n", useridnr, spasswd);
		return 1;
	}
	return db_user_set_security_password(useridnr, spasswd);
}

int do_saction(const uint64_t useridnr, long int saction)
{
	if (no_to_all) {
		qprintf("Pretending to set security action for user [%" PRIu64 "] to [%ld]\n", useridnr, saction);
		return 1;
	}
	return db_user_set_security_action(useridnr, saction);
}

int do_enable(const uint64_t useridnr, gboolean enable)
{
	if (no_to_all) {
		qprintf("Pretending to %s authentication for user [%" PRIu64 "]\n", enable?"enable":"disable", useridnr);
		return 1;
	}
	return db_user_set_active(useridnr, enable);
}

int do_delete(const uint64_t useridnr, const char * const name)
{
	int result;
	GList *aliases = NULL;

	if (no_to_all) {
		qprintf("Pretending to delete alias [%s] for user id number [%" PRIu64 "]\n",
			name, useridnr);
		return 1;
	}

	qprintf("Deleting forwarders for user [%lu] and alias [%s]\n",useridnr,name);
	/* get all aliases for the specified userid  */
	aliases = auth_get_user_aliases(useridnr);
	
	while (aliases) {	
		char *localAlias=(char *)aliases->data;
		/* avoid numeric numeric alias */
		if ((unsigned int) strtol(localAlias, NULL, 10)!=0){
			aliases = g_list_next(aliases);
			continue;
		}
		GList * aliasesLocal = auth_get_aliases_ext(localAlias);
		qprintf("Deleting forwarders for user [%lu] and alias [%s]\n",useridnr, localAlias);
		while (aliasesLocal) {
			char *deliver_to = (char *)aliasesLocal->data;
			/* avoid numeric deliver_to */
			if ((unsigned int) strtol(deliver_to, NULL, 10)!=0){
				aliasesLocal = g_list_next(aliasesLocal);
				continue;
			}	
			qprintf("\tDeleting forward for [%s]\n",deliver_to);
			auth_removealias_ext(localAlias, deliver_to);		
			if (! g_list_next(aliasesLocal))
				break;
			aliasesLocal = g_list_next(aliasesLocal);
		}
		if (! g_list_next(aliases))
				break;
		aliases = g_list_next(aliases);
	}
	
	qprintf("Deleting aliases for user [%s]...\n", name);
	aliases = auth_get_user_aliases(useridnr);
	do_aliases(useridnr, NULL, aliases);

	
	
	qprintf("Deleting user [%s]...\n", name);
	result = auth_delete_user(name);

	if (result < 0) {
		qprintf("Failed. Please check the log\n");
		return -1;
	}

	qprintf("Done\n");
	return 0;
}

/* Set concise = 1 for passwd / aliases style output. */
static int show_alias(const char * const name, int concise);
static int show_user(uint64_t useridnr, int concise);

int do_show(const char * const name)
{
	uint64_t useridnr;
	GList *users = NULL;
	GList *aliases = NULL;

	if (!name) {
		qprintf("-- users --\n");

		/* show all users and their aliases */
		users = auth_get_known_users();
		if (g_list_length(users) > 0) {
			users = g_list_first(users);
			while (users) {
				do_show(users->data);
				if (! g_list_next(users))
					break;
				users = g_list_next(users);
			}
			g_list_foreach(users,(GFunc)g_free,NULL);
		}
		g_list_free(g_list_first(users));

		qprintf("\n-- forwards --\n");

		/* show all aliases with forwarding addresses */
		aliases = auth_get_known_aliases();
		aliases = g_list_dedup(aliases, (GCompareFunc)strcmp, TRUE);
		if (g_list_length(aliases) > 0) {
			aliases = g_list_first(aliases);
			while (aliases) {
				show_alias(aliases->data, 1);
				if (! g_list_next(aliases))
					break;
				aliases = g_list_next(aliases);
			}
			g_list_foreach(aliases,(GFunc)g_free,NULL);
		}
		g_list_free(g_list_first(aliases));
	} else {
		if (auth_user_exists(name, &useridnr)) {
			; // ignore
		}

		if (useridnr == 0) {
			return show_alias(name, 0);
		} else {
			return show_user(useridnr, 0);
		}
	}

	return 0;
}

static int show_alias(const char * const name, int concise)
{
	int result;
	char *username;
	GList *userids = NULL;
	GList *forwards = NULL;

	/* not a user, search aliases */
	result = auth_check_user_ext(name,&userids,&forwards,0);
	
	if (!result) {
		qerrorf("Nothing found searching for [%s].\n", name);
		return 1;
	}

	if (forwards) {
		if (concise) {
			GString *fwdlist = g_list_join(forwards,",");
			printf("%s: %s\n", name, fwdlist->str);
			g_string_free(fwdlist, TRUE);
		} else {
			GString *fwdlist = g_list_join(forwards,", ");
			printf("forward [%s] to [%s]\n", name, fwdlist->str);
			g_string_free(fwdlist, TRUE);
		}
		g_list_destroy(g_list_first(forwards));
	}
	
	userids = g_list_first(userids);
	if (userids) {
		while (userids) {
			username = auth_get_userid(*(uint64_t *)userids->data);
			if (!username) {
				// FIXME: This is a dangling entry. These should be removed
				// by dbmail-util. This method of identifying dangling aliases
				// should go into maintenance.c at some point.
			} else {
				if (!concise) // Don't print for concise
				printf("deliver [%s] to [%s]\n", name, username);
			}
			g_free(username);
			if (! g_list_next(userids))
				break;
			userids = g_list_next(userids);
		}
		g_list_free(g_list_first(userids));
	}
	return 0;
}

/* TODO: Non-concise output, like the old dbmail-users used to have. */
static int show_user(uint64_t useridnr, int concise UNUSED)
{
	uint64_t cid, quotum, quotumused;
	GList *userlist = NULL;
	char *username;

	auth_getclientid(useridnr, &cid);
	auth_getmaxmailsize(useridnr, &quotum);
	dm_quota_user_get(useridnr, &quotumused);
        
	GList *out = NULL;
	GString *s = g_string_new("");
	
	username = auth_get_userid(useridnr);
	out = g_list_append_printf(out,"%s", username);
	g_free(username);
	
	out = g_list_append_printf(out,"x");
	out = g_list_append_printf(out,"%" PRIu64 "", useridnr);
	out = g_list_append_printf(out,"%" PRIu64 "", cid);
	out = g_list_append_printf(out,"%.02f", 
			(double) quotum / (1024.0 * 1024.0));
	out = g_list_append_printf(out,"%.02f", 
			(double) quotumused / (1024.0 * 1024.0));
	userlist = auth_get_user_aliases(useridnr);
        
	if (g_list_length(userlist)) {
		userlist = g_list_first(userlist);
		s = g_list_join(userlist,",");
		g_list_append_printf(out,"%s", s->str);
		g_list_foreach(userlist,(GFunc)g_free, NULL);
	} else {
		g_list_append_printf(out,"");
	}
	g_list_free(g_list_first(userlist));
	s = g_list_join(out,":");
	printf("%s\n", s->str);
	g_string_free(s,TRUE);
	return 0;
}


/*
 * empties the mailbox associated with user 'name'
 */
int do_empty(uint64_t useridnr)
{
	int result = 0;

	if (yes_to_all) {
		qprintf("Emptying mailbox... ");
		fflush(stdout);
        
		result = db_empty_mailbox(useridnr, 1);
		if (result != 0) {
			qerrorf("Error. Please check the log.\n");
		} else {
			qprintf("Ok.\n");
		}
	} else {
		GList *children = NULL;
		uint64_t owner_idnr;
		char mailbox[IMAP_MAX_MAILBOX_NAMELEN];

		qprintf("You've requested to delete all mailboxes owned by user number [%" PRIu64 "]:\n", useridnr);

		db_findmailbox_by_regex(useridnr, "*", &children, 0);
		children = g_list_first(children);

		while (children) {
			uint64_t *mailbox_id = (uint64_t *)children->data;
			/* Given a list of mailbox id numbers, check if the
			 * user actually owns the mailbox (because that means
			 * it is on the hit list for deletion) and then look up
			 * and print out the name of the mailbox. */
			if (db_get_mailbox_owner(*mailbox_id, &owner_idnr)) {
				if (owner_idnr == useridnr) {
					db_getmailboxname(*mailbox_id, useridnr, mailbox);			
					qprintf("%s\n", mailbox);
				}
			}
			if (! g_list_next(children)) break;
			children = g_list_next(children);
		}

		qprintf("please run again with -y to actually perform this action.\n");
		return 1;
	}

	return result;
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
		if (strcmp(user, name) == 0) {
			fclose(passfile);
			return pw;
		}

	}
	fclose(passfile);
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

uint64_t strtomaxmail(const char * const str)
{
	uint64_t maxmail;
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

