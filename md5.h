/*
  $Id: md5.h 1032 2004-03-19 16:27:38Z ilja $

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

/* header for md5.h */

#ifndef GdmMD5_H
#define GdmMD5_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

typedef unsigned int uint32;

struct GdmMD5Context {
	uint32 buf[4];
	uint32 bits[2];
	unsigned char in[64];
};

void gdm_md5_init(struct GdmMD5Context *context);
void gdm_md5_update(struct GdmMD5Context *context,
		    unsigned char const *buf, unsigned len);
void gdm_md5_final(unsigned char digest[16],
		   struct GdmMD5Context *context);
void gdm_md5_transform(uint32 buf[4], uint32 const in[16]);

/*
 * This is needed to make RSAREF happy on some MS-DOS compilers.
 */
/* typedef struct gdm_md5_Context gdm_md5__CTX; */

#endif				/* !GdmMD5_H */
