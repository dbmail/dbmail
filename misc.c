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

/*	$Id$
 *
 *	Miscelaneous functions */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "auth.h"
#include "dbmail.h"
#include "dbmd5.h"
#include "misc.h"


int drop_privileges(char *newuser, char *newgroup)
{
	/* will drop running program's priviledges to newuser and newgroup */
	struct passwd *pwd;
	struct group *grp;

	grp = getgrnam(newgroup);

	if (grp == NULL) {
		trace(TRACE_ERROR, "%s,%s: could not find group %s\n",
		      __FILE__, __FUNCTION__, newgroup);
		return -1;
	}

	pwd = getpwnam(newuser);
	if (pwd == NULL) {
		trace(TRACE_ERROR, "%s,%s: could not find user %s\n",
		      __FILE__, __FUNCTION__, newuser);
		return -1;
	}

	if (setgid(grp->gr_gid) != 0) {
		trace(TRACE_ERROR, "%s,%s: could not set gid to %s\n",
		      __FILE__, __FUNCTION__, newgroup);
		return -1;
	}

	if (setuid(pwd->pw_uid) != 0) {
		trace(TRACE_ERROR, "%s,%s: could not set uid to %s\n",
		      __FILE__, __FUNCTION__, newuser);
		return -1;
	}
	return 0;
}

char *itoa(int i)
{
	char *s = (char *) malloc(42);	/* Enough for a 128 bit integer */
	if (s)
		sprintf(s, "%d", i);
	return s;
}

void create_unique_id(char *target, u64_t message_idnr)
{
	char *a_message_idnr, *a_rand;
	char *md5_str;

	a_message_idnr = itoa(message_idnr);
	a_rand = itoa(rand());

	if (message_idnr != 0)
		snprintf(target, UID_SIZE, "%s:%s",
			 a_message_idnr, a_rand);
	else
		snprintf(target, UID_SIZE, "%s", a_rand);
	md5_str = makemd5(target);
	snprintf(target, UID_SIZE, "%s", md5_str);
	trace(TRACE_DEBUG, "%s,%s: created: %s", __FILE__, __FUNCTION__,
	      target);
	my_free(md5_str);
	my_free(a_message_idnr);
	my_free(a_rand);
}

void create_current_timestring(timestring_t * timestring)
{
	time_t td;
	struct tm tm;

	if (time(&td) == -1)
		trace(TRACE_FATAL, "%s,%s: error getting time from OS",
		      __FILE__, __FUNCTION__);

	tm = *localtime(&td);	/* get components */
	strftime((char *) timestring, sizeof(timestring_t),
		 "%Y-%m-%d %H:%M:%S", &tm);
}

char *mailbox_add_namespace(const char *mailbox_name, u64_t owner_idnr,
			    u64_t user_idnr)
{
	char *fq_name;
	char *owner_name;
	size_t fq_name_len;

	if (mailbox_name == NULL) {
		trace(TRACE_ERROR, "%s,%s: error, mailbox_name is "
		      "NULL.", __FILE__, __FUNCTION__);
		return NULL;
	}

	if (user_idnr == owner_idnr) {
		/* mailbox owned by current user */
		return strdup(mailbox_name);
	} else {
		owner_name = auth_get_userid(owner_idnr);
		if (owner_name == NULL) {
			trace(TRACE_ERROR,
			      "%s,%s: error owner_name is NULL", __FILE__,
			      __FUNCTION__);
			return NULL;
		}
		trace(TRACE_ERROR, "%s,%s: owner name = %s", __FILE__,
		      __FUNCTION__, owner_name);
		if (strcmp(owner_name, PUBLIC_FOLDER_USER) == 0) {
			fq_name_len = strlen(NAMESPACE_PUBLIC) +
			    strlen(MAILBOX_SEPERATOR) +
			    strlen(mailbox_name) + 1;
			if (!(fq_name = my_malloc(fq_name_len *
						  sizeof(char)))) {
				trace(TRACE_ERROR,
				      "%s,%s: not enough memory", __FILE__,
				      __FUNCTION__);
				return NULL;
			}
			snprintf(fq_name, fq_name_len, "%s%s%s",
				 NAMESPACE_PUBLIC, MAILBOX_SEPERATOR,
				 mailbox_name);
		} else {
			fq_name_len = strlen(NAMESPACE_USER) +
			    strlen(MAILBOX_SEPERATOR) +
			    strlen(owner_name) +
			    strlen(MAILBOX_SEPERATOR) +
			    strlen(mailbox_name) + 1;
			if (!(fq_name = my_malloc(fq_name_len *
						  sizeof(char)))) {
				trace(TRACE_ERROR,
				      "%s,%s: not enough memory", __FILE__,
				      __FUNCTION__);
				return NULL;
			}
			snprintf(fq_name, fq_name_len, "%s%s%s%s%s",
				 NAMESPACE_USER, MAILBOX_SEPERATOR,
				 owner_name, MAILBOX_SEPERATOR,
				 mailbox_name);
		}
		my_free(owner_name);
		trace(TRACE_INFO, "%s,%s: returning fully qualified name "
		      "[%s]", __FILE__, __FUNCTION__, fq_name);
		return fq_name;
	}
}

