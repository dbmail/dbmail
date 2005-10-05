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

#include "dbmail.h"



#undef max
#define max(x,y) ( (x) > (y) ? (x) : (y) )

int drop_privileges(char *newuser, char *newgroup)
{
	/* will drop running program's priviledges to newuser and newgroup */
	struct passwd *pwd;
	struct group *grp;

	grp = getgrnam(newgroup);

	if (grp == NULL) {
		trace(TRACE_ERROR, "%s,%s: could not find group %s\n",
		      __FILE__, __func__, newgroup);
		return -1;
	}

	pwd = getpwnam(newuser);
	if (pwd == NULL) {
		trace(TRACE_ERROR, "%s,%s: could not find user %s\n",
		      __FILE__, __func__, newuser);
		return -1;
	}

	if (setgid(grp->gr_gid) != 0) {
		trace(TRACE_ERROR, "%s,%s: could not set gid to %s\n",
		      __FILE__, __func__, newgroup);
		return -1;
	}

	if (setuid(pwd->pw_uid) != 0) {
		trace(TRACE_ERROR, "%s,%s: could not set uid to %s\n",
		      __FILE__, __func__, newuser);
		return -1;
	}
	return 0;
}

char *itoa(int i)
{
	char *s = (char *) dm_malloc(42); /* Enough for a 128 bit integer */
	if (s)
		sprintf(s, "%d", i);
	return s;
}

void create_unique_id(char *target, u64_t message_idnr)
{
	char *a_message_idnr, *a_rand;
	unsigned char *md5_str;

	a_message_idnr = itoa(message_idnr);
	a_rand = itoa(rand());

	if (message_idnr != 0)
		snprintf(target, UID_SIZE, "%s:%s",
			 a_message_idnr, a_rand);
	else
		snprintf(target, UID_SIZE, "%s", a_rand);
	md5_str = makemd5((unsigned char *)target);
	snprintf(target, UID_SIZE, "%s", (char *)md5_str);
	trace(TRACE_DEBUG, "%s,%s: created: %s", __FILE__, __func__,
	      target);
	dm_free(md5_str);
	dm_free(a_message_idnr);
	dm_free(a_rand);
}

void create_current_timestring(timestring_t * timestring)
{
	time_t td;
	struct tm tm;

	if (time(&td) == -1)
		trace(TRACE_FATAL, "%s,%s: error getting time from OS",
		      __FILE__, __func__);

	tm = *localtime(&td);	/* get components */
	strftime((char *) timestring, sizeof(timestring_t),
		 "%Y-%m-%d %H:%M:%S", &tm);
}

char *mailbox_add_namespace(const char *mailbox_name, u64_t owner_idnr,
			    u64_t user_idnr)
{
	char *fq_name;
	char *owner_name;
	GString *tmp;
	char *tmp_name;

	if (mailbox_name == NULL) {
		trace(TRACE_ERROR, "%s,%s: error, mailbox_name is "
		      "NULL.", __FILE__, __func__);
		return NULL;
	}
	
	tmp_name = g_strdup(mailbox_name);
	trace(TRACE_DEBUG,"%s,%s: mailbox_name [%s], owner_idnr [%llu], user_idnr [%llu]",
			__FILE__, __func__, tmp_name, owner_idnr, user_idnr);


	if (user_idnr == owner_idnr) {
		/* mailbox owned by current user */
		return tmp_name;
	} else {
		tmp = g_string_new("");
		
		owner_name = auth_get_userid(owner_idnr);
		if (owner_name == NULL) {
			trace(TRACE_ERROR, "%s,%s: error owner_name is NULL", 
					__FILE__, __func__);
			return NULL;
		}
		trace(TRACE_DEBUG, "%s,%s: owner name = %s", 
				__FILE__, __func__, 
				owner_name);
		if (strcmp(owner_name, PUBLIC_FOLDER_USER) == 0) {
			g_string_printf(tmp, "%s%s%s", 
					NAMESPACE_PUBLIC, 
					MAILBOX_SEPARATOR, 
					tmp_name);
		} else {
			g_string_printf(tmp, "%s%s%s%s%s", 
					NAMESPACE_USER, 
					MAILBOX_SEPARATOR, 
					owner_name, 
					MAILBOX_SEPARATOR, 
					tmp_name);
		}
		dm_free(owner_name);
		fq_name = tmp->str;
		g_string_free(tmp,FALSE);
		trace(TRACE_INFO, "%s,%s: returning fully qualified name [%s]", 
				__FILE__, __func__, 
				fq_name);
		g_free(tmp_name);
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
		temp = strstr(fq_name, MAILBOX_SEPARATOR);
		if (temp == NULL || strlen(temp) <= 1) {
			trace(TRACE_ERROR,
			      "%s,%s wronly constructed mailbox " "name",
			      __FILE__, __func__);
			return NULL;
		}
		temp = strstr(&temp[1], MAILBOX_SEPARATOR);
		if (temp == NULL || strlen(temp) <= 1) {
			trace(TRACE_ERROR,
			      "%s,%s wronly constructed mailbox " "name",
			      __FILE__, __func__);
			return NULL;
		}
		return &temp[1];
	}
	if (strcmp(fq_name, NAMESPACE_PUBLIC) == 0) {
		temp = strstr(fq_name, MAILBOX_SEPARATOR);

		if (temp == NULL || strlen(temp) <= 1) {
			trace(TRACE_ERROR,
			      "%s,%s wronly constructed mailbox " "name",
			      __FILE__, __func__);
			return NULL;
		}
		return &temp[1];
	}
	return fq_name;
}

