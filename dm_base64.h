/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl

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

/* $Id: dm_base64.h 1878 2005-09-04 06:34:44Z paul $
 *
 * Base64 encoding and decoding routines.
 */

#ifndef _BASE64_H
#define _BASE64_H

#include "dbmail.h"

/* out must be preallocated to at least 1.5 * inlen. */
void base64_encode(unsigned char *out, const unsigned char *in, int inlen);

/* returns the length of the decoded string, as it may be raw binary. */
int base64_decode(char *out, const char *in);

/* Free the result with g_strfreev. */
char **base64_decodev(char *in);

#endif
