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
 * MD5 creation */

#ifndef _DBMD5_H
#define _DBMD5_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/**
 * \brief calculate md5-hash of a string
 * \param buf input string
 * \return md5 hash of buf
 */
unsigned char *makemd5(const char * const buf);

#endif
