/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

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


static const char hex[] = "0123456789abcdef";

int dm_digest(const unsigned char * hash, hashid type, char *out)
{
	char *buf = out;
	size_t i, j;

	for (i = 0; i < mhash_get_block_size(type); i++) {
		j = i;
		if (type == MHASH_TIGER) {
			/* compensate for endian-ess */
			if (i<8) j=7-i;
			else if (i<16) j=23-i;
			else j=39-i;
		}
		unsigned int val = hash[j];
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	*buf = '\0';

	return 0;
}

static void dm_hash(const unsigned char * buf, hashid type, gpointer data)
{
	MHASH td = mhash_init(type);
	mhash(td, buf, strlen((const char *)buf));
	mhash_deinit(td, data);
}

#define DM_HASH(x, t, out) \
	g_return_val_if_fail(x != NULL, 1); \
	unsigned char h[1024]; \
	memset(h,'\0', sizeof(h)); \
	dm_hash((unsigned char *)x, t, (gpointer)h); \
	return dm_digest(h, t, out)

int dm_whirlpool(const char * const s, char *out)
{
	DM_HASH(s, MHASH_WHIRLPOOL, out);
}

int dm_sha512(const char * const s, char *out)
{
	DM_HASH(s, MHASH_SHA512, out);
}

int dm_sha256(const char * const s, char *out)
{
	DM_HASH(s, MHASH_SHA256, out);
}

int dm_sha1(const char * const s, char *out)
{
	DM_HASH(s, MHASH_SHA1, out);
}

int dm_tiger(const char * const s, char *out)
{
	DM_HASH(s, MHASH_TIGER, out);
}

int dm_md5(const char * const s, char *out)
{
	DM_HASH(s, MHASH_MD5, out);
}

int dm_md5_base64(const char * const s, char *out)
{
	char *enc;
	g_return_val_if_fail(s != NULL, 1);
	unsigned char h[2048];
	memset(h,'\0', sizeof(h));
	dm_hash((unsigned char *)s, MHASH_MD5, h);
	enc = g_base64_encode(h, sizeof(h));
	g_strlcpy(out, enc, FIELDSIZE);
	return 0;
}

