/*	$Id$
 *	Miscelaneous functions */

#include "config.h"
#include "misc.h"

int drop_priviledges (char *newuser, char *newgroup)
{
	/* will drop running program's priviledges to newuser and newgroup */
	struct passwd *pwd;
	struct group *grp;
	
	grp = getgrnam(newgroup);

	if (grp == NULL)
	{
		trace (TRACE_ERROR,"drop_priviledges(): could not find group %s",newgroup);
		return -1;
	}

	pwd = getpwnam(newuser);
	if (pwd == NULL)
	{
		trace (TRACE_ERROR,"drop_priviledges(): could not find user %s",newgroup);
		return -1;
	}

	if (setgid (grp->gr_gid) !=0)
	{
		trace (TRACE_ERROR,"drop_priviledges(): could not set gid to %s",newuser);
		return -1;
	}
	
	if (setuid (pwd->pw_uid) != 0)
	{
		trace (TRACE_ERROR,"drop_priviledges(): could not set uid to %s",newuser);
		return -1;
	}
	return 0;
}
