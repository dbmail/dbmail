/*
  $Id$

 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include "proctitleutils.h"

/* Globals */
static char **Argv = ((void *) 0);
static char *LastArgv = ((void *) 0);
static int start = 0;

void init_set_proc_title(int argc, char *argv[], char *envp[],
			 const char *name)
{
	int i, envpsize;
	extern char **environ;
	char **p;
	char *ptr;

	for (i = envpsize = 0; envp[i] != NULL; i++)
		envpsize += strlen(envp[i]) + 1;

	if ((p = (char **) malloc((i + 1) * sizeof(char *))) != NULL) {
		environ = p;

		for (i = 0; envp[i] != NULL; i++) {
			if ((environ[i] =
			     malloc(strlen(envp[i]) + 1)) != NULL)
				strcpy(environ[i], envp[i]);
		}

		environ[i] = NULL;
	}

	Argv = argv;

	for (i = 0; envp[i] != NULL; i++) {
		if ((LastArgv + 1) == envp[i])	// Not sure if this conditional is needed
			LastArgv = envp[i] + strlen(envp[i]);
	}

	// Clear the title (from the start of argv to the start of envp)
	// All command line arguments should have been taken care of by now...
	for (ptr = Argv[0]; ptr < envp[0]; ptr++)
		*ptr = '\0';

	set_proc_title("%s : ", name);
	start = strlen(name) + 3;
}

void set_proc_title(char *fmt, ...)
{
	va_list msg;
	static char statbuf[8192];
	char *p = Argv[0];
	int maxlen = (LastArgv - Argv[0]) - 2;

	// Clear old Argv[0]
	for (p += start; *p; p++)
		*p = '\0';

	va_start(msg, fmt);

	memset(statbuf, 0, sizeof(statbuf));
	vsnprintf(statbuf, sizeof(statbuf), fmt, msg);

	va_end(msg);

	snprintf(Argv[0] + start, maxlen, "%s", statbuf);

	Argv[1] = ((void *) 0);
}
