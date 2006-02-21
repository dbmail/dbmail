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

/*	$Id: misc.c 1982 2006-02-15 14:45:48Z aaron $
 *
 *	Miscelaneous functions */

#include "dbmail.h"

const char AcceptedMailboxnameChars[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789-=/ _.&,+@()[]";

/**
 * abbreviated names of the months
 */
const char *month_desc[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* returned by date_sql2imap() */
#define IMAP_STANDARD_DATE "Sat, 03-Nov-1979 00:00:00 +0000"
char _imapdate[IMAP_INTERNALDATE_LEN] = IMAP_STANDARD_DATE;

/* returned by date_imap2sql() */
#define SQL_STANDARD_DATE "1979-11-03 00:00:00"
char _sqldate[SQL_INTERNALDATE_LEN + 1] = SQL_STANDARD_DATE;

const int month_len[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

#undef max
#define max(x,y) ( (x) > (y) ? (x) : (y) )

/* only locally used */				     
typedef struct {
	GTree *tree;
	GList *list;
	int condition;
} tree_merger_t;

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

void create_unique_id(char *target, u64_t message_idnr)
{
	char *a_message_idnr, *a_rand;
	unsigned char *md5_str;

	a_message_idnr = g_strdup_printf("%llu",message_idnr);
	a_rand = g_strdup_printf("%d",rand());

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
	g_free(a_message_idnr);
	g_free(a_rand);
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
	char *fq;
	char *owner;
	GString *t;

	if (mailbox_name == NULL) {
		trace(TRACE_ERROR, "%s,%s: error, mailbox_name is NULL.",
				__FILE__, __func__);
		return NULL;
	}
	
	if (user_idnr == owner_idnr) 
		/* mailbox owned by current user */
		return g_strdup(mailbox_name);
	
	/* else */
	t = g_string_new("");
	
	if ((owner = auth_get_userid(owner_idnr))==NULL)
		return NULL;
	
	if (strcmp(owner, PUBLIC_FOLDER_USER) == 0)
		g_string_printf(t, "%s%s%s", NAMESPACE_PUBLIC, MAILBOX_SEPARATOR, mailbox_name);
	else
		g_string_printf(t, "%s%s%s%s%s", NAMESPACE_USER, MAILBOX_SEPARATOR, owner, 
				MAILBOX_SEPARATOR, mailbox_name);
	dm_free(owner);
	
	fq = t->str;
	g_string_free(t,FALSE);
	
	return fq;
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
int find_bounded(const char * const value, char left, char right,
		char **retchar, size_t * retsize, size_t * retlast)
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

int zap_between(const char * const instring, signed char left, signed char right,
		char **outstring, size_t *outlen, size_t *zaplen)
{
	char *start, *end;
	char *incopy = g_strdup(instring);
	int clipleft = 0, clipright = 0;

	if (!incopy)
		return -2;

	// Should we clip the left char, too?
	if (left < 0) {
		left = 0 - left;
		clipleft = 1;
	}

	// Should we clip the right char, too?
	if (right < 0) {
		right = 0 - right;
		clipright = 1;
	}

	start = strchr(incopy, left);
	end = strrchr(incopy, right);

	if (!start || !end)
		return -1;

	if (!clipleft) start++;
	if (clipright) end++;

	memmove(start, end, strlen(end)+1);

	if (outstring)
		*outstring = incopy;
	if (outlen)
		*outlen = strlen(incopy);
	if (zaplen)
		*zaplen = (end - start);

	return 0;
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
	char **array;
	int i, len = 0;
	
	if (string->len == 0)
		return NULL;
	
	array = (char **)g_strsplit((const gchar *)string->str, (const gchar *)sep,0);
	while(array[len++]);
	len--;
	for (i=0; i<len; i++)
		list = g_list_append(list,g_strdup(array[i]));
	g_strfreev(array);
	return list;
}
/*
 * append a formatted string to a GList
 */
GList * g_list_append_printf(GList * list, char * format, ...)
{
	va_list argp;
	va_start(argp, format);
	return g_list_append(list, g_strdup_vprintf(format, argp));
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


/*
 * checkmailboxname()
 *
 * performs a check to see if the mailboxname is valid
 * returns 0 if invalid, 1 otherwise
 */
int checkmailboxname(const char *s)
{
	int i;

	if (strlen(s) == 0)
		return 0;	/* empty name is not valid */

	if (strlen(s) >= IMAP_MAX_MAILBOX_NAMELEN)
		return 0;	/* a too large string is not valid */

	/* check for invalid characters */
	for (i = 0; s[i]; i++) {
		if (!strchr(AcceptedMailboxnameChars, s[i])) {
			/* dirty hack to allow namespaces to function */
			if (i == 0 && s[0] == '#')
				continue;
			/* wrong char found */
			return 0;
		}
	}

	/* check for double '/' */
	for (i = 1; s[i]; i++) {
		if (s[i] == '/' && s[i - 1] == '/')
			return 0;
	}

	/* check if the name consists of a single '/' */
	if (strlen(s) == 1 && s[0] == '/')
		return 0;

	return 1;
}


/*
 * check_date()
 *
 * checks a date for IMAP-date validity:
 * dd-MMM-yyyy
 * 01234567890
 * month three-letter specifier
 */
int check_date(const char *date)
{
	char sub[4];
	int days, i, j;

	if (strlen(date) != strlen("01-Jan-1970")
	    && strlen(date) != strlen("1-Jan-1970"))
		return 0;

	j = (strlen(date) == strlen("1-Jan-1970")) ? 1 : 0;

	if (date[2 - j] != '-' || date[6 - j] != '-')
		return 0;

	days = strtoul(date, NULL, 10);
	strncpy(sub, &date[3 - j], 3);
	sub[3] = 0;

	for (i = 0; i < 12; i++) {
		if (strcasecmp(month_desc[i], sub) == 0)
			break;
	}

	if (i >= 12 || days > month_len[i])
		return 0;

	for (i = 7; i < 11; i++)
		if (!isdigit(date[i - j]))
			return 0;

	return 1;
}



/*
 * check_msg_set()
 *
 * checks if s represents a valid message set 
 */
int check_msg_set(const char *s)
{
	int i, indigit=0, result = 1;
	

	if (!s || (!isdigit(s[0]) && s[0]!= '*') )
		return 0;

	for (i = 0; s[i]; i++) {
		if (isdigit(s[i]) || s[i]=='*')
			indigit = 1;
		else if (s[i] == ',') {
			if (!indigit) {
				result = 0;
				break;
			}

			indigit = 0;
		} else if (s[i] == ':') {
			if (!indigit) {
				result = 0;
				break;
			}

			indigit = 0;
		} else {
			result = 0;
			break;
		}
	}
	trace(TRACE_DEBUG, "%s,%s: [%s] [%s]", __FILE__, __func__, s, result ? "ok" : "fail" );

	return result;
}


/*
 * convert a mySQL date (yyyy-mm-dd hh:mm:ss) to a valid IMAP internal date:
 * dd-mon-yyyy hh:mm:ss with mon characters (i.e. 'Apr' for april)
 * return value is valid until next function call.
 * NOTE: if date is not valid, IMAP_STANDARD_DATE is returned
 */
char *date_sql2imap(const char *sqldate)
{
        struct tm tm_sql_date;
	struct tm tm_imap_localtime;
	
	time_t ltime;
        char *last;

	// bsd needs:
	memset(&tm_sql_date, 0, sizeof(struct tm));
	
        last = strptime(sqldate,"%Y-%m-%d %H:%M:%S", &tm_sql_date);
        if ( (last == NULL) || (*last != '\0') ) {
                strcpy(_imapdate, IMAP_STANDARD_DATE);
                return _imapdate;
        }

	/* FIXME: this works fine on linux, but may cause dst offsets in netbsd. */
//	ltime = mktime (&tm_sql_date);
//	localtime_r(&ltime, &tm_imap_localtime);
//	localtime_r(&ltime, &tm_sql_date);

        strftime(_imapdate, sizeof(_imapdate), "%a, %d %b %Y %H:%M:%S %z", &tm_sql_date);
        return _imapdate;
}


/*
 * convert TO a mySQL date (yyyy-mm-dd) FROM a valid IMAP internal date:
 *                          0123456789
 * dd-mon-yyyy with mon characters (i.e. 'Apr' for april)
 * 01234567890
 * OR
 * d-mon-yyyy
 * return value is valid until next function call.
 */
char *date_imap2sql(const char *imapdate)
{
	struct tm tm;
	char *last_char;

	// bsd needs this:
	memset(&tm, 0, sizeof(struct tm));

	last_char = strptime(imapdate, "%d-%b-%Y", &tm);
	if (last_char == NULL || *last_char != '\0') {
		trace(TRACE_DEBUG, "%s,%s: error parsing IMAP date %s",
		      __FILE__, __func__, imapdate);
		return NULL;
	}
	(void) strftime(_sqldate, SQL_INTERNALDATE_LEN,
			"%Y-%m-%d 00:00:00", &tm);

	return _sqldate;
}


int num_from_imapdate(const char *date)
{
	int j = 0, i;
	char datenum[] = "YYYYMMDD";
	char sub[4];

	if (date[1] == ' ' || date[1] == '-')
		j = 1;

	strncpy(datenum, &date[7 - j], 4);

	strncpy(sub, &date[3 - j], 3);
	sub[3] = 0;

	for (i = 0; i < 12; i++) {
		if (strcasecmp(sub, month_desc[i]) == 0)
			break;
	}

	i++;
	if (i > 12)
		i = 12;

	sprintf(&datenum[4], "%02d", i);

	if (j) {
		datenum[6] = '0';
		datenum[7] = date[0];
	} else {
		datenum[6] = date[0];
		datenum[7] = date[1];
	}

	return atoi(datenum);
}

void g_list_destroy(GList *l)
{
	g_list_foreach(l,(GFunc)g_free,NULL);
	g_list_free(l);
}

static gboolean traverse_tree_keys(gpointer key, gpointer value UNUSED, GList **l)
{
	*(GList **)l = g_list_append(*(GList **)l, key);
	return FALSE;
}
static gboolean traverse_tree_values(gpointer key UNUSED, gpointer value, GList **l)
{
	*(GList **)l = g_list_append(*(GList **)l, value);
	return FALSE;
}

GList * g_tree_keys(GTree *tree)
{
	GList *l = NULL;
	g_tree_foreach(tree, (GTraverseFunc)traverse_tree_keys, &l);
	return g_list_first(l);
}
GList * g_tree_values(GTree *tree)
{
	GList *l = NULL;
	g_tree_foreach(tree, (GTraverseFunc)traverse_tree_values, &l);
	return g_list_first(l);
}


/*
 * boolean merge of two GTrees. The result is stored in GTree *a.
 * the state of GTree *b is undefined: it may or may not have been changed, 
 * depending on whether or not key/value pairs were moved from b to a.
 * Both trees are safe to destroy afterwards, assuming g_tree_new_full was used
 * for their construction.
 */

static gboolean tree_print(gpointer key, gpointer value, gpointer data UNUSED)
{
	if (! (key && value))
		return TRUE;

	u64_t *k = (u64_t *)key;
	u64_t *v = (u64_t *)value;
	trace(TRACE_DEBUG,"%s,%s: %llu: %llu\n", __FILE__, __func__, *k, *v);
	return FALSE;
}

void tree_dump(GTree *t)
{
	trace(TRACE_DEBUG,"%s,%s: start",__FILE__,__func__);
	g_tree_foreach(t,(GTraverseFunc)tree_print,NULL);
	trace(TRACE_DEBUG,"%s,%s: done",__FILE__,__func__);
}

	      
static gboolean traverse_tree_merger(gpointer key, gpointer value UNUSED, tree_merger_t **merger)
{
	tree_merger_t *t = *(tree_merger_t **)merger;
	GTree *tree = t->tree;
	int condition = t->condition;

	switch(condition) {
		case IST_SUBSEARCH_NOT:
		break;
		
		default:
		case IST_SUBSEARCH_OR:
		case IST_SUBSEARCH_AND:
			if (! g_tree_lookup(tree,key)) 
				(*(tree_merger_t **)merger)->list = g_list_append((*(tree_merger_t **)merger)->list,key);
		break;
	}

	return FALSE;
}


int g_tree_merge(GTree *a, GTree *b, int condition)
{
	char *type = NULL;
	GList *keys;
	int alen = 0, blen=0;
	
	gpointer key;
	gpointer value;	
	
	g_return_val_if_fail(a && b,1);
	
	tree_merger_t *merger = g_new0(tree_merger_t,1);
	
	alen = g_tree_nnodes(a);
	blen = g_tree_nnodes(b);
	
	switch(condition) {
		case IST_SUBSEARCH_AND:
			type=g_strdup("AND");
			/* delete from A all keys not in B */
			merger->tree = b;
			merger->condition = IST_SUBSEARCH_AND;
			g_tree_foreach(a,(GTraverseFunc)traverse_tree_merger, &merger);
			
			keys = g_list_first(merger->list);
			if (! g_list_length(keys))
				break;

			while (keys->data) {
				g_tree_remove(a,keys->data);
				if (! g_list_next(keys))
					break;
				
				keys = g_list_next(keys);
			}
			break;
			
		case IST_SUBSEARCH_OR:
			type=g_strdup("OR");
			
			if (! g_tree_nnodes(b) > 0)
				break;

			merger->tree = a;
			merger->condition = IST_SUBSEARCH_OR;
			g_tree_foreach(b,(GTraverseFunc)traverse_tree_merger, &merger);
			keys = g_list_first(merger->list);
		
			/* add to A all keys in B */
			if (! g_list_length(keys))
				break;

			while (keys->data) {
				g_tree_lookup_extended(b,keys->data,&key,&value);
				g_tree_steal(b,keys->data);
				g_tree_insert(a,key,value);

				if (! g_list_next(keys))
					break;
				
				keys = g_list_next(keys);
			}
			break;
			
		case IST_SUBSEARCH_NOT:
			type=g_strdup("NOT");
			
			keys = g_tree_keys(b);
			
			if (! g_list_length(keys))
				break;
			
			while (keys->data) {
				// remove from A keys also in B 
				if (g_tree_lookup(a,keys->data)) {
					g_tree_remove(a,keys->data);
				} else {
					// add to A all keys in B not in A 
			 		g_tree_lookup_extended(b,keys->data,&key,&value);
					g_tree_steal(b,keys->data);
					g_tree_insert(a,key,value);
				}
				
				if (! g_list_next(keys))
					break;
				
				keys = g_list_next(keys);
			}
			break;
	}

	trace(TRACE_DEBUG,"%s,%s: a[%d] [%s] b[%d] -> a[%d]",
			__FILE__, __func__, 
			alen, type, blen, 
			g_tree_nnodes(a));

	g_free(merger);
	g_free(type);

	return 0;
}

gint ucmp(const u64_t *a, const u64_t *b)
{
	u64_t x,y;
	x = (u64_t)*a;
	y = (u64_t)*b;
	
	if (x>y)
		return 1;
	if (x==y)
		return 0;
	return -1;
}
