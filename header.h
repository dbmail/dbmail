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

/* $Id$ */


#ifndef _HEADER_H
#define _HEADER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "list.h"

/**
 * \brief Read from the specified FILE pointer until either
 * a long carriage-return line-feed or a lone period stand
 * on a line by themselves.
 * \param instream A FILE pointer to the stream where the header is.
 * \param headerrfcsize The size of the header if all lines ended in \r\n.
 * \param headersize The actual byte count of the header.
 * \param header A pointer to an unallocated char array. On
 * error, the pointer may not be valid and must not be used.
 * \return
 *      - 1 on success
 *      - 0 on failure
*/
int read_header(FILE * instream, u64_t * newlines, u64_t * headersize,
		char **header);

#endif