int ci_write(FILE * fd, char * msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	
	if (feof(fd) || vfprintf(fd,msg,ap) < 0 || fflush(fd) < 0) {
		va_end(ap);
		return -1;
	}
	va_end(ap);
	return 0;
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
	str_decoded = (char *) dm_malloc(sizeof(char) * maxlen);
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
	ret = (char **) dm_malloc(sizeof(char *) * (numstrings + 1));
	if (ret == NULL)
		return NULL;

	/* If there are more strings, copy those, too */
	for (i = j = n = 0; i <= len_decoded; i++) {
		if (str_decoded[i] == '\0') {
			ret[n] = dm_strdup(str_decoded + j);
			j = i + 1;
			n++;
		}
	}

	/* Put that final NULL on the end of the array */
	ret[n] = NULL;

	dm_free(str_decoded);

	return ret;
}

void base64_free(char **ret)
{
	size_t i;

	if (ret == NULL)
		return;

	for (i = 0; ret[i] != NULL; i++) {
		dm_free(ret[i]);
	}

	dm_free(ret);
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

	tmp_buf = dm_malloc(sizeof(char) * (f_len += 512));
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
			    dm_realloc(f_buf, sizeof(char) * (f_len *= 2));
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
		      __FILE__, __func__, left, right);
		*retchar = NULL;
		*retsize = 0;
		*retlast = 0;
		return -1;
	} else {
		/* Step left up to skip the actual left thinger */
		if (tmpright != tmpleft)
			tmpleft++;

		tmplen = tmpright - tmpleft;
		*retchar = dm_malloc(sizeof(char) * (tmplen + 1));
		if (*retchar == NULL) {
			*retchar = NULL;
			*retsize = 0;
			*retlast = 0;
			trace(TRACE_INFO,
			      "%s, %s: Found [%s] of length [%zd] between '%c' and '%c' so next skip [%zd]",
			      __FILE__, __func__, *retchar, *retsize,
			      left, right, *retlast);
			return -2;
		}
		strncpy(*retchar, tmpleft, tmplen);
		(*retchar)[tmplen] = '\0';
		*retsize = tmplen;
		*retlast = tmpright - value;
		trace(TRACE_INFO,
		      "%s, %s: Found [%s] of length [%zd] between '%c' and '%c' so next skip [%zd]",
		      __FILE__, __func__, *retchar, *retsize, left,
		      right, *retlast);
		return 0;
	}
}

/*
 *
 *
 *  Some basic string handling utilities
 *
 *
 *
 */
GString * g_list_join(GList * list, char * sep)
{
	GString *string = g_string_new("");
	if (sep == NULL)
		sep="";
	if (list == NULL)
		return string;
	list = g_list_first(list);
	string = g_string_append(string, (gchar *)list->data);
	while((list = g_list_next(list))) {
		string = g_string_append(string,sep);
		string = g_string_append(string,(gchar *)list->data);
		if (! g_list_next(list))
			break;
	}
	return string;	
}

GList * g_string_split(GString * string, char * sep)
{
	GList * list = NULL;
	char **array = (char **)g_strsplit((const gchar *)string->str, (const gchar *)sep,0);
	int i, len = 0;
	while(array[len++]);
	len--;
	for (i=0; i<len; i++)
		list = g_list_append(list,g_strdup(array[i]));
	g_strfreev(array);
	return list;
}
/*
 * append a formatted GString to a GList
 */
GList * g_list_append_printf(GList * list, char * format, ...)
{
	char *str = (char *)dm_malloc(sizeof(char) * BUFLEN);
	va_list argp;
	va_start(argp, format);
	vsnprintf(str, sizeof(char) * BUFLEN, format, argp);
	list = g_list_append(list, strdup(str));
	dm_free(str);
	return list;
}

