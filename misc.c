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

/*	$Id$
 *	(c) 2000-2002 IC&S, The Netherlands
 *
 *	Miscelaneous functions */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "auth.h"
#include "dbmail.h"
#include "dbmd5.h"
#include "misc.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

int drop_privileges (char *newuser, char *newgroup)
{
     /* will drop running program's priviledges to newuser and newgroup */
     struct passwd *pwd;
     struct group *grp;
     
     grp = getgrnam(newgroup);
     
     if (grp == NULL) {
	  trace (TRACE_ERROR,"%s,%s: could not find group %s\n",
		 __FILE__, __FUNCTION__, newgroup);
	     return -1;
     }
     
     pwd = getpwnam(newuser);
     if (pwd == NULL) {
	  trace (TRACE_ERROR,"%s,%s: could not find user %s\n",
		 __FILE__, __FUNCTION__, newuser);
	  return -1;
     }
     
     if (setgid (grp->gr_gid) !=0) {
	  trace (TRACE_ERROR,"%s,%s: could not set gid to %s\n",
		 __FILE__, __FUNCTION__, newgroup);
	  return -1;
     }
     
     if (setuid (pwd->pw_uid) != 0) {
     	  trace (TRACE_ERROR,"%s,%s: could not set uid to %s\n",
		 __FILE__, __FUNCTION__, newuser);
	  return -1;
     }
     return 0;
}

char *itoa(int i)
{
       char *s=(char *) malloc(42); /* Enough for a 128 bit integer */
       if (s) sprintf(s,"%d",i);
       return s;
}

void create_unique_id(char *target, u64_t message_idnr)
{
     if (message_idnr != 0)
	  snprintf (target, UID_SIZE, "%s:%s", 
		    itoa(message_idnr), itoa(rand()));
     else
	  snprintf (target, UID_SIZE, "%s", itoa(rand()) );
     snprintf (target, UID_SIZE, "%s", makemd5 (target) );
     trace (TRACE_DEBUG,"%s,%s: created: %s", __FILE__, __FUNCTION__, target);
}

void create_current_timestring(timestring_t *timestring)
{
	time_t td;
	struct tm tm;
	
	if (time(&td) == -1)
		trace(TRACE_FATAL, "%s,%s: error getting time from OS",
		      __FILE__, __FUNCTION__);

	tm = *localtime(&td);   /* get components */
	strftime((char*)timestring, sizeof(timestring_t), 
		 "%Y-%m-%d %H:%M:%S", &tm);
}

char *mailbox_add_namespace(const char *mailbox_name, u64_t owner_idnr, 
		       u64_t user_idnr)
{
	char *fq_name;
	char *owner_name;
	size_t fq_name_len;

	if (mailbox_name == NULL) {
		trace(TRACE_ERROR, "%s,%s: error, mailbox_name is "
		      "NULL.", __FILE__, __FUNCTION__);
		return NULL;
	}
		
	if (user_idnr == owner_idnr) {
		/* mailbox owned by current user */
		return strdup(mailbox_name);
	} else {
		owner_name = auth_get_userid(&owner_idnr);
		if (owner_name == NULL) {
			trace(TRACE_ERROR, "%s,%s: error owner_name is NULL",
			      __FILE__, __FUNCTION__);
			return NULL;
		}
		trace(TRACE_ERROR, "%s,%s: owner name = %s", __FILE__,
		      __FUNCTION__, owner_name);
		if (strcmp(owner_name, PUBLIC_FOLDER_USER) == 0) {
			fq_name_len = strlen(NAMESPACE_PUBLIC) +
				strlen(MAILBOX_SEPERATOR) +
				strlen(mailbox_name) + 1;
			if (!(fq_name = my_malloc(fq_name_len * 
						  sizeof(char)))) {
				trace(TRACE_ERROR, "%s,%s: not enough memory",
				      __FILE__, __FUNCTION__);
				return NULL;
			}
			snprintf(fq_name, fq_name_len, "%s%s%s",
				 NAMESPACE_PUBLIC, MAILBOX_SEPERATOR, 
				 mailbox_name);
		} else {
			fq_name_len = strlen(NAMESPACE_USER) +
				strlen(MAILBOX_SEPERATOR) +
				strlen(owner_name) +
				strlen(MAILBOX_SEPERATOR) +
				strlen(mailbox_name) + 1;
			if (!(fq_name = my_malloc(fq_name_len *
						  sizeof(char)))) {
				trace(TRACE_ERROR, "%s,%s: not enough memory",
				      __FILE__, __FUNCTION__);
				return NULL;
			}
			snprintf(fq_name, fq_name_len, "%s%s%s%s%s",
				 NAMESPACE_USER, MAILBOX_SEPERATOR,
				 owner_name, MAILBOX_SEPERATOR,
				 mailbox_name);
		}
		my_free(owner_name);
		trace(TRACE_INFO, "%s,%s: returning fully qualified name "
		      "[%s]", __FILE__, __FUNCTION__, fq_name);
		return fq_name;
	}
}

const char *mailbox_remove_namespace(const char *fq_name)
{
	char *temp;

	/* a lot of strlen() functions are used here, so this
	   can be quite inefficient! On the other hand, this
	   is a function that's not used that much. */
	if (strcmp(fq_name, NAMESPACE_USER) == 0) {
		temp = strstr(fq_name, MAILBOX_SEPERATOR);
		if (temp == NULL || strlen(temp) <= 1) {
			trace(TRACE_ERROR, "%s,%s wronly constructed mailbox "
			      "name", __FILE__, __FUNCTION__);
			return NULL;
		}
		temp = strstr(&temp[1], MAILBOX_SEPERATOR);
		if (temp == NULL || strlen(temp) <= 1) {
			trace(TRACE_ERROR, "%s,%s wronly constructed mailbox "
			      "name", __FILE__, __FUNCTION__);
			return NULL;
		}
		return &temp[1];
	}
	if (strcmp(fq_name, NAMESPACE_PUBLIC) == 0) {
		temp = strstr(fq_name, MAILBOX_SEPERATOR);
		
		if (temp == NULL || strlen(temp) <= 1) {
			trace(TRACE_ERROR, "%s,%s wronly constructed mailbox "
			      "name", __FILE__, __FUNCTION__);
			return NULL;
		}
		return &temp[1];
	}
	return fq_name;
}
