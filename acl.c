/*
 Copyright (C) 2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl

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

#define NR_ACL_FLAGS 9
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
	"administer_flag"
};

static const char acl_right_chars[] = "lrswipcda";
//{'l','r','s','w','i','p','c','d','a'};

/* local functions */
static ACLRight_t acl_get_right_from_char(char right_char);
static int acl_change_rights(u64_t userid, u64_t mboxid,
			     const char *rightsstring, int set);
static int acl_replace_rights(u64_t userid, u64_t mboxid,
			      const char *rightsstring);
static int acl_set_one_right(u64_t userid, u64_t mboxid,
			     ACLRight_t right, int set);
static int acl_get_rightsstring_identifier(char *identifier, u64_t mboxid,
					   /*@out@*/ char *rightsstring);
static int acl_get_rightsstring(u64_t userid, u64_t mboxid,
				/*@out@*/ char *rightsstring);


int acl_has_right(mailbox_t *mailbox, u64_t userid, ACLRight_t right)
{
	u64_t anyone_userid;
	int test;
	
	switch(right) {
		case ACL_RIGHT_SEEN:
		case ACL_RIGHT_WRITE:
		case ACL_RIGHT_INSERT:
		case ACL_RIGHT_POST:
		case ACL_RIGHT_CREATE:
		case ACL_RIGHT_DELETE:
		case ACL_RIGHT_ADMINISTER:
			if (mailbox_is_writable(mailbox->uid))
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
	if ((test = db_acl_has_right(mailbox, userid, right_flag)))
		return TRUE;

	/* else check the 'anyone' user */
	test = auth_user_exists(DBMAIL_ACL_ANYONE_USER, &anyone_userid);
	if (test == DM_EQUERY) 
		return DM_EQUERY;
	if (test)
		return db_acl_has_right(mailbox, anyone_userid, right_flag);
	
	return FALSE;
}

int acl_set_rights(u64_t userid, u64_t mboxid, const char *rightsstring)
{
	if (rightsstring[0] == '-')
		return acl_change_rights(userid, mboxid, rightsstring, 0);
	if (rightsstring[0] == '+')
		return acl_change_rights(userid, mboxid, rightsstring, 1);
	return acl_replace_rights(userid, mboxid, rightsstring);
}

ACLRight_t acl_get_right_from_char(char right_char)
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
	case 'c':
		return ACL_RIGHT_CREATE;
	case 'd':
		return ACL_RIGHT_DELETE;
	case 'a':
		return ACL_RIGHT_ADMINISTER;
	default:
		TRACE(TRACE_ERROR, "error wrong acl character. This error should have been caught earlier!");
		return ACL_RIGHT_NONE;
	}
}

int
acl_change_rights(u64_t userid, u64_t mboxid, const char *rightsstring,
		  int set)
{
	size_t i;
	char rightchar;

	for (i = 1; i < strlen(rightsstring); i++) {
		rightchar = rightsstring[i];
		if (acl_set_one_right(userid, mboxid,
				      acl_get_right_from_char(rightchar),
				      set) < 0)
			return -1;
	}
	return 1;
}

int
acl_replace_rights(u64_t userid, u64_t mboxid, const char *rightsstring)
{
	unsigned i;
	int set;

	TRACE(TRACE_DEBUG, "replacing rights for user [%llu], mailbox [%llu] to %s", userid, mboxid, rightsstring);
	for (i = ACL_RIGHT_LOOKUP; i < ACL_RIGHT_NONE; i++) {

		if (strchr(rightsstring, (int) acl_right_chars[i]))
			set = 1;
		else
			set = 0;
		if (db_acl_set_right
		    (userid, mboxid, acl_right_strings[i], set) < 0) {
			TRACE(TRACE_ERROR, "error replacing ACL");
			return -1;
		}
	}
	return 1;

}

int
acl_set_one_right(u64_t userid, u64_t mboxid, ACLRight_t right, int set)
{
	return db_acl_set_right(userid, mboxid, acl_right_strings[right],
				set);
}


/*
int acl_delete_acl(u64_t userid, u64_t mboxid)
{
	return db_acl_delete_acl(userid, mboxid);
}
*/