/* 
 * return newly allocated escaped strings
 */

char * dm_stresc(const char * from)
{
	char *to;
	if (! (to = g_new0(char,(strlen(from)+1) * 2 + 1)))
		return NULL;
	db_escape_string(to, from, strlen(from));
	return to;
}

/* 
 *
 * replace tabs with spaces and all multi-spaces with single spaces 
 *
 */

void  dm_pack_spaces(char *in) 
{
	char *tmp, *saved;
	/* replace tabs with spaces */
	g_strdelimit(in,"\t",' ');
	
	/* replace all multi-spaces with single spaces */
	tmp = g_strdup(in);
	saved = tmp;
	while(*tmp) {
		if ((*tmp == ' ') && (*(tmp+1) == ' ')) {
			tmp++;
		} else {
			*in++=*tmp++;
		}
	}
	g_free(saved);
	*in='\0';
}
/* 
 * base-subject
 *
 */

static void _strip_blob_prefix(char *subject)
{
	char *tmp = g_strdup(subject);
	char *saved = tmp;
	if (*tmp == '[') {
		while (*tmp != '\0' && *tmp != ']')
			tmp++;

		if (*tmp != ']') {
			g_free(saved);
			return;
		}

		g_strstrip(++tmp); // skip ']'

		if (strlen(tmp) > 0)
			strncpy(subject,tmp,strlen(tmp)+1);

	}
	g_free(saved);
	return;
}
static void _strip_refwd(char *subject) 
{
	char *tmp, *saved;
	if (! (strncasecmp(subject,"re",2)==0 || strncasecmp(subject,"fw",2)==0))
		return;
	
	tmp = g_strdup(subject);	
	saved = tmp;
	
	if (strncasecmp(tmp,"fwd",3)==0) 
		tmp+=3;
	else if ((strncasecmp(tmp,"re",2)==0) || (strncasecmp(tmp,"fw",2)==0))
		tmp+=2;
	
	g_strstrip(tmp);
	if (strlen(tmp) > 0)
		_strip_blob_prefix(tmp);

	if (*tmp!=':') {
		g_free(saved);
		return;
	}

	g_strstrip(++tmp); // skip ':'
	
	if (strlen(tmp) > 0)
		strncpy(subject,tmp,strlen(tmp)+1);

	g_free(saved);
}
		


static void _strip_sub_leader(char *subject)
{
	unsigned len;
	/* strip blobs prefixes */
	while (1==1) {
		len = strlen(subject);
		_strip_blob_prefix(subject);
		if (strlen(subject)==len)
			break;
	}
	/* strip refwd prefixes */
	_strip_refwd(subject);
}

void dm_base_subject(char *subject)
{
	unsigned offset, len, olen;
	char *tmp, *saved;
	
	tmp = g_strdup(subject);
	saved = tmp;
	dm_pack_spaces(tmp);
	g_strstrip(tmp);
	while (1==1) {
		olen = strlen(tmp);
		while (g_str_has_suffix(tmp,"(fwd)")) {
			offset = strlen(tmp) - strlen("(fwd)");
			tmp[offset] = '\0';
			g_strstrip(tmp);
		}
		while (1==1) {
			len = strlen(tmp);
			_strip_sub_leader(tmp);
			if (strlen(tmp)==len)
				break;
		}


		if (g_str_has_suffix(tmp,"]") && strncasecmp(tmp,"[fwd:",strlen("[fwd:"))==0 ) {
			offset=strlen(tmp)-1;
			tmp[offset]='\0';
			tmp+=5;
			g_strstrip(tmp);
		}
		
		while (g_str_has_prefix(tmp,":") && (strlen(tmp) > 1)) 
			g_strstrip(++tmp);

		if (strlen(tmp)==olen)
			break;
	}
	strncpy(subject,tmp,strlen(tmp)+1);
	g_free(saved);
}

/* 
 * \brief listexpression match for imap (rfc2060) 
 * \param p pattern
 * \param s string to search
 * \param x separator string ("." or "/"- multichar okay; e.g. "π" would work f
 * 	you can find a IMAP client that read rfc2060)
 * \param flags presently only LISTEX_NOCASE -- if you want case-insensitive
 * 	"folders"
 * \return 1 indicates a match
 */
