/*	$Id$
 *	(c) 2000-2002 IC&S, The Netherlands
 *
 *	Miscelaneous functions */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmail.h"
#include "dbmd5.h"
#include "misc.h"
#include <stdlib.h>

int drop_priviledges (char *newuser, char *newgroup)
{
	/* will drop running program's priviledges to newuser and newgroup */
	struct passwd *pwd;
	struct group *grp;
	
	grp = getgrnam(newgroup);

	if (grp == NULL)
	{
		trace (TRACE_ERROR,"drop_priviledges(): could not find group %s\n",newgroup);
		return -1;
	}

	pwd = getpwnam(newuser);
	if (pwd == NULL)
	{
		trace (TRACE_ERROR,"drop_priviledges(): could not find user %s\n",newuser);
		return -1;
	}

	if (setgid (grp->gr_gid) !=0)
	{
		trace (TRACE_ERROR,"drop_priviledges(): could not set gid to %s\n",newgroup);
		return -1;
	}
	
	if (setuid (pwd->pw_uid) != 0)
	{
		trace (TRACE_ERROR,"drop_priviledges(): could not set uid to %s\n",newuser);
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

void create_unique_id(char *target, u64_t messageid)
{
  if (messageid != 0)
    snprintf (target,UID_SIZE,"%s:%s",itoa(messageid),itoa(rand()));
  else
    snprintf (target,UID_SIZE,"%s",itoa(rand()));
  snprintf (target,UID_SIZE,"%s",makemd5(target));
  trace (TRACE_DEBUG,"create_unique_id(): created: %s",target);
}

