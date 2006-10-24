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

 $Id: md5.h 1891 2005-10-03 10:01:21Z paul $
*/

#ifndef _MD5_H
#define _MD5_H

/**
 * \brief calculate md5-hash of a string
 * \param buf input string
 * \return md5 hash of buf
 */
char *dm_md5(const unsigned char * const buf);
char *dm_md5_base64(const unsigned char * const buf);

#endif