const char *mailbox_remove_namespace(const char *fq_name)
{
	char *temp;

	/* a lot of strlen() functions are used here, so this
	   can be quite inefficient! On the other hand, this
	   is a function that's not used that much. */
	if (strcmp(fq_name, NAMESPACE_USER) == 0) {
		temp = strstr(fq_name, MAILBOX_SEPERATOR);
		if (temp == NULL || strlen(temp) <= 1) {
			trace(TRACE_ERROR,
			      "%s,%s wronly constructed mailbox " "name",
			      __FILE__, __FUNCTION__);
			return NULL;
		}
		temp = strstr(&temp[1], MAILBOX_SEPERATOR);
		if (temp == NULL || strlen(temp) <= 1) {
			trace(TRACE_ERROR,
			      "%s,%s wronly constructed mailbox " "name",
			      __FILE__, __FUNCTION__);
			return NULL;
		}
		return &temp[1];
	}
	if (strcmp(fq_name, NAMESPACE_PUBLIC) == 0) {
		temp = strstr(fq_name, MAILBOX_SEPERATOR);

		if (temp == NULL || strlen(temp) <= 1) {
			trace(TRACE_ERROR,
			      "%s,%s wronly constructed mailbox " "name",
			      __FILE__, __FUNCTION__);
			return NULL;
		}
		return &temp[1];
	}
	return fq_name;
}

/* This base64 code is heavily modified from fetchmail.
 *
 * Original copyright notice:
 *
 * The code in the fetchmail distribution is Copyright 1997 by Eric
 * S. Raymond.  Portions are also copyrighted by Carl Harris, 1993
 * and 1995.  Copyright retained for the purpose of protecting free
 * redistribution of source.
 * */

#define BAD     -1
static const char base64val[] = {
	BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD,
	    BAD, BAD, BAD,
	BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD,
	    BAD, BAD, BAD,
	BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, BAD, 62, BAD,
	    BAD, BAD, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, BAD, BAD, BAD, BAD, BAD,
	    BAD,
	BAD, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, BAD, BAD, BAD, BAD,
	    BAD,
	BAD, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, BAD, BAD, BAD, BAD, BAD
};
#define DECODE64(c)  (isascii(c) ? base64val[c] : BAD)

/* Base64 to raw bytes in quasi-big-endian order */
/* Returns 0 on success, -1 on failure */
int base64_decode_internal(const char *in, size_t inlen, size_t maxlen,
			   char *out, size_t * outlen)
{
	size_t pos = 0;
	size_t len = 0;
	register unsigned char digit1, digit2, digit3, digit4;

	/* Don't even bother if the string is too short */
	if (inlen < 4)
		return -1;

	do {
		digit1 = in[0];
		if (DECODE64(digit1) == BAD)
			return -1;
		digit2 = in[1];
		if (DECODE64(digit2) == BAD)
			return -1;
		digit3 = in[2];
		if (digit3 != '=' && DECODE64(digit3) == BAD)
			return -1;
		digit4 = in[3];
		if (digit4 != '=' && DECODE64(digit4) == BAD)
			return -1;
		in += 4;
		pos += 4;
		++len;
		if (maxlen && len > maxlen)
			return -1;
		*out++ = (DECODE64(digit1) << 2) | (DECODE64(digit2) >> 4);
		if (digit3 != '=') {
			++len;
			if (maxlen && len > maxlen)
				return -1;
			*out++ =
			    ((DECODE64(digit2) << 4) & 0xf0) |
			    (DECODE64(digit3) >> 2);
			if (digit4 != '=') {
				++len;
				if (maxlen && len > maxlen)
					return -1;
				*out++ =
				    ((DECODE64(digit3) << 6) & 0xc0) |
				    DECODE64(digit4);
			}
		}
	} while (pos < inlen && digit4 != '=');

	*out = '\0';

	*outlen = len;
	return 0;
}

