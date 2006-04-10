/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2006 NFG Net Facilities Group BV support@nfg.nl

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

/* $Id: imap4.h 2054 2006-03-27 14:31:02Z paul $
 * 
 * imap4.h
 */

#ifndef _IMAP4_H
#define _IMAP4_H

#include "dbmail.h"

#define IMAP_SERVER_VERSION VERSION
#define IMAP_CAPABILITY_STRING "IMAP4 IMAP4rev1 AUTH=LOGIN ACL NAMESPACE CHILDREN SORT QUOTA"
#define IMAP_TIMEOUT_MSG "* BYE dbmail IMAP4 server signing off due to timeout\r\n"

/* max number of BAD/NO responses */
#define MAX_FAULTY_RESPONSES 5

/* max number of retries when synchronizing mailbox with dbase */
#define MAX_RETRIES 20

#define null_free(p) { dm_free(p); p = NULL; }



typedef struct {
	int itemtype;		/* the item to be fetched */
	int argstart;		/* start index in the arg array */
	int argcnt;		/* number of args belonging to this bodyfetch */
	guint64 octetstart, octetcnt;	/* number of octets to be retrieved */

	char partspec[IMAP_MAX_PARTSPEC_LEN];	/* part specifier (i.e. '2.1.3' */

} body_fetch_t;


typedef struct {
	GList *bodyfetch;

	gboolean noseen;		/* set the seen flag ? */
	gboolean msgparse_needed;
	gboolean hdrparse_needed;

	/* helpers */
	gboolean setseen;
	gboolean isfirstfetchout;

	/* fetch elements */
	gboolean getUID;
	gboolean getSize;
	gboolean getFlags;
	gboolean getInternalDate;
	gboolean getEnvelope;
	gboolean getMIME_IMB;
	gboolean getMIME_IMB_noextension;
	gboolean getRFC822Header;
	gboolean getRFC822Text;
	gboolean getRFC822Peek;
	gboolean getRFC822;
	gboolean getBodyTotal;
	gboolean getBodyTotalPeek;
} fetch_items_t;



typedef struct {
	int state;		/* IMAP state of client */
	u64_t userid;		/* userID of client in dbase */
	mailbox_t mailbox;	/* currently selected mailbox */
} imap_userdata_t;

typedef enum {
	IMAP_STORE_FLAG_SEEN,
	IMAP_STORE_FLAG_ANSWERED,
	IMAP_STORE_FLAG_DELETED,
	IMAP_STORE_FLAG_FLAGGED,
	IMAP_STORE_FLAG_DRAFT,
	IMAP_STORE_FLAG_RECENT
} imap_store_flag_t;

imap_userdata_t * dbmail_imap_userdata_new(void);

int IMAPClientHandler(clientinfo_t * ci);

#endif
