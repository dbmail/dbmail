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

/**
 * split the whole message into header and body
 * \param[in] whole_message the whole message, including header
 * \param[in] whole_message_size size of whole_message.
 * \param[out] header will hold header 
 * \param[out] header_size size of header
 * \param[out] header_rfcsize rfc size of header
 * \param[out] body will hold body
 * \param[out] body_size size of body
 * \param[out] body_rfcsize rfc size of body
 */
int split_message(const char *whole_message, 
		  u64_t whole_message_size,
		  char **header, u64_t *header_size,
		  u64_t *header_rfcsize,
		  const char **body, u64_t *body_size,
		  u64_t *body_rfcsize);

#endif
