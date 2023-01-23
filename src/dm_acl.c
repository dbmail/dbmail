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

#include "dbmail.h"

#define NR_ACL_FLAGS 13	// Need enough space for our 11 real rights and the old RFC 2086 virtual c and d rights
#define THIS_MODULE "acl"

static const char *acl_right_strings[] = {
	"lookup_flag",
	"read_flag",
	"seen_flag",
	"write_flag",
	"insert_flag",
	"post_flag",
	"create_flag",
	"delete_flag",
	"deleted_flag",
	"expunge_flag",
	"administer_flag",
	"create_flag",
	"delete_flag"
};

static const char acl_right_chars[] = "lrswipkxteacd";
//{'l','r','s','w','i','p','k','x','t','e','a','c','d'};

/* local functions */
static ACLRight acl_get_right_from_char(char right_char);
static int acl_change_rights(uint64_t userid, uint64_t mboxid,
			     const char *rightsstring, int set);
static int acl_replace_rights(uint64_t userid, uint64_t mboxid,
			      const char *rightsstring);
static int acl_set_one_right(uint64_t userid, uint64_t mboxid,
			     ACLRight right, int set);
static int acl_get_rightsstring_identifier(char *identifier, uint64_t mboxid,
					   /*@out@*/ char *rightsstring);
static int acl_get_rightsstring(uint64_t userid, uint64_t mboxid,
				/*@out@*/ char *rightsstring);


int acl_has_right(MailboxState_T S, uint64_t userid, ACLRight right)
{
	uint64_t anyone_userid;
	int test;
	
	switch(right) {
		case ACL_RIGHT_SEEN:
		case ACL_RIGHT_WRITE:
		case ACL_RIGHT_INSERT:
		case ACL_RIGHT_POST:
		case ACL_RIGHT_CREATE:
		case ACL_RIGHT_DELETE:
		case ACL_RIGHT_DELETED:
		case ACL_RIGHT_EXPUNGE:
		case ACL_RIGHT_ADMINISTER:

			if (MailboxState_getPermission(S) != IMAPPERM_READWRITE)
				return FALSE;
		break;

		case ACL_RIGHT_LOOKUP:
		case ACL_RIGHT_READ:
		case ACL_RIGHT_NONE:
			/* Write access not required;
			 * check these by flags. */
		break;
	}

	const char *right_flag = acl_right_strings[right];

	/* Check if the user has the right; this will also
	 * return true if the user is the mailbox owner. */
	if ((test = MailboxState_hasPermission(S, userid, right_flag)))
		return TRUE;

	/* else check the 'anyone' user */
	if (auth_user_exists(DBMAIL_ACL_ANYONE_USER, &anyone_userid))
		return MailboxState_hasPermission(S, anyone_userid, right_flag);
	
	return FALSE;
}

int acl_set_rights(uint64_t userid, uint64_t mboxid, const char *rightsstring)
{
	if (rightsstring[0] == '-')
		return acl_change_rights(userid, mboxid, rightsstring, 0);
	if (rightsstring[0] == '+')
		return acl_change_rights(userid, mboxid, rightsstring, 1);
	return acl_replace_rights(userid, mboxid, rightsstring);
}

ACLRight acl_get_right_from_char(char right_char)
{
	switch (right_char) {
	case 'l':
		return ACL_RIGHT_LOOKUP;
	case 'r':
		return ACL_RIGHT_READ;
	case 's':
		return ACL_RIGHT_SEEN;
	case 'w':
		return ACL_RIGHT_WRITE;
	case 'i':
		return ACL_RIGHT_INSERT;
	case 'p':
		return ACL_RIGHT_POST;
	case 'k':
		return ACL_RIGHT_CREATE;
	case 'x':
		return ACL_RIGHT_DELETE;
	case 't':
		return ACL_RIGHT_DELETED;
	case 'e':
		return ACL_RIGHT_EXPUNGE;
	case 'a':
		return ACL_RIGHT_ADMINISTER;
	default:
		TRACE(TRACE_ERR, "error wrong acl character. This error should have been caught earlier!");
		return ACL_RIGHT_NONE;
	}
}

