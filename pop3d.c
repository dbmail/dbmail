/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (C) 2005-2006 NFG Net Facilities Group BV support@nfg.nl
 Copyright (C) 2006 Aaron Stone aaron@serendipity.cx

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

/* $Id: pop3d.c 2253 2006-09-07 06:01:24Z aaron $
*
* pop3d.c
*
* main prg for pop3 daemon
*/

#include "dbmail.h"
#define THIS_MODULE "pop3d"
#define PNAME "dbmail/pop3d"

/* server timeout error */
#define POP_TIMEOUT_MSG "-ERR I'm leaving, you're tooo slow\r\n"

/* also used in pop3.c */
int pop_before_smtp = 0;

int main(int argc, char *argv[])
{
	serverConfig_t config;
	int result;
		
	g_mime_init(0);
	openlog(PNAME, LOG_PID, LOG_MAIL);

	serverparent_config(&config, "POP");
	result = serverparent_getopt(&config, argc, argv);
	if (result == -1)
		goto shutdown;

	if (result == 1) {
		serverparent_showhelp("dbmail-pop3d",
			"This daemon provides Post Office Protocol v3 services.\n");
		goto shutdown;
	}

	config.ClientHandler = pop3_handle_connection;
	config.timeoutMsg = POP_TIMEOUT_MSG;
	pop_before_smtp = config.service_before_smtp;

	result = serverparent_mainloop(&config, "dbmail-pop3d");
	
shutdown:
	g_mime_shutdown();
	config_free();

	TRACE(TRACE_INFO, "exit");
	return result;
}

