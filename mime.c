/*
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

/* $Id$
 *
 * Functions for pulling addresses out of a header field. */

#include "dbmail.h"


int find_deliver_to_header_addresses(struct DbmailMessage *message,
		const char *scan_for_field, struct dm_list *targetlist)
{
	InternetAddressList *ialisthead, *ialist;
	InternetAddress *ia;
	char *header_field;

	if (!message || !scan_for_field || !targetlist) {
		trace(TRACE_WARNING, "%s,%s: received a NULL argument, this is a bug",
				__FILE__, __func__);
		return -1;
	}

	header_field = dbmail_message_get_header(message, scan_for_field);
	trace(TRACE_INFO, "%s,%s: mail address parser looking at field [%s] with value [%s]",
			__FILE__, __func__, scan_for_field, header_field);
	
	if ((ialist = internet_address_parse_string(header_field)) == NULL) {
		trace(TRACE_ERROR, "%s,%s: mail address parser error parsing header field",
			__FILE__, __func__);
		g_free(header_field);
		return -1;
	}
	g_free(header_field);

	ialisthead = ialist;
	while (1) {
		ia = ialist->address;
		dm_list_nodeadd(targetlist, ia->value.addr, strlen(ia->value.addr) + 1);
		if (! ialist->next)
			break;
		ialist = ialist->next;
	}
	
	internet_address_list_destroy(ialisthead);

	trace(TRACE_DEBUG, "%s,%s: mail address parser found [%ld] email addresses",
			__FILE__, __func__, targetlist->total_nodes);

	if (targetlist->total_nodes == 0)	/* no addresses found */
		return -1;

	return 0;
}