#define LISTEX_NOCASE	1
int listex_match(const char *p, const char *s,
			const char *x, int flags)
{
	int i, p8;
	p8=0;
	trace(TRACE_DEBUG,"%s,%s: pattern [%s], string [%s], delim [%s], flags [%d]",
			__FILE__, __func__, p, s, x, flags);
	while (*p) {
		if (!p8 && *p == '%') {
			p++;
			while (*s) {
				for (i = 0; x[i] && x[i] == s[i]; i++);
				if (! x[i]) {
					s += i;
					break;
				}
				s++;
			}
			/* %. */
			for (i = 0; x[i] && x[i] == p[i]; i++);
			if (! x[i]) p += i;
			if (*s || *p) return 0;
			return 1;

		}
		if (!p8 && *p == '*') {
			/* use recursive for synchronize */
			p++;
			if (!(*p)) return 1;
			while (*s) {
				if (listex_match(p,s,x,flags)) return 1;
				s++;
			}
			return 0;

		}
		
		if (!p8 && *p == *x) {
			for (i = 0; x[i] && p[i] == x[i] && p[i] == s[i]; i++);
			if (! x[i]) {
				p += i; s += i;
				continue; /* sync'd */
			}
			/* fall; try regular search */
		}

		if ((flags & LISTEX_NOCASE && tolower(((unsigned int)*p))
					== tolower(((unsigned int)*s)))
		|| (*p == *s)) {
			p8=(((unsigned char)*p) > 0xC0);
			p++; s++;
		} else {
			/* failed */
			return 0;
		}
	}
	if (*p || *s) return 0;
	return 1;
}

u64_t dm_getguid(unsigned int serverid)
{
        char s[30];
        struct timeval tv;

	assert((int)serverid >= 0);

        if (gettimeofday(&tv,NULL))
                return 0;

        snprintf(s,30,"%ld%06ld%02d", tv.tv_sec, tv.tv_usec,serverid);
        return (u64_t)strtoll(s,NULL,10);
}

sa_family_t dm_get_client_sockaddr(clientinfo_t *ci, struct sockaddr *saddr)
{
	int maxsocklen = 128; /* ref. UNP */
	
	union {
		struct sockaddr sa;
		char data[maxsocklen];
	} un;

	socklen_t len;
	len = maxsocklen;

	if (getsockname(fileno(ci->tx), (struct sockaddr *)un.data, &len) < 0)
		return (sa_family_t) -1;

	memcpy(saddr, &un.sa, sizeof(un.sa));
	return (un.sa.sa_family);
}

int dm_sock_score(const char *base, const char *test)
{
	struct cidrfilter *basefilter, *testfilter;
	int result = 0;
	char *t;

	t = strstr(base,"unix:");
	if (t==base) {
		base = strstr(base,":");
		test = strstr(test,":");
		return (fnmatch(base,test,0) ? 0 : 1);
	}

	t = strstr(base,"inet:");
	if (t!=base)
		return 0;

	if (! test)
		return 0;
	
	basefilter = cidr_new(base);
	testfilter = cidr_new(test);
	
	if (strlen(test)==0) {
		result = 32;
	} else if (basefilter && testfilter) {
		result = cidr_match(basefilter,testfilter);
	}

	cidr_free(basefilter);
	cidr_free(testfilter);
	
	trace(TRACE_DEBUG, "%s,%s: base[%s] test[%s] => [%d]",
			__FILE__, __func__, base, test, result);
	return result;
}

static int socket_match(const char *base, const char *test)
{
	return (dm_sock_score(base,test) ? 0 : 1);

}

int dm_sock_compare(const char *clientsock, const char *sock_allow, const char *sock_deny) 
{
	int result;
	assert(clientsock);
	
	if ( (strlen(sock_allow) == 0) && (strlen(sock_deny) == 0) ) {
		result = DM_SUCCESS;
	} else if (strlen(sock_deny) > 0 && socket_match(sock_deny, clientsock)==0) {
		result = DM_EGENERAL;
	} else if (strlen(sock_allow) > 0  && socket_match(sock_allow, clientsock)==0) {
		result = DM_SUCCESS;
	} else {
		result = DM_EGENERAL;
	}

	trace(TRACE_DEBUG, "%s,%s: clientsock [%s] sock_allow[%s], sock_deny [%s] => [%d]",
			__FILE__, __func__, clientsock, sock_allow, sock_deny, result);
	return result;
	
}


/* dm_valid_format
 * check if str is a valid format string containing a single "%s" for use in
 * printf style calls
 * \return 1 format is invalid
 * \return 0 format is valid
 */
int dm_valid_format(const char *str)
{
        char *left, *right;
        left = index(str,'%');
        right = rindex(str,'%');
        if (! (left && right && left==right))
                return DM_EGENERAL;
        if (*(left+1) != 's')
                return DM_EGENERAL;
        return DM_SUCCESS;
}

