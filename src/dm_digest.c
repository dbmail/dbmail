/*
 Copyright (C) 2007 NFG Net Facilities Group support@nfg.nl

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

#include "dbmail.h"
#define THIS_MODULE "digest"

static char * tiger_digest(const unsigned char *hash)
{
	static int bufno;
	static char hexbuffer[4][50];
	static const char hex[] = "0123456789abcdef";
	char *buffer = hexbuffer[3 & ++bufno], *buf = buffer;
	size_t i, j;

	for (i = 0; i < mhash_get_block_size(MHASH_TIGER); i++) {
		if (i<8) j=7-i;
		else if (i<16) j=23-i;
		else j=39-i;
		unsigned int val = hash[j];
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	*buf = '\0';

	return buffer;
}


static char * sha1_digest(const unsigned char *sha1)
{
	static int bufno;
	static char hexbuffer[4][50];
	static const char hex[] = "0123456789abcdef";
	char *buffer = hexbuffer[3 & ++bufno], *buf = buffer;
	size_t i;

	for (i = 0; i < mhash_get_block_size(MHASH_SHA1); i++) {
		unsigned int val = *sha1++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	*buf = '\0';

	return buffer;
}

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

static const char * dm_hash(const unsigned char * const buf, hashid type)
{
	MHASH td = mhash_init(type);
	mhash(td, buf, strlen(buf));
	return mhash_end(td);
}

char * dm_sha1(const unsigned char * const s)
{
	g_return_val_if_fail(s != NULL, NULL);

	return sha1_digest(dm_hash(s, MHASH_SHA1));
}

char * dm_tiger(const unsigned char * const s)
{
	g_return_val_if_fail(s != NULL, NULL);
	
	return tiger_digest(dm_hash(s, MHASH_TIGER));
}

char *dm_md5(const unsigned char * const s)
{
	g_return_val_if_fail(s != NULL, NULL);

	return md5_digest(dm_hash(s, MHASH_MD5));
}

char *dm_md5_base64(const unsigned char * const s)
{
	g_return_val_if_fail(s != NULL, NULL);

	const char *h = dm_hash(s, MHASH_MD5);

	return g_base64_encode(h, sizeof(h));
}

