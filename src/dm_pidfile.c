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
#define THIS_MODULE "pidfile"

/* These are used by pidfile_remove. */
static FILE *pidfile_to_close;
static char *pidfile_to_remove;

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
	unlink(pidFile);
	return 0;
}

static void pidfile_remove(void)
{
	int res;

	if (pidfile_to_close) {
		res = fclose(pidfile_to_close);
		if (res) TRACE(TRACE_ERR, "Error closing pidfile: [%s].",
			strerror(errno));
		pidfile_to_close = NULL;
	}

	if (pidfile_to_remove) {
		res = unlink(pidfile_to_remove);
		if (res) TRACE(TRACE_ERR, "Error unlinking pidfile [%s]: [%s].",
			pidfile_to_remove, strerror(errno));
		g_free(pidfile_to_remove);
		pidfile_to_remove = NULL;
	}

}

/* Create a pidfile and leave it open. */
void pidfile_create(const char *pidFile, pid_t pid)
{
	FILE *f;
	pid_t oldpid;

	oldpid = pidfile_pid(pidFile);

	if (oldpid != 0) {
		TRACE(TRACE_EMERG, "File [%s] exists and process id [%d] is running.", 
			pidFile, (int)pid);
	}

	if (!(f = fopen(pidFile, "w"))) {
		int serr = errno;
		TRACE(TRACE_EMERG, "open pidfile [%s] failed: [%s]",
				pidFile, strerror(serr));
		return;
	}
	if (chmod(pidFile, 0644)) {
		int serr = errno;
		TRACE(TRACE_EMERG, "chown pidfile [%s] failed: [%s]",
			       	pidFile, strerror(serr));
		fclose(f);
		return;
	}

	fprintf(f, "%u\n", pid);
	fflush(f);

	/* Leave pid file open & locked for the duration,
	 * but close and remove it upon termination.  */
	atexit(pidfile_remove);

	pidfile_to_close = f;
	pidfile_to_remove = g_strdup(pidFile);

}