/* Finds the folder if included in an address between a '+' and '@'.
 * Does not allocate memory since that should already be done prior to
 * calling this function.  The string must be shorter if anything.
 * retchar will have the new string or the original value if there is
 * no folder to remove.
 *
 * Return values are:
 *   0 on success (found)
 *   -1 on failure (not found)
 *
 * */
int dm_strip_folder(char **retchar, size_t * retsize)
{
	char *value;
	char *tmpleft;
	char *tmpright;
	size_t tmplen;

	char left = '+';
	char right = '@';

	value = *retchar;
	tmpleft = value;
	tmpright = value + strlen(value);

	while (tmpleft[0] != left && tmpleft < tmpright)
		tmpleft++;
	while (tmpright[0] != right && tmpright > tmpleft)
		tmpright--;

	if (tmpleft[0] != left || tmpright[0] != right) {
		trace(TRACE_INFO,
		      "%s, %s: No folder found.",
		      __FILE__, __func__);
		*retsize = strlen(*retchar);
		return -1;
	} else {
		tmplen = strlen(value) - (strlen(tmpleft) - strlen(tmpright));
		strncpy(*retchar, value, (strlen(value) - strlen(tmpleft)));
		(*retchar)[(strlen(value) - strlen(tmpleft))] = '\0';
		strncat(*retchar, tmpright, strlen(tmpright));
		(*retchar)[tmplen] = '\0';
		*retsize = tmplen;
		trace(TRACE_INFO,
		      "%s, %s: Found [%s] of length [%zd] around '%c' and '%c'.",
		      __FILE__, __func__, *retchar, *retsize, left,
		      right);
		return 0;
	}
}

/* Function to determine if the requested mailbox exists or is valid to
 * be created for the specified userid
 *
 * Return values are:
 *   1 on success (existing or allowed to be created)
 *   0 on failure (does not exist not allowed to be created)
 *
 * */

int dm_valid_folder(const char *userid, char *folder)
{
	field_t val;
	char tmp[IMAP_MAX_MAILBOX_NAMELEN+2] = "";
	u64_t useridnr;
	u64_t mailboxidnr;
	u64_t *children = NULL;
	unsigned nchildren;
	int result;

	/* We need to get the user_idnr first */
	if (auth_user_exists(userid, &useridnr)) {
		/* Create the pattern to search on the folder list. */
		tmp[0] = '^';
		strncat(&tmp[1], folder, 78);
		
		/* Only search subscribed folders for delivery. */
		result = db_findmailbox_by_regex(useridnr, tmp, &children, &nchildren, 1);

		/* Invalid response, deal with it. */
		if (result != 0) {
			trace(TRACE_INFO, "%s, %s: Folder [%s] regex failure for [%s].",
					   __FILE__, __func__, folder, userid);
			return 0;
		}

		/* If we got some children back we are home free. */
		if (nchildren > 0) {

			mailboxidnr = children[0];	/* Save the mailboxid for later. */
			dm_free(children);		/* Done with the children. */

			/* Set the folder name to what is in the database, this is */
			/* to avoid mixed case differences.  See README.folders. */
			result = db_getmailboxname(mailboxidnr, useridnr, folder);

			/* No success, then bail out and assume INBOX */
			if (result == -1) {
				trace(TRACE_INFO, "%s, %s: Folder [%s] lookup failure for [%s].",
						   __FILE__, __func__, folder, userid);
				return 0;
			}

			/* Otherwise we now have the foldername as appears in the DB. */
			trace(TRACE_INFO, "%s, %s: mailbox [%s] exists for [%s].",
					   __FILE__, __func__, folder, userid);
			return 1;
		}

		/* No existing folder, are we allowed to create it? */
		config_get_value("CREATE_FOLDERS", "DBMAIL", val);

		/* Reset the tmp string and get our search item. */
		/* we can cheat and just modify our initial search string some */
		tmp[0] = ':';
		strcat(tmp, ":");

		if (strcasestr(val, tmp) != NULL) {
			trace(TRACE_INFO, "%s,%s: folder [%s] is in create list [%s]",
					   __FILE__, __func__, tmp, val);
			return 1;
		} else {
			trace(TRACE_INFO, "%s,%s: folder [%s] not in create list [%s]",
					   __FILE__, __func__, tmp, val);

		}
	}

	trace(TRACE_INFO, "%s,%s: failed to find or create folder [%s] for [%s], using INBOX",
			   __FILE__, __func__, folder, userid);

	/* Either user doesn't exist, or not allowed to create. */
	return 0;
}

