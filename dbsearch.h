/* $Id: dbsearch.h 1562 2005-01-14 15:37:44Z paul $

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

/**
 * \file dbsearch.h
 *
 * \brief Function prototypes for search functionality. 
 */

#ifndef _DBSEARCH_H
#define _DBSEARCH_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmailtypes.h"


/**
 * \brief searches the dbase for messages belonging to mailbox mb
 * and matching the specified key
 * entries of rset will be set for matching msgs 
 * (using their MSN as identifier)
 * \param rset array index by MSN
 * \param setlen length of rset
 * \param key key to match
 * \param mb mailbox
 * \param type search type. If this is IST_IDATE, it is treated differently 
 * from other searches
 * \return 
 *     - 2 on memory error
 *     - -1 on database error
 *     - 0 on success
 *     - 1 on synchronisation error. (search returned a UID which was not
 *     in the MSN-list -> mailbox should be updated
 */
int db_search(unsigned int *rset, int setlen, const char *key, mailbox_t * mb,
	      int type);

/**
 * \brief search messages in mailbox mb that match the search key sk. To
 * be used with search keys that require message parsing
 * \param rset array indexed by MSN
 * \param setlen length of rset
 * \param sk search key
 * \param mb mailbox
 * \return 
 *    - 0 on success
 *    - 1 if mb->exists != setlen
 */
int db_search_parsed(unsigned int *rset, unsigned setlen,
		     search_key_t * sk, mailbox_t * mb, int condition);


int db_sort_parsed(unsigned int *rset, unsigned setlen, 
		     search_key_t *sk, mailbox_t *mb, int condition);
 
#endif
