/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (C) 2006 Aaron Stone aaron@serendipity.cx
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2024 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

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
 *
 * sieved.c
 * 
 * Main program for sieve daemon
 */

#include "dbmail.h"
#define THIS_MODULE "sieved"
#define PNAME "dbmail/sieved"

/* this is write-once read-many, so we'll do it once for all children. */
const char *sieve_extensions = NULL;

int main(int argc, char *argv[])
{
	ServerConfig_T config;
	int result;
	openlog(PNAME, LOG_PID, LOG_MAIL);
	memset(&config, 0, sizeof(ServerConfig_T));
	result = server_getopt(&config, "SIEVE", argc, argv);
	if (result == -1) goto shutdown;
	if (result == 1) {
		server_showhelp(
			"dbmail-sieved",
			"This daemon provides a partial ManageSieve "
			"daemon RFC 5804 for managing "
			"Sieve mail filtering scripts RFC 5228.");
		goto shutdown;
	}

	config.ClientHandler = sieve_handle_connection;

	/* Get the Sieve capabilities. This may also cause the
	 * program to bomb out if Sieve support was not compiled in. */
	sieve_extensions = sort_listextensions();
	if (sieve_extensions == NULL) {
		fprintf(stderr, "dbmail-sieved: error loading Sieve extensions.\n\n");
		result = 1;
		goto shutdown;
	}
	result = server_mainloop(&config, "dbmail-sieved");
shutdown:
	TRACE(TRACE_INFO, "return [%d]", result);
	return result;
}
