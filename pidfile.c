/* pidfile handling for DBMail, many thanks to Tridge and Samba
 * for making their software available under the GNU GPL.
 *
 * Modified for DBMail by Aaron Stone, July 9, 2004.
 */

/* this code is broken - there is a race condition with the unlink (tridge) */

/* 
   Unix SMB/CIFS implementation.
   pidfile handling
   Copyright (C) Andrew Tridgell 1998
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "dbmail.h"

/* These are used by pidfile_remove. */
static FILE *pidfile_to_close = NULL;
static const char *pidfile_to_remove = NULL;

/* Check if a process exists. */
static int process_exists(pid_t pid)
{
	if (pid > 0)
		return (kill(pid, 0) == 0 || errno != ESRCH);
	return 0;
}

/* Return the pid in a pidfile, or return 0 
 * if the process or pidfile does not exist. */
static pid_t pidfile_pid(const char *pidFile)
{
	FILE *f;
	char pidstr[20];
	unsigned ret;

	memset(pidstr, 0, sizeof(pidstr));

	if (!(f = fopen(pidFile, "r"))) {
		return 0;
	}

	if (fread(pidstr, sizeof(char), sizeof(pidstr)-1, f) <= 0) {
		goto noproc;
	}

	ret = atoi(pidstr);
	
	if (!process_exists((pid_t)ret)) {
		goto noproc;
	}

	fclose(f);
	return (pid_t)ret;

 noproc:
	fclose(f);
	remove(pidFile);
	return 0;
}

void pidfile_remove(void)
{
	if (pidfile_to_close)
		fclose(pidfile_to_close);

	if (pidfile_to_remove)
		remove(pidfile_to_remove);
}

/* Create a pidfile and leave it open. */
void pidfile_create(const char *pidFile, pid_t pid)
{
	FILE *f;
	char buf[20];
	pid_t oldpid;

	oldpid = pidfile_pid(pidFile);

	if (oldpid != 0) {
		trace(TRACE_FATAL, "%s, %s: File [%s] exists and process id [%d] is running.", 
			__FILE__, __func__, pidFile, (int)pid);
	}

	if (!(f = fopen(pidFile, "w"))) {
		trace(TRACE_FATAL, "%s, %s: Cannot open pidfile [%s], error was [%s]",
			__FILE__, __func__, pidFile, strerror(errno));
	}

	memset(buf, 0, sizeof(buf));

	snprintf(buf, sizeof(buf)-1, "%u", pid);

	fwrite(buf, sizeof(char), strlen(buf), f);

	fflush(f);

	/* Leave pid file open & locked for the duration,
	 * but close and remove it upon termination.  */

	pidfile_to_close = f;
	pidfile_to_remove = pidFile;

	atexit(pidfile_remove);

}

