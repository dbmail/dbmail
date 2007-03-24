/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (C) 2004-2006 NFG Net Facilities Group BV support@nfg.nl
 Copyright (C) 2006 Aaron Stone aaron@serendipity.cx

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


 The Base64 code contained herein was written by Eric S. Raymond
 with the statement: "Copyright retained for the purpose
 of protecting free redistribution of source."
*/

#include "dbmail.h"
#define THIS_MODULE "base64"

char base64encodestring[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

#define BAD     -1
static const char base64decodeval[] = {
	BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
	BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
	BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD, 62, BAD,BAD,BAD, 63,
	 52, 53, 54, 55,  56, 57, 58, 59,  60, 61,BAD,BAD, BAD,BAD,BAD,BAD,
	BAD,  0,  1,  2,   3,  4,  5,  6,   7,  8,  9, 10,  11, 12, 13, 14,
	 15, 16, 17, 18,  19, 20, 21, 22,  23, 24, 25,BAD, BAD,BAD,BAD,BAD,
	BAD, 26, 27, 28,  29, 30, 31, 32,  33, 34, 35, 36,  37, 38, 39, 40,
	 41, 42, 43, 44,  45, 46, 47, 48,  49, 50, 51,BAD, BAD,BAD,BAD,BAD
};
#define DECODE64(c)  (isascii(c) ? base64decodeval[c] : BAD)

/* out must be preallocated to at least 1.5 * inlen.
 * I recommend allocating 2*inlen to be surely safe. */
void base64_encode(unsigned char *out, const unsigned char *in, int inlen)
{
	assert(out);

	for (; inlen >= 3; inlen -= 3) {
		*out++ = base64encodestring[in[0] >> 2];
		*out++ = base64encodestring[((in[0] << 4) & 0x30) | (in[1] >> 4)];
		*out++ = base64encodestring[((in[1] << 2) & 0x3c) | (in[2] >> 6)];
		*out++ = base64encodestring[in[2] & 0x3f];
		in += 3;
	}

	if (inlen > 0) {
		unsigned char fragment;

		*out++ = base64encodestring[in[0] >> 2];
		fragment = (in[0] << 4) & 0x30;

		if (inlen > 1)
			fragment |= in[1] >> 4;

		*out++ = base64encodestring[fragment];
		*out++ = (inlen < 2) ? '=' : base64encodestring[(in[1] << 2) & 0x3c];
		*out++ = '=';
	}
	
	*out = '\0';
}

int base64_decode(char *out, const char *in)
{
	int len = 0;
	register unsigned char digit1, digit2, digit3, digit4;

	if (in[0] == '+' && in[1] == ' ')
		in += 2;
	if (*in == '\r')
		return(0);

	do {
		digit1 = in[0];
		if (DECODE64(digit1) == BAD)
			return(-1);
		digit2 = in[1];
		if (DECODE64(digit2) == BAD)
			return(-1);
		digit3 = in[2];
		if (digit3 != '=' && DECODE64(digit3) == BAD)
			return(-1);
		digit4 = in[3];
		if (digit4 != '=' && DECODE64(digit4) == BAD)
			return(-1);
		in += 4;
		*out++ = (DECODE64(digit1) << 2) | (DECODE64(digit2) >> 4);
		++len;
		if (digit3 != '=') {
			*out++ = ((DECODE64(digit2) << 4) & 0xf0) | (DECODE64(digit3) >> 2);
			++len;
			if (digit4 != '=') {
				*out++ = ((DECODE64(digit3) << 6) & 0xc0) | DECODE64(digit4);
				++len;
			}
		}
	} while (*in && *in != '\r' && digit4 != '=');

	return (len);
}


/* A frontend to the base64_decode_internal() that deals with embedded strings. */
char **base64_decodev(char *str)
{
	int i, j, n;
	int numstrings = 0;
	int decodelen = 0;
	char *decoded;
	char **ret = NULL;

	/* Base64 always decodes to a shorter string. */
	decoded = g_new0(char, strlen(str));
	decodelen = base64_decode(decoded, str);

	/* Count up the number of embedded strings... */
	for (i = 0; i <= decodelen; i++) {
		if (decoded[i] == '\0') {
			numstrings++;
		}
	}

	/* Allocate an array large enough
	 * for the strings and a final NULL. */
	ret = g_new0(char *, (numstrings + 1));
	if (ret == NULL) {
		g_free(decoded);
		TRACE(TRACE_WARNING, "could not allocate array of length [%d].", numstrings+1);
		return NULL;
	}

	/* Copy each nul terminated string to the array. */
	for (i = j = n = 0; i <= decodelen; i++) {
		if (decoded[i] == '\0') {
			ret[n] = dm_strdup(decoded + j);
			j = i + 1;
			n++;
		}
	}

	/* Put the final NULL on the end of the array. */
	ret[n] = NULL;

	g_free(decoded);

	return ret;
}