/* A frontend to the base64_decode_internal() that deals with embedded strings */
char **base64_decode(char *str, size_t len)
{
	size_t i, j, n, maxlen;
	size_t numstrings = 0;
	char *str_decoded = NULL;
	size_t len_decoded = 0;
	char **ret = NULL;

	/* Base64 encoding required about 40% more space.
	 * So we'll allocate 50% more space. */
	maxlen = 3 * len / 2;
	str_decoded = (char *) malloc(sizeof(char) * maxlen);
	if (str_decoded == NULL)
		return NULL;

	if (0 !=
	    base64_decode_internal(str, len, maxlen, str_decoded,
				   &len_decoded))
		return NULL;
	if (str_decoded == NULL)
		return NULL;

	/* Count up the number of embedded strings... */
	for (i = 0; i <= len_decoded; i++) {
		if (str_decoded[i] == '\0') {
			numstrings++;
		}
	}

	/* Allocate an array of arrays large enough
	 * for the strings and a terminating NULL */
	ret = (char **) malloc(sizeof(char *) * (numstrings + 1));
	if (ret == NULL)
		return NULL;

	/* If there are more strings, copy those, too */
	for (i = j = n = 0; i <= len_decoded; i++) {
		if (str_decoded[i] == '\0') {
			ret[n] = strdup(str_decoded + j);
			j = i + 1;
			n++;
		}
	}

	/* Put that final NULL on the end of the array */
	ret[n] = NULL;

	my_free(str_decoded);

	return ret;
}

void base64_free(char **ret)
{
	size_t i;

	if (ret == NULL)
		return;

	for (i = 0; ret[i] != NULL; i++) {
		my_free(ret[i]);
	}

	my_free(ret);
}

/* Return 0 is all's well. Returns something else if not... */
int read_from_stream(FILE * instream, char **m_buf, size_t maxlen)
{
	size_t f_len = 0;
	size_t f_pos = 0;
	char *tmp_buf = NULL;
	char *f_buf = NULL;

	/* Give up on a zero length request */
	if (maxlen < 1) {
		*m_buf = NULL;
		return 0;
	}

	tmp_buf = malloc(sizeof(char) * (f_len += 512));
	if (tmp_buf != NULL)
		f_buf = tmp_buf;
	else
		return -2;

	/* Shouldn't this also check for ferror() or feof() ?? */
	while (f_pos < maxlen) {
		if (f_pos + 1 >= f_len) {
			/* Per suggestion of my CS instructor, double the
			 * buffer every time it is too small. This yields
			 * a logarithmic number of reallocations. */
			tmp_buf =
			    realloc(f_buf, sizeof(char) * (f_len *= 2));
			if (tmp_buf != NULL)
				f_buf = tmp_buf;
			else
				return -2;
		}
		f_buf[f_pos] = fgetc(instream);
		f_pos++;
	}

	if (f_pos)
		f_buf[f_pos] = '\0';

	*m_buf = f_buf;

	return 0;
}

/* Finds what lurks between two bounding symbols.
 * Allocates and fills retchar with the string.
 *
 * Return values are:
 *   0 on success (found and allocated)
 *   -1 on failure (not found)
 *   -2 on memory error (found but allocation failed)
 *
 * The caller is responsible for free()ing *retchar.
 * */
int find_bounded(char *value, char left, char right, char **retchar,
		 size_t * retsize, size_t * retlast)
{
	char *tmpleft;
	char *tmpright;
	size_t tmplen;

	tmpleft = value;
	tmpright = value + strlen(value);

	while (tmpleft[0] != left && tmpleft < tmpright)
		tmpleft++;
	while (tmpright[0] != right && tmpright > tmpleft)
		tmpright--;

	if (tmpleft[0] != left || tmpright[0] != right) {
		trace(TRACE_INFO,
		      "%s, %s: Found nothing between '%c' and '%c'",
		      __FILE__, __FUNCTION__, left, right);
		*retchar = NULL;
		*retsize = 0;
		*retlast = 0;
		return -1;
	} else {
		/* Step left up to skip the actual left thinger */
		if (tmpright != tmpleft)
			tmpleft++;

		tmplen = tmpright - tmpleft;
		*retchar = my_malloc(sizeof(char) * (tmplen + 1));
		if (*retchar == NULL) {
			*retchar = NULL;
			*retsize = 0;
			*retlast = 0;
			trace(TRACE_INFO,
			      "%s, %s: Found [%s] of length [%zd] between '%c' and '%c' so next skip [%zd]",
			      __FILE__, __FUNCTION__, *retchar, *retsize,
			      left, right, *retlast);
			return -2;
		}
		strncpy(*retchar, tmpleft, tmplen);
		(*retchar)[tmplen] = '\0';
		*retsize = tmplen;
		*retlast = tmpright - value;
		trace(TRACE_INFO,
		      "%s, %s: Found [%s] of length [%zd] between '%c' and '%c' so next skip [%zd]",
		      __FILE__, __FUNCTION__, *retchar, *retsize, left,
		      right, *retlast);
		return 0;
	}
}