char *acl_get_acl(u64_t mboxid)
{
	u64_t userid;
	char *username;
	size_t acl_string_size = 0;
	size_t acl_strlen;
	char *acl_string;	/* return string */
	char *identifier;	/* one identifier */
        char *identifier_astring;  /* identifier as IMAP astring */
	char rightsstring[NR_ACL_FLAGS + 1];
	int result;
	struct dm_list identifier_list;
	struct element *identifier_elm;
	unsigned nr_identifiers = 0;

	result = db_acl_get_identifier(mboxid, &identifier_list);

	if (result < 0) {
		TRACE(TRACE_ERROR, "error when getting identifier list for mailbox [%llu].", mboxid);
		dm_list_free(&identifier_list.start);
		return NULL;
	}

	/* add the current user to the list if this user is the owner
	 * of the mailbox
	 */
	if (db_get_mailbox_owner(mboxid, &userid) < 0) {
		TRACE(TRACE_ERROR, "error querying ownership of mailbox");
		dm_list_free(&identifier_list.start);
		return NULL;
	}

	if ((username = auth_get_userid(userid)) == NULL) {
		TRACE(TRACE_ERROR, "error getting username for user [%llu]", userid);
		dm_list_free(&identifier_list.start);
		return NULL;
	}
	
	if (dm_list_nodeadd(&identifier_list, username, strlen(username) + 1) == NULL) { 
		TRACE(TRACE_ERROR, "error adding username to list");
		dm_list_free(&identifier_list.start);
		g_free(username);
		return NULL;
	}
	
	g_free(username);

	TRACE(TRACE_DEBUG, "before looping identifiers!");
	
	identifier_elm = dm_list_getstart(&identifier_list);
	while (identifier_elm) {
		nr_identifiers++;
		identifier_astring = dbmail_imap_astring_as_string(identifier_elm->data);
		acl_string_size += strlen(identifier_astring) + NR_ACL_FLAGS + 2;
		g_free(identifier_astring);
		identifier_elm = identifier_elm->nextnode;
	}

	TRACE(TRACE_DEBUG, "acl_string size = %zd", acl_string_size);

	if (! (acl_string = g_new0(char, acl_string_size + 1))) {
		dm_list_free(&identifier_list.start);
		TRACE(TRACE_FATAL, "error allocating memory");
		return NULL;
	}
	
	identifier_elm = dm_list_getstart(&identifier_list);
	while (identifier_elm) {
		identifier = (char *) identifier_elm->data;
		if (acl_get_rightsstring_identifier(identifier, mboxid, rightsstring) < 0) {
			dm_list_free(&identifier_list.start);
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
		identifier_elm = identifier_elm->nextnode;
	}
	dm_list_free(&identifier_list.start);
	
	return g_strstrip(acl_string);
}

char *acl_listrights(u64_t userid, u64_t mboxid)
{
	int result;
	
	if ((result = db_user_is_mailbox_owner(userid, mboxid)) < 0) {
		TRACE(TRACE_ERROR, "error checking if user is owner of a mailbox");
		return NULL;
	}

	if (result == 0) {
		/* user is not owner. User will never be granted any right
		   by default, but may be granted any right by setting the
		   right ACL */
		return g_strdup("\"\" l r s w i p c d a");
	}

	/* user is owner, User will always be granted all rights */
	return g_strdup(acl_right_chars);
}

char *acl_myrights(u64_t userid, u64_t mboxid)
{
	char *rightsstring;

	if (! (rightsstring = g_new0(char, NR_ACL_FLAGS + 1))) {
		TRACE(TRACE_ERROR, "error allocating memory for rightsstring");
		return NULL;
	}

	if (acl_get_rightsstring(userid, mboxid, rightsstring) < 0) {
		TRACE(TRACE_ERROR, "error getting rightsstring.");
		g_free(rightsstring);
		return NULL;
	}

	return rightsstring;
}


int acl_get_rightsstring_identifier(char *identifier, u64_t mboxid, char *rightsstring)
{
	u64_t userid;

	assert(rightsstring);
	memset(rightsstring, '\0', NR_ACL_FLAGS + 1);
	
	if (auth_user_exists(identifier, &userid) < 0) {
		TRACE(TRACE_ERROR, "error finding user id for user with name [%s]", identifier);
		return -1;
	}

	return acl_get_rightsstring(userid, mboxid, rightsstring);
}

int acl_get_rightsstring(u64_t userid, u64_t mboxid, char *rightsstring)
{
	int result;
	u64_t owner_idnr;
	mailbox_t mailbox;
	struct ACLMap map;

	assert(rightsstring);
	memset(rightsstring, '\0', NR_ACL_FLAGS + 1);

	if ((result = db_get_mailbox_owner(mboxid, &owner_idnr)) <= 0)
		return result;

	if (owner_idnr == userid) {
		TRACE(TRACE_DEBUG, "mailbox [%llu] is owned by user [%llu], giving all rights", mboxid, userid);
		g_strlcat(rightsstring, acl_right_chars, NR_ACL_FLAGS+1);
		return 1;
	}
	
	memset(&mailbox, '\0', sizeof(mailbox_t));
	memset(&map, '\0', sizeof(struct ACLMap));
	
	mailbox.uid = mboxid;
	mailbox.owner_idnr = owner_idnr;
	
	if ((result = db_acl_get_acl_map(&mailbox, userid, &map)) == DM_EQUERY)
		return result;
	
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
		g_strlcat(rightsstring,"c", NR_ACL_FLAGS+1);
	if (map.delete_flag)
		g_strlcat(rightsstring,"d", NR_ACL_FLAGS+1);
	if (map.administer_flag)
		g_strlcat(rightsstring,"a", NR_ACL_FLAGS+1);
	
	return 1;
}

