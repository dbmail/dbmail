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

#include <stdlib.h>
#include "dbmail.h"
#include "dbmd5.h"
#include "misc.h"

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
