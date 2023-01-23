/* 
 Copyright (C) 1999-2004 Aaron Stone aaron at serendipity dot cx
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

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

 *
 * Functions for running user defined sorting rules
 * on a message in the temporary store, usually
 * just delivering the message to the user's INBOX
 * ...unless they have fancy rules defined, that is :-)
 * 
 */

#include "dbmail.h"
#define THIS_MODULE "sort"

const char * sort_listextensions(void)
{
	return "";
}

SortResult_T *sort_validate(uint64_t user_idnr UNUSED, char *scriptname UNUSED)
{
	TRACE(TRACE_WARNING, "SIEVE sorting enabled in DELIVERY section of dbmail.conf,"
			" but this build of DBMail was statically configured without Sieve.");
	return NULL;
}

SortResult_T *sort_process(uint64_t user_idnr UNUSED, DbmailMessage *message UNUSED, const char *mailbox UNUSED)
{
	TRACE(TRACE_WARNING, "SIEVE sorting enabled in DELIVERY section of dbmail.conf,"
			" but this build of DBMail was statically configured without Sieve.");
	return NULL;
}

/* SORT RESULT INTERFACE */

void sort_free_result(SortResult_T *result UNUSED)
{
	return;
}

int sort_get_cancelkeep(SortResult_T *result UNUSED)
{
	return 0;
}

int sort_get_reject(SortResult_T *result UNUSED)
{
	return 0;
}

const char * sort_get_mailbox(SortResult_T *result UNUSED)
{
	return "";
}

const char * sort_get_errormsg(SortResult_T *result UNUSED)
{
	return "";
}

int sort_get_error(SortResult_T *result UNUSED)
{
	return 0;
}

