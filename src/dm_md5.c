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

/*
 * This code implements the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 *
 * To compute the message digest of a chunk of bytes, declare an
 * GdmMD5Context structure, pass it to gdm_md5_init, call
 * gdm_md5_update as needed on buffers full of bytes, and then call
 * gdm_md5_final, which will fill a supplied 16-byte array with the
 * digest. 
 *
 * Changed all names to avoid namespace pollution -- mkp
 *
 */

#include "dbmail.h"
#define THIS_MODULE "md5"

static char * md5_digest(const unsigned char *md5)
{
	static int bufno;
	static char hexbuffer[4][34];
	static const char hex[] = "0123456789abcdef";
	char *buffer = hexbuffer[3 & ++bufno], *buf = buffer;
	size_t i;

	for (i = 0; i < mhash_get_block_size(MHASH_MD5); i++) {
		unsigned int val = *md5++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	*buf = '\0';

	return buffer;
}


char *dm_md5(const unsigned char * const buf)
{
	char *md5hash;
	int i;

	if (buf == NULL) {
		TRACE(TRACE_ERROR, "received NULL argument");
		return NULL;
	}

	const char *h;
	MHASH td = mhash_init(MHASH_MD5);
	mhash(td, buf, strlen(buf));
	h = mhash_end(td);

	return md5_digest(h);
}

/* Always returns an allocation of 18 bytes. */
char *dm_md5_base64(const unsigned char * const buf)
{
	if (buf == NULL) {
		TRACE(TRACE_ERROR, "received NULL argument");
		return NULL;
	}

	const char *h;
	MHASH td = mhash_init(MHASH_MD5);
	mhash(td, buf, strlen(buf));
	h = mhash_end(td);

	return g_base64_encode(h, sizeof(h));
}