int
acl_change_rights(uint64_t userid, uint64_t mboxid, const char *rightsstring,
		  int set)
{
	size_t i;
	char rightchar;

	for (i = 1; i < strlen(rightsstring); i++) {
		rightchar = rightsstring[i];
		switch (rightchar) {
			case 'c': // Old RFC 2086 - maps to k in RFC 4314
				if (acl_set_one_right(userid, mboxid, acl_get_right_from_char('k'), set) < 0) return -1;
				break;
			case 'd': // Old RFC 2086 - maps to x, t, and e in RFC 4314
				if (acl_set_one_right(userid, mboxid, acl_get_right_from_char('x'), set) < 0) return -1;
				if (acl_set_one_right(userid, mboxid, acl_get_right_from_char('t'), set) < 0) return -1;
				if (acl_set_one_right(userid, mboxid, acl_get_right_from_char('e'), set) < 0) return -1;
				break;
			default:
				if (acl_set_one_right(userid, mboxid, acl_get_right_from_char(rightchar), set) < 0) return -1;
				break;
		}
	}
	return 1;
}

int
acl_replace_rights(uint64_t userid, uint64_t mboxid, const char *rights)
{
	unsigned i;
	int set;
	gchar *rightsstring = NULL;

	rightsstring = g_strndup(rights, 256);

	TRACE(TRACE_DEBUG, "replacing rights for user [%" PRIu64 "], mailbox [%" PRIu64 "] to %s", userid, mboxid, rightsstring);

	// RFC 2086 to RFC 4314 mapping
	if (strchr(rightsstring, (int) 'c')) 
		rightsstring = g_strconcat(rightsstring, "k\0", NULL);
	if (strchr(rightsstring, (int) 'd'))
		rightsstring = g_strconcat(rightsstring, "xte\0", NULL);

	for (i = ACL_RIGHT_LOOKUP; i < ACL_RIGHT_NONE; i++) {

		if (strchr(rightsstring, (int) acl_right_chars[i]))
			set = 1;
		else
			set = 0;
		if (db_acl_set_right(userid, mboxid, acl_right_strings[i], set) < 0) {
			TRACE(TRACE_ERR, "error replacing ACL");
			g_free(rightsstring);
			return -1;
		}
	}
	g_free(rightsstring);
	return 1;

}

int
acl_set_one_right(uint64_t userid, uint64_t mboxid, ACLRight right, int set)
{
	return db_acl_set_right(userid, mboxid, acl_right_strings[right],
				set);
}


/*
int acl_delete_acl(uint64_t userid, uint64_t mboxid)
{
	return db_acl_delete_acl(userid, mboxid);
}
*/

char *acl_get_acl(uint64_t mboxid)
{
	uint64_t userid;
	char *username;
	size_t acl_string_size = 0;
	size_t acl_strlen;
	char *acl_string;	/* return string */
	char *identifier;	/* one identifier */
        char *identifier_astring;  /* identifier as IMAP astring */
	char rightsstring[NR_ACL_FLAGS + 1];
	int result;
	GList *identifier_list = NULL;
	unsigned nr_identifiers = 0;

	result = db_acl_get_identifier(mboxid, &identifier_list);

	if (result < 0) {
		TRACE(TRACE_ERR, "error when getting identifier list for mailbox [%" PRIu64 "].", mboxid);
		g_list_destroy(identifier_list);
		return NULL;
	}

	/* add the current user to the list if this user is the owner
	 * of the mailbox
	 */
	if (db_get_mailbox_owner(mboxid, &userid) < 0) {
		TRACE(TRACE_ERR, "error querying ownership of mailbox");
		g_list_destroy(identifier_list);
		return NULL;
	}

	if ((username = auth_get_userid(userid)) == NULL) {
		TRACE(TRACE_ERR, "error getting username for user [%" PRIu64 "]", userid);
		g_list_destroy(identifier_list);
		return NULL;
	}
	
	identifier_list = g_list_append(identifier_list, username);

	TRACE(TRACE_DEBUG, "before looping identifiers!");
	
	identifier_list = g_list_first(identifier_list);
	while (identifier_list) {
		nr_identifiers++;
		identifier_astring = dbmail_imap_astring_as_string((const char *)identifier_list->data);
		acl_string_size += strlen(identifier_astring) + NR_ACL_FLAGS + 2;
		g_free(identifier_astring);

		if (! g_list_next(identifier_list))
			break;
		identifier_list = g_list_next(identifier_list);
	}

	TRACE(TRACE_DEBUG, "acl_string size = %zd", acl_string_size);

	acl_string = g_new0(char, acl_string_size + 1);
	
	identifier_list = g_list_first(identifier_list);

	while (identifier_list) {
		identifier = (char *) identifier_list->data;
		if (acl_get_rightsstring_identifier(identifier, mboxid, rightsstring) < 0) {
			g_list_destroy(identifier_list);
			g_free(acl_string);
			return NULL;
		}
		TRACE(TRACE_DEBUG, "%s", rightsstring);
		if (strlen(rightsstring) > 0) {
			acl_strlen = strlen(acl_string);
			identifier_astring = dbmail_imap_astring_as_string(identifier);
			(void) snprintf(&acl_string[acl_strlen], acl_string_size - acl_strlen, "%s %s ", identifier_astring, rightsstring);
			g_free(identifier_astring);
		}

		if (! g_list_next(identifier_list))
			break;
		identifier_list = g_list_next(identifier_list);
	}
	g_list_destroy(identifier_list);
	
	return g_strstrip(acl_string);
}

