/*
 Copyright (C) 1999-2005 IC & S  dbmail@ic-s.nl
 Copyright (C) 2004-2005 NFG Net Facilities Group info@nfg.nl

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

/** 
 * \file dbmsgbuf.h
 * 
 * \brief datatypes & function prototypes for the msgbuf system
 * using a mysql database
 *
 * \date August 20, 2003 changed to doxygen
 */

#ifndef _DBMSGBUF_H
#define _DBMSGBUF_H

#include "dbmail.h"

#define MSGBUF_FORCE_UPDATE -1

/**
 * \brief initialises a message headers fetch
 * \param msg_idnr 
 * \return 
 *     - -1 on error
 *     -  0 if already inited (sic) before
 *     -  1 on success
 */

struct DbmailMessage * db_init_fetch(u64_t msg_idnr, int filter);

#define db_init_fetch_headers(x) db_init_fetch(x,DBMAIL_MESSAGE_FILTER_HEAD)
#define db_init_fetch_message(x) db_init_fetch(x,DBMAIL_MESSAGE_FILTER_FULL)

#endif
