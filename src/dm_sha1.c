/* 
 */

#include "dbmail.h"

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

char * dm_sha1(const char *s)
{
	const char *h;
	MHASH td = mhash_init(MHASH_SHA1);
	mhash(td, s, strlen(s));
	h = mhash_end(td);

	return sha1_digest(h);
}



