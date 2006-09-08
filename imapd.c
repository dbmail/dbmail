/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (C) 2004-2006 NFG Net Facilities Group BV support@nfg.nl
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

/* $Id: imapd.c 2257 2006-09-08 08:44:29Z aaron $
 *
 * imapd.c
 * 
 * main prg for imap daemon
 */

#include "dbmail.h"
#define THIS_MODULE "imapd"
#define PNAME "dbmail/imap4d"

int imap_before_smtp = 0;

int main(int argc, char *argv[])
{
	serverConfig_t config;
	int result;
		
	g_mime_init(0);
	openlog(PNAME, LOG_PID, LOG_MAIL);

	result = serverparent_getopt(&config, "IMAP", argc, argv);
	if (result == -1)
		goto shutdown;

	if (result == 1) {
		serverparent_showhelp("dbmail-imapd",
			"This daemon provides Internet Message Access Protocol 4.1 services.");
		goto shutdown;
	}

	/* These are in imap4.h */
	config.ClientHandler = IMAPClientHandler;
	config.timeoutMsg = IMAP_TIMEOUT_MSG;

	imap_before_smtp = config.service_before_smtp;

	result = serverparent_mainloop(&config, "dbmail-imapd");
	
shutdown:
	g_mime_shutdown();
	config_free();

	TRACE(TRACE_INFO, "exit");
	return result;
}