const char *acl_listrights(uint64_t userid, uint64_t mboxid)
{
	int result;
	
	if ((result = db_user_is_mailbox_owner(userid, mboxid)) < 0) {
		TRACE(TRACE_ERR, "error checking if user is owner of a mailbox");
		return NULL;
	}

	if (result == 0) {
		/* user is not owner. User will never be granted any right
		   by default, but may be granted any right by setting the
		   right ACL */
		return "\"\" l r s w i p k x t e a c d";
	}

	/* user is owner, User will always be granted all rights */
	return acl_right_chars;
}

char *acl_myrights(uint64_t userid, uint64_t mboxid)
{
	char *rightsstring;

	if (! (rightsstring = g_new0(char, NR_ACL_FLAGS + 1))) {
		TRACE(TRACE_ERR, "error allocating memory for rightsstring");
		return NULL;
	}

	if (acl_get_rightsstring(userid, mboxid, rightsstring) < 0) {
		TRACE(TRACE_ERR, "error getting rightsstring.");
		g_free(rightsstring);
		return NULL;
	}

	return rightsstring;
}


int acl_get_rightsstring_identifier(char *identifier, uint64_t mboxid, char *rightsstring)
{
	uint64_t userid;

	assert(rightsstring);
	memset(rightsstring, '\0', NR_ACL_FLAGS + 1);
	
	if (! auth_user_exists(identifier, &userid)) {
		TRACE(TRACE_ERR, "error finding user id for user with name [%s]", identifier);
		return -1;
	}

	return acl_get_rightsstring(userid, mboxid, rightsstring);
}

int acl_get_rightsstring(uint64_t userid, uint64_t mboxid, char *rightsstring)
{
	int result;
	uint64_t owner_idnr;
	MailboxState_T S;
	struct ACLMap map;

	assert(rightsstring);
	memset(rightsstring, '\0', NR_ACL_FLAGS + 1);

	if ((result = db_get_mailbox_owner(mboxid, &owner_idnr)) <= 0)
		return result;

	if (owner_idnr == userid) {
		TRACE(TRACE_DEBUG, "mailbox [%" PRIu64 "] is owned by user [%" PRIu64 "], giving all rights", mboxid, userid);
		g_strlcat(rightsstring, acl_right_chars, NR_ACL_FLAGS+1);
		return 1;
	}
	
	memset(&map, '\0', sizeof(struct ACLMap));
	S = MailboxState_new(NULL, mboxid);
	MailboxState_setOwner(S, owner_idnr);
	result = MailboxState_getAcl(S, userid, &map);
	MailboxState_free(&S);

	if (result == DM_EQUERY) return result;
	
	if (map.lookup_flag)
		g_strlcat(rightsstring,"l", NR_ACL_FLAGS+1);
	if (map.read_flag)
		g_strlcat(rightsstring,"r", NR_ACL_FLAGS+1);
	if (map.seen_flag)
		g_strlcat(rightsstring,"s", NR_ACL_FLAGS+1);
	if (map.write_flag)
		g_strlcat(rightsstring,"w", NR_ACL_FLAGS+1);
	if (map.insert_flag)
		g_strlcat(rightsstring,"i", NR_ACL_FLAGS+1);
	if (map.post_flag)
		g_strlcat(rightsstring,"p", NR_ACL_FLAGS+1);
	if (map.create_flag)
		g_strlcat(rightsstring,"k", NR_ACL_FLAGS+1);
	if (map.delete_flag)
		g_strlcat(rightsstring,"x", NR_ACL_FLAGS+1);
	if (map.deleted_flag)
		g_strlcat(rightsstring,"t", NR_ACL_FLAGS+1);
	if (map.expunge_flag)
		g_strlcat(rightsstring,"e", NR_ACL_FLAGS+1);
	if (map.administer_flag)
		g_strlcat(rightsstring,"a", NR_ACL_FLAGS+1);

	// RFC 4314 backwords compatible RFC 2086 virtual c and d rights
	if (map.create_flag)
		g_strlcat(rightsstring,"c", NR_ACL_FLAGS+1);
	if (map.delete_flag || map.deleted_flag || map.expunge_flag)
		g_strlcat(rightsstring,"d", NR_ACL_FLAGS+1);

	return 1;
}
