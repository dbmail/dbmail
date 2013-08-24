/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2012 NFG Net Facilities Group BV support@nfg.nl

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
 *
 *	Miscelaneous functions */


#include "dbmail.h"
#include "dm_mailboxstate.h"

#define THIS_MODULE "misc"

const char AcceptedMailboxnameChars[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789-=/ _.&,+@()[]'#";

/**
 * abbreviated names of the months
 */
const char *month_desc[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* returned by date_sql2imap() */
#define IMAP_STANDARD_DATE "03-Nov-1979 00:00:00 +0000"

/* returned by date_imap2sql() */
#define SQL_STANDARD_DATE "1979-11-03 00:00:00"

const int month_len[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

extern const char *imap_flag_desc_escaped[];

extern struct DbmailIconv *ic;

#undef max
#define max(x,y) ( (x) > (y) ? (x) : (y) )

/* only locally used */				     
typedef struct {
	GTree *tree;
	GList *list;
	int condition;
} tree_merger_t;

void g_string_maybe_shrink(GString *s)
{
	if (s->len+1 < s->allocated_len) {
		s->str = g_realloc(s->str, s->len+1);
		s->allocated_len = s->len+1;
	}
}

int drop_privileges(char *newuser, char *newgroup)
{
	/* will drop running program's priviledges to newuser and newgroup */
	struct passwd pwd;
	struct passwd *presult;
	struct group grp;
	struct group *gresult;
	char buf[16384];
	int s;

	memset(buf,0,sizeof(buf));

	s = getgrnam_r(newgroup, &grp, buf, sizeof(buf), &gresult);
	if (gresult == NULL) {
		if (s == 0)
			TRACE(TRACE_ERR, "could not find group %s\n", newgroup);
		return -1;
	}

	s = getpwnam_r(newuser, &pwd, buf, sizeof(buf), &presult);
	if (presult == NULL) {
		if (s == 0)
			TRACE(TRACE_ERR, "could not find user %s\n", newuser);
		return -1;
	}

	if (setgid(grp.gr_gid) != 0) {
		TRACE(TRACE_ERR, "could not set gid to %s\n", newgroup);
		return -1;
	}

	if (setuid(pwd.pw_uid) != 0) {
		TRACE(TRACE_ERR, "could not set uid to %s\n", newuser);
		return -1;
	}
	return 0;
}

void create_unique_id(char *target, uint64_t message_idnr)
{
	char md5_str[FIELDSIZE];
	if (message_idnr != 0) {
		snprintf(target, UID_SIZE, "%" PRIu64 ":%ld", message_idnr, random());
	} else {
		snprintf(target, UID_SIZE, "%ld", random());
	}

	memset(md5_str, 0, sizeof(md5_str));
	dm_md5(target, md5_str);
	snprintf(target, UID_SIZE, "%s", md5_str);

	TRACE(TRACE_DEBUG, "created: %s", target);
}

void create_current_timestring(TimeString_T * timestring)
{
	time_t td;
	struct tm tm;

	if (time(&td) == -1)
		TRACE(TRACE_EMERG, "error getting time from OS");

	memset(&tm,0,sizeof(tm));
	localtime_r(&td, &tm);	/* get components */
	strftime((char *) timestring, sizeof(TimeString_T),
		 "%Y-%m-%d %H:%M:%S", &tm);
}

char *mailbox_add_namespace(const char *mailbox_name, uint64_t owner_idnr,
			    uint64_t user_idnr)
{
	char *fq;
	char *owner;
	GString *t;

	if (mailbox_name == NULL) {
		TRACE(TRACE_ERR, "error, mailbox_name is NULL.");
		return NULL;
	}
	
	if (user_idnr == owner_idnr) 
		/* mailbox owned by current user */
		return g_strdup(mailbox_name);
	
	/* else */
	
	if ((owner = auth_get_userid(owner_idnr))==NULL)
		return NULL;

	t = g_string_new("");
	if (strcmp(owner, PUBLIC_FOLDER_USER) == 0)
		g_string_printf(t, "%s%s%s", NAMESPACE_PUBLIC, MAILBOX_SEPARATOR, mailbox_name);
	else
		g_string_printf(t, "%s%s%s%s%s", NAMESPACE_USER, MAILBOX_SEPARATOR, owner, 
				MAILBOX_SEPARATOR, mailbox_name);
	g_free(owner);
	
	fq = t->str;
	g_string_free(t,FALSE);
	
	return fq;
}

/* Strips off the #Users or #Public namespace, returning
 * the simple name, the namespace and username, if present. */

char *mailbox_remove_namespace(char *name, char **namespace, char **username)
{
	char *temp = NULL, *user = NULL;
	size_t ns_user_len = 0;
	size_t ns_publ_len = 0;
	size_t fq_name_len;
	char *mbox = NULL, *fq_name = (char *)name;

	ns_user_len = strlen(NAMESPACE_USER);
	ns_publ_len = strlen(NAMESPACE_PUBLIC);

	if (username) *username = NULL;
	if (namespace) *namespace = NULL;

	fq_name_len = strlen(fq_name);
	while (fq_name_len) {
		if (! g_str_has_suffix(fq_name,"/"))
			break;
		fq_name_len--;
		fq_name[fq_name_len] = '\0';
	}

 	TRACE(TRACE_DEBUG,"[%s]", fq_name);

	// i.e. '#Users/someuser/foldername'
	// assume a slash in '#Users/foo*' and '#Users/foo%' like this '#Users/foo/*'
	if (fq_name_len >= ns_user_len && strncasecmp(fq_name, NAMESPACE_USER, ns_user_len) == 0) {
		if (namespace) *namespace = NAMESPACE_USER;

		int end = 0, err = 0, slash = 0;
		// We'll use a simple state machine to parse through this.
		for (temp = &fq_name[ns_user_len]; !end && !err; temp++) {
			switch (*temp) {
			case '/':
				if (!user) {
					user = temp + 1;
				} else if (user && !mbox) {
					slash = 1;
					if (strlen(temp+1) && (*(temp+1) != '/'))
						mbox = temp + 1;
				} else if (user && mbox) {
					end = 1;
				}
				break;
			case '*':
				mbox = temp;
				break;
			case '%':
				mbox = temp;
				break;
			case '\0':
				end = 1;
				break;
			}
		}

		if (err) {
			TRACE(TRACE_NOTICE, "Illegal mailbox name");
			return NULL;
		}

		if (mbox && strlen(mbox) && (!user || (user + slash == mbox))) {
			TRACE(TRACE_DEBUG, "Username not found, returning mbox [%s]", mbox);
			return mbox;
		}

		if (!mbox) {
			TRACE(TRACE_DEBUG, "Mailbox not found");
			return NULL;
		}

		TRACE(TRACE_DEBUG, "Copying out username [%s] of length [%zu]", user, (size_t)(mbox - user - slash));
		if (username) *username = g_strndup(user, mbox - user - slash);

		TRACE(TRACE_DEBUG, "returning [%s]", mbox);
		return mbox;
	}
	
	// i.e. '#Public/foldername'
	// accept #Public* and #Public% also
	if (fq_name_len >= ns_publ_len && strncasecmp(fq_name, NAMESPACE_PUBLIC, ns_publ_len) == 0) {
		if (namespace) *namespace = NAMESPACE_PUBLIC;
		if (username) *username = g_strdup(PUBLIC_FOLDER_USER);
		// Drop the slash between the namespace and the mailbox spec
		if (fq_name[ns_publ_len] == '/')
			return &fq_name[ns_publ_len+1]; 
		// But if the slash wasn't there, it means we have #Public*, and that's OK.
		return &fq_name[ns_publ_len]; // FIXME: leakage
	}
	
	return fq_name;
}
/* Finds what lurks between two bounding symbols.
 * Allocates and fills retchar with the string.
 *
 * Return values are:
 *   0 or greater (size of item found inbounds)
 *   -1 on failure (bounds not found)
 *
 * The caller is responsible for free()ing *retchar.
 * */
int find_bounded(const char * const value, char left, char right,
		char **retchar, size_t * retsize, size_t * retlast)
{
	char *tmpleft;
	char *tmpright;
	size_t tmplen;

	tmpleft = (char *)value;
	tmpright = (char *)(value + strlen(value));

	while (tmpleft[0] != left && tmpleft < tmpright)
		tmpleft++;
	while (tmpright[0] != right && tmpright > tmpleft)
		tmpright--;

	if (tmpleft[0] != left || tmpright[0] != right) {
		TRACE(TRACE_INFO, "Missing part or all of our bounding points");
		*retchar = NULL;
		*retsize = 0;
		*retlast = 0;
		return -1;
	} 
	/* else */
	/* Step left up to skip the actual left bound */
	if (tmpright != tmpleft)
		tmpleft++;

	tmplen = tmpright - tmpleft;
	*retchar = g_new0(char, tmplen + 1);
	strncpy(*retchar, tmpleft, tmplen);
	(*retchar)[tmplen] = '\0';
	*retsize = tmplen;
	*retlast = tmpright - value;
	TRACE(TRACE_INFO, "Found [%s] of length [%zu] between '%c' and '%c' so next skip [%zu]", *retchar, *retsize, left,
	      right, *retlast);
	return *retlast;
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
		left = (signed char)(0 - left);
		clipleft = 1;
	}

	// Should we clip the right char, too?
	if (right < 0) {
		right = (signed char)(0 - right);
		clipright = 1;
	}

	start = strchr(incopy, left);
	end = strrchr(incopy, right);

	if (!start || !end) {
		g_free(incopy);
		return -1;
	}

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

GList * g_string_split(GString * string, const gchar * sep)
{
	GList * list = NULL;
	char **array;
	int i = 0;
	
	if (string->len == 0)
		return NULL;
	
	array = (char **)g_strsplit((const gchar *)string->str, sep, 0);
	while(array[i])
		list = g_list_append(list,array[i++]);

	g_free(array);

	return list;
}
char * g_strcasestr(const char *haystack, const char *needle)
{
	// Like strstr, but case insensitive.
	size_t n = strlen(needle);
	for (; *haystack; haystack++) {
		if (g_ascii_strncasecmp(haystack, needle, n) == 0)
			return (char *)haystack;
	}

	return NULL;
}

/* 
 * replace all multi-spaces with single spaces 
 */
void pack_char(char *in, char c)
{
	char *saved;
	char *tmp = g_strdup(in);
	saved = tmp;
	while(*tmp) {
		if ((*tmp == c) && (*(tmp+1) == c))
			tmp++;
		else
			*in++=*tmp++;
	}
	g_free(saved);
	*in='\0';
}

/* 
 *
 * replace tabs with spaces and all multi-spaces with single spaces 
 *
 */

void  dm_pack_spaces(char *in) 
{
	/* replace tabs with spaces */
	g_strdelimit(in,"\t",' ');
	pack_char(in,' ');
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

char * dm_base_subject(const char *subject)
{
	unsigned offset, len, olen;
	char *tmp, *saved;
	
	// we expect utf-8 or 7-bit data
	if (subject == NULL) return NULL;
	if (g_mime_utils_text_is_8bit((unsigned char *)subject, strlen(subject))) 
		tmp = g_strdup(subject);
	else 
		tmp = dbmail_iconv_decode_text(subject);
	saved = tmp;
	
	dm_pack_spaces(tmp);
	g_strstrip(tmp);
	while (1==1) {
		olen = strlen(tmp);
		while (g_str_has_suffix(tmp,"(fwd)")) {
			offset = strlen(tmp) - 5;
			tmp[offset] = '\0';
			g_strstrip(tmp);
		}
		while (1==1) {
			len = strlen(tmp);
			_strip_sub_leader(tmp);
			if (strlen(tmp)==len)
				break;
		}

		if (g_str_has_suffix(tmp,"]") && strncasecmp(tmp,"[fwd:",5)==0 ) {
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
	tmp = g_strdup(tmp);
	g_free(saved);
	
	return tmp;
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
				/* fast-forward until after next separator in string */
				for (i = 0; x[i] && x[i] == s[i]; i++);
				if (! x[i]) {
					s += i;
					break;
				}
				/* %foo. */
				for (i = 0; s[i] && p[i] && s[i] == p[i] && s[i] != x[0] && p[i] != x[0]; i++);
				if (i > 0 && ((! p[i] ) || (p[i] == x[0] && s[i] == x[0]))) {
					p += i; s += i;
				} else {
					s++;
				}
			}
			/* %. */
			for (i = 0; x[i] && x[i] == p[i]; i++);
			if (! x[i]) p += i;
			if (*s && *p) return listex_match(p,s,x,flags); // We have more to look at - lets look again.
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

		if ( (*p == *s)||
		((flags & LISTEX_NOCASE) && (tolower(*p) == tolower(*s)))) {
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

uint64_t dm_getguid(unsigned int serverid)
{
        char s[30];
        struct timeval tv;

	assert((int)serverid >= 0);

        if (gettimeofday(&tv,NULL))
                return 0;

        snprintf(s,30,"%ld%06ld%02u", tv.tv_sec, tv.tv_usec,serverid);
        return (uint64_t)strtoll(s,NULL,10);
}

int dm_sock_score(const char *base, const char *test)
{
	Cidr_T basefilter, testfilter;
	int result = 0;
	char *t;

	if ((! base) || (! test))
		return 0;
		
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

	cidr_free(&basefilter);
	cidr_free(&testfilter);
	
	return result;
}

static int socket_match(const char *base, const char *test)
{
	return (dm_sock_score(base,test) ? TRUE : FALSE);

}

int dm_sock_compare(const char *clientsock, const char *sock_allow, const char *sock_deny) 
{
	int result = TRUE;
	assert(clientsock);
	
	if ( (strlen(sock_allow) == 0) && (strlen(sock_deny) == 0) )
		result = TRUE;
	else if (strlen(sock_deny) && socket_match(sock_deny, clientsock))
		result = FALSE;
	else if (strlen(sock_allow))
		result = socket_match(sock_allow, clientsock);

	TRACE(TRACE_DEBUG, "clientsock [%s] sock_allow[%s], sock_deny [%s] => [%d]", clientsock, sock_allow, sock_deny, result);
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
 
// Define len if "01-Jan-1970" string
#define STRLEN_MINDATA	11

int check_date(const char *date)
{
	char sub[4];
	int days, i, j=1;
	size_t l;

	l = strlen(date);

	if (l != STRLEN_MINDATA && l != STRLEN_MINDATA-1) return 0;

	j = (l==STRLEN_MINDATA) ? 0 : 1;

	if (date[2 - j] != '-' || date[6 - j] != '-') return 0;

	days = strtoul(date, NULL, 10);
	strncpy(sub, &date[3 - j], 3);
	sub[3] = 0;

	for (i = 0; i < 12; i++) {
		if (strcasecmp(month_desc[i], sub) == 0) break;
	}

	if (i >= 12 || days > month_len[i]) return 0;

	for (i = 7; i < 11; i++)
		if (!isdigit(date[i - j])) return 0;

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
	
	if (!s || (!isdigit(s[0]) && s[0]!= '*') ) return 0;

	for (i = 0; s[i]; i++) {
		if (isdigit(s[i]) || s[i]=='*') indigit = 1;
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
	TRACE(TRACE_DEBUG, "[%s] [%s]", s, result ? "ok" : "fail" );
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
	char *last;
	char _imapdate[IMAP_INTERNALDATE_LEN] = IMAP_STANDARD_DATE;
	char q[IMAP_INTERNALDATE_LEN];

	// bsd needs:
	memset(&tm_sql_date, 0, sizeof(struct tm));
	
	last = strptime(sqldate,"%Y-%m-%d %H:%M:%S", &tm_sql_date);
	if ( (last == NULL) || (*last != '\0') ) {
		// Could not convert date - something went wrong - using default IMAP_STANDARD_DATE
		strcpy(_imapdate, IMAP_STANDARD_DATE);
		return g_strdup(_imapdate);
	}

	strftime(q, sizeof(q), "%d-%b-%Y %H:%M:%S", &tm_sql_date);
	snprintf(_imapdate,IMAP_INTERNALDATE_LEN, "%s +0000", q);
	
	return g_strdup(_imapdate);
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
int date_imap2sql(const char *imapdate, char *sqldate)
{
	struct tm tm;
	char *last_char;
	size_t l = strlen(imapdate);

	assert(sqldate);

	if ((l < 10) || (l > 11)) {
		TRACE(TRACE_DEBUG, "invalid size IMAP date [%s]", imapdate);
		return 1;
	}

	// bsd needs this:
	memset(&tm, 0, sizeof(struct tm));
	last_char = strptime(imapdate, "%d-%b-%Y", &tm);
	if (last_char == NULL || *last_char != '\0') {
		TRACE(TRACE_DEBUG, "error parsing IMAP date %s", imapdate);
		return 1;
	}
	(void) strftime(sqldate, SQL_INTERNALDATE_LEN-1, "%Y-%m-%d 00:00:00", &tm);

	return 0;
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
static gboolean traverse_tree_keys(gpointer key, gpointer value UNUSED, GList **l)
{
	*(GList **)l = g_list_prepend(*(GList **)l, key);
	return FALSE;
}

static gboolean traverse_tree_values(gpointer key UNUSED, gpointer value, GList **l)
{
	*(GList **)l = g_list_prepend(*(GList **)l, value);
	return FALSE;
}

GList * g_tree_keys(GTree *tree)
{
	GList *l = NULL;
	g_tree_foreach(tree, (GTraverseFunc)traverse_tree_keys, &l);
	return g_list_reverse(l);
}
GList * g_tree_values(GTree *tree)
{
	GList *l = NULL;
	g_tree_foreach(tree, (GTraverseFunc)traverse_tree_values, &l);
	return g_list_reverse(l);
}


/*
 * boolean merge of two GTrees. The result is stored in GTree *a.
 * the state of GTree *b is undefined: it may or may not have been changed, 
 * depending on whether or not key/value pairs were moved from b to a.
 * Both trees are safe to destroy afterwards, assuming g_tree_new_full was used
 * for their construction.
 */
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
				(*(tree_merger_t **)merger)->list = g_list_prepend((*(tree_merger_t **)merger)->list,key);
		break;
	}

	return FALSE;
}

int g_tree_merge(GTree *a, GTree *b, int condition)
{
	char *type = NULL;
	GList *keys = NULL;
	int alen = 0, blen=0, klen=0;
	
	gpointer key;
	gpointer value;	
	
	g_return_val_if_fail(a && b,1);
	
	tree_merger_t *merger = g_new0(tree_merger_t,1);
	
	alen = g_tree_nnodes(a);
	blen = g_tree_nnodes(b);
	
	switch(condition) {
		case IST_SUBSEARCH_AND:

			if (! g_tree_nnodes(a) > 0)
				break;

			type=g_strdup("AND");
			/* delete from A all keys not in B */
			merger->tree = b;
			merger->condition = IST_SUBSEARCH_AND;
			g_tree_foreach(a,(GTraverseFunc)traverse_tree_merger, &merger);
			keys = g_list_first(merger->list);
			if (! (klen = g_list_length(keys)))
				break;
			if (klen > 1)
				keys = g_list_reverse(merger->list);
			
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
			if (! (klen = g_list_length(keys)))
				break;
			if (klen > 1)
				keys = g_list_reverse(keys);
			
			/* add to A all keys in B */
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

			if (! g_tree_nnodes(b) > 0)
				break;
			
			keys = g_tree_keys(b);
			
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

			keys = g_list_first(keys);
			g_list_free(keys);

			break;
	}

	TRACE(TRACE_DEBUG,"(%p) (%p): a[%d] [%s] b[%d] -> a[%d]", 
			a, b, alen, type, blen, 
			g_tree_nnodes(a));

	merger->list = g_list_first(merger->list);
	g_list_free(merger->list);


	g_free(merger);
	g_free(type);

	return 0;
}

gint ucmp(const uint64_t *a, const uint64_t *b)
{
	uint64_t x,y;
	x = (uint64_t)*a;
	y = (uint64_t)*b;
	
	if (x>y)
		return 1;
	if (x==y)
		return 0;
	return -1;
}

gint ucmpdata(const uint64_t *a, const uint64_t *b, gpointer data UNUSED)
{
	return ucmp(a,b);
}

gint dm_strcmpdata(gconstpointer a, gconstpointer b, gpointer data UNUSED)
{
	return strcmp((const char *)a, (const char *)b);
}

gint dm_strcasecmpdata(gconstpointer a, gconstpointer b, gpointer data UNUSED)
{
	return g_ascii_strcasecmp((const char *)a, (const char *)b);
}


/* Read from instream until ".\r\n", discarding what is read. */
int discard_client_input(ClientBase_T *ci)
{
	int c = 0, n = 0;

	while ((read(ci->rx, (void *)&c, 1)) == 1) {
		if (c == '\r') {
			if (n == 4) n = 5;	 /*  \r\n.\r    */
			else n = 1; 		 /*  \r         */
		} else if (c == '\n') {
			if (n == 1) n = 2;	 /*  \r\n       */
			else if (n == 5)	 /*  \r\n.\r\n  DONE */
				break;
			else 			 /*  .\n ?      */
				TRACE(TRACE_ERR, "bare LF.");
		} else if (c == '.' && n == 3)   /*  \r\n.      */
			n = 4;
		
	}
	return 0;
}

/* Following the advice of:
 * "Secure Programming for Linux and Unix HOWTO"
 * Chapter 8: Carefully Call Out to Other Resources */
char * dm_shellesc(const char * command)
{
	char *safe_command;
	int pos, end, len;

	// These are the potentially unsafe characters:
	// & ; ` ' \ " | * ? ~ < > ^ ( ) [ ] { } $ \n \r
	// # ! \t \ (space)

	len = strlen(command);
	if (! (safe_command = g_new0(char,(len + 1) * 2 + 1)))
		return NULL;

	for (pos = end = 0; pos < len; pos++) {
		switch (command[pos]) {
		case '&':
		case ';':
		case '`':
		case '\'':
		case '\\':
		case '"':
		case '|':
		case '*':
		case '?':
		case '~':
		case '<':
		case '>':
		case '^':
		case '(':
		case ')':
		case '[':
		case ']':
		case '{':
		case '}':
		case '$':
		case '\n':
		case '\r':
		case '\t':
		case ' ':
		case '#':
		case '!':
			// Add an escape before the offending char.
			safe_command[end++] = '\\';
		default:
			// And then put in the character itself.
			safe_command[end++] = command[pos];
			break;
		}
	}

	/* The string is already initialized,
	 * but let's be extra double sure. */
	safe_command[end] = '\0';

	return safe_command;
}

/* some basic imap type utils */
char *dbmail_imap_plist_collapse(const char *in)
{
	/*
	 * collapse "(NIL) (NIL)" to "(NIL)(NIL)"
	 *
	 * do for bodystructure, and only for addresslists in the envelope
	 */
	char *p;
	char **sublists;

	g_return_val_if_fail(in,NULL);
	
	sublists = g_strsplit(in,") (",0);
	p = g_strjoinv(")(",sublists);
	g_strfreev(sublists);
	return p;
}

/*
 *  build a parenthisized list (4.4) from a GList
 */
char *dbmail_imap_plist_as_string(GList * list)
{
	char *p;
	size_t l;
	GString * tmp1 = g_string_new("");
	GString * tmp2 = g_list_join(list, " ");
	g_string_printf(tmp1,"(%s)", tmp2->str);

	/*
	 * strip empty outer parenthesis
	 * "((NIL NIL))" to "(NIL NIL)" 
	 */
	p = tmp1->str;
	l = tmp1->len;
	while (tmp1->len>4 && p[0]=='(' && p[l-1]==')' && p[1]=='(' && p[l-2]==')') {
		tmp1 = g_string_truncate(tmp1,l-1);
		tmp1 = g_string_erase(tmp1,0,1);
		p=tmp1->str;
	}
	
	g_string_free(tmp1,FALSE);
	g_string_free(tmp2,TRUE);
	return p;
}

void dbmail_imap_plist_free(GList *l)
{
	g_list_destroy(l);
}

/* 
 * return a quoted or literal astring
 */

char *dbmail_imap_astring_as_string(const char *s)
{
	int i;
	const char *p;
	char *r, *t, *l = NULL;
	char first, last, penult = '\\';

	if (! s)
		return g_strdup("\"\"");

	l = g_strdup(s);
	t = l;
	/* strip off dquote */
	first = s[0];
	last = s[strlen(s)-1];
	if (strlen(s) > 2)
		penult = s[strlen(s)-2];
	if ((first == '"') && (last == '"') && (penult != '\\')) {
		l[strlen(l)-1] = '\0';
		l++;
	}
	
	for (i=0; l[i]; i++) { 
		if ((l[i] & 0x80) || (l[i] == '\r') || (l[i] == '\n') || (l[i] == '"') || (l[i] == '\\')) {
			if ((l[i] == '"') && (i>0) && (l[i-1] != '\\'))
				p = s;
			else
				p = l;
			r = g_strdup_printf("{%" PRIu64 "}\r\n%s", (uint64_t) strlen(p), p);
			g_free(t);
			return r;
		}
		
	}
	r = g_strdup_printf("\"%s\"", l);
	g_free(t);

	return r;

}
/* structure and envelope tools */
static void _structure_part_handle_part(GMimeObject *part, gpointer data, gboolean extension);
static void _structure_part_text(GMimeObject *part, gpointer data, gboolean extension);
static void _structure_part_multipart(GMimeObject *part, gpointer data, gboolean extension);
static void _structure_part_message_rfc822(GMimeObject *part, gpointer data, gboolean extension);


static GList * imap_append_hash_as_string(GList *list, const char *type)
{
	size_t i = 0;
	char curr = 0;
	char name[512];
	char value[1024];
	char *head = (char *)type;
	GList *l = NULL;

	if (! type)
		return list;

	while (type[i]) {
		curr = type[i++];
		if (curr == ';') {
			break;
		}
	}

	while (type[i]) {
		curr = type[i];
		if ((curr == '\n') || (curr == ' ') || (curr == '\t')) {
			i++;
			continue;
		}
		break;
	}

	head += i;

	int offset = 0;
	int inname = 1;
	TRACE(TRACE_DEBUG, "analyse [%s]", head);
	while (head) {
		curr = head[offset];
		if ((! curr) && (offset==0))
			break;
		if (curr == '=' && inname) {
			memset(name, 0, sizeof(name));
			strncpy(name, head, offset);
			g_strstrip(name);
			head += offset+1;
			inname = 0;
			offset = 0;
			TRACE(TRACE_DEBUG, "name: %s", name);
			l = g_list_append_printf(l, "\"%s\"", name);
			continue;
		} else if ((! curr) || (curr == ';')) {
			size_t len;
			char *clean1, *clean2, *clean3;
			memset(value, 0, sizeof(value));
			strncpy(value, head, offset);
			head += offset+1;
			inname = 1;
			offset = 0;

			clean1 = value;
			if (clean1[0] == '"')
				clean1++;

			len = strlen(clean1);
			if (clean1[len-1] == '"')
				clean1[len-1] = '\0';

			clean2 = g_strcompress(clean1);

			if (g_mime_utils_text_is_8bit((const unsigned char *)clean2, strlen(clean2))) {
				clean1 = g_mime_utils_header_encode_text(clean2);
				g_free(clean2);
				clean2 = clean1;
			}
			clean3 = g_strescape(clean2, NULL);
			g_free(clean2);

			TRACE(TRACE_DEBUG, "value: %s", value);
			TRACE(TRACE_DEBUG, "clean: %s", clean3);
			l = g_list_append_printf(l, "\"%s\"", clean3);

			g_free(clean3);

			if (! curr)
				break;
		}
		offset ++;
	}

	if (l) {
		char *s = dbmail_imap_plist_as_string(l);
		TRACE(TRACE_DEBUG, "plist from content-type: %s", s);
		list = g_list_append_printf(list, "%s", s);
		g_free(s);
		
		g_list_destroy(l);
	} else {
		TRACE(TRACE_DEBUG, "plist from content-type: NIL");
		list = g_list_append_printf(list, "NIL");
	}
	

	return list;
}

static GList * imap_append_disposition_as_string(GList *list, GMimeObject *part)
{
	GList *t = NULL;
	GMimeContentDisposition *disposition;
	char *result;
	const char *disp = g_mime_object_get_header(part, "Content-Disposition");
	
	if(disp) {
		disposition = g_mime_content_disposition_new_from_string(disp);
		t = g_list_append_printf(t,"\"%s\"",
				g_mime_content_disposition_get_disposition(disposition));
		
		/* paramlist */
		t = imap_append_hash_as_string(t, disp);

		g_object_unref(disposition);
		
		result = dbmail_imap_plist_as_string(t);
		list = g_list_append_printf(list,"%s",result);
		g_free(result);

		g_list_destroy(t);
	} else {
		list = g_list_append_printf(list,"NIL");
	}
	return list;
}

#define imap_append_header_as_string(list, part, header) \
	imap_append_header_as_string_default(list, part, header, "NIL")

static GList * imap_append_header_as_string_default(GList *list,
		GMimeObject *part, const char *header, char *def)
{
	char *result;
	char *s;
	if((result = (char *)g_mime_object_get_header(part, header))) {
		s = dbmail_imap_astring_as_string(result);
		list = g_list_append_printf(list, "%s", s);
		g_free(s);
	} else {
		list = g_list_append_printf(list, def);
	}
	return list;
}

static void imap_part_get_sizes(GMimeObject *part, size_t *size, size_t *lines)
{
	char *v = NULL, curr = 0, prev = 0;
	int i = 0;
	size_t s = 0, l = 1;

	/* count body lines */
	v = g_mime_object_get_body(part);
	if (! v) return;
	s = strlen(v);

	while (v[i]) {
		curr = v[i];
		if (ISLF(curr))
			l++;
		if (ISLF(curr) && (! ISCR(prev))) // rfcsize
			s++;
		prev = curr;
		i++;
	}

//	if (s >=2 && (! ISLF(prev)))
//		l++;
	
	g_free(v);

	*size = s;
	*lines = l;
}


void _structure_part_handle_part(GMimeObject *part, gpointer data, gboolean extension)
{
	GMimeContentType *type;
	GMimeObject *object;

	if (GMIME_IS_MESSAGE(part))
		object = g_mime_message_get_mime_part(GMIME_MESSAGE(part));
	else
		object = part;
	
	type = g_mime_object_get_content_type(object);
	if (! type) {
		return;
	}

	/* multipart composite */
	if (g_mime_content_type_is_type(type,"multipart","*"))
		_structure_part_multipart(object,data, extension);
	/* message included as mimepart */
	else if (g_mime_content_type_is_type(type,"message","rfc822"))
		_structure_part_message_rfc822(object,data, extension);
	/* simple message */
	else
		_structure_part_text(object,data, extension);

}

void _structure_part_multipart(GMimeObject *part, gpointer data, gboolean extension)
{
	GMimeMultipart *multipart;
	GMimeObject *subpart, *object;
	GList *list = NULL;
	GList *alist = NULL;
	GString *s;
	int i,j;
	GMimeContentType *type;
	gchar *b;
	
	if (GMIME_IS_MESSAGE(part))
		object = g_mime_message_get_mime_part(GMIME_MESSAGE(part));
	else
		object = part;
	
	type = g_mime_object_get_content_type(object);
	if (! type){
		return;
	}
	multipart = GMIME_MULTIPART(object);
	i = g_mime_multipart_get_count(multipart);
	
	
	b = g_mime_content_type_to_string(type);
	TRACE(TRACE_DEBUG,"parse [%d] parts for [%s] with boundary [%s]", 
			i, b, g_mime_multipart_get_boundary(multipart));
	g_free(b);

	/* loop over parts for base info */
	for (j=0; j<i; j++) {
		subpart = g_mime_multipart_get_part(multipart,j);
		_structure_part_handle_part(subpart,&alist,extension);
	}
	
	/* sub-type */
	alist = g_list_append_printf(alist,"\"%s\"", type->subtype);

	/* extension data (only for multipart, in case of BODYSTRUCTURE command argument) */
	if (extension) {
		/* paramlist */
		list = imap_append_hash_as_string(list, 
				g_mime_object_get_header(object, "Content-Type"));

		/* disposition */
		list = imap_append_disposition_as_string(list, object);
		/* language */
		list = imap_append_header_as_string(list,object,"Content-Language");
		/* location */
		list = imap_append_header_as_string(list,object,"Content-Location");
		s = g_list_join(list," ");
		
		alist = g_list_append(alist,s->str);

		g_list_destroy(list);
		g_string_free(s,FALSE);
	}

	/* done*/
	*(GList **)data = (gpointer)g_list_append(*(GList **)data,dbmail_imap_plist_as_string(alist));
	
	g_list_destroy(alist);

}

void _structure_part_message_rfc822(GMimeObject *part, gpointer data, gboolean extension)
{
	char *result, *b;
	GList *list = NULL;
	size_t s, l=0;
	GMimeObject *object;
	const GMimeContentType *type;
	GMimeMessage *tmpmes;
	
	if (GMIME_IS_MESSAGE(part))
		object = g_mime_message_get_mime_part(GMIME_MESSAGE(part));
	else
		object = part;
	
	type = g_mime_object_get_content_type(object);
	if (! type){
		return;
	}
	/* type/subtype */
	list = g_list_append_printf(list,"\"%s\"", type->type);
	list = g_list_append_printf(list,"\"%s\"", type->subtype);
	/* paramlist */
	list = imap_append_hash_as_string(list, 
			g_mime_object_get_header(object, "Content-Type"));
	/* body id */
	if ((result = (char *)g_mime_object_get_content_id(object)))
		list = g_list_append_printf(list,"\"%s\"", result);
	else
		list = g_list_append_printf(list,"NIL");
	/* body description */
	list = imap_append_header_as_string(list,object,"Content-Description");
	/* body encoding */
	list = imap_append_header_as_string_default(list,object,"Content-Transfer-Encoding", "\"7BIT\"");
	/* body size */
	imap_part_get_sizes(object,&s,&l);
	
	list = g_list_append_printf(list,"%d", s);

	/* envelope structure */
	b = imap_get_envelope(tmpmes = g_mime_message_part_get_message(GMIME_MESSAGE_PART(part)));
	list = g_list_append_printf(list,"%s", b?b:"NIL");
	g_free(b);

	/* body structure */
	b = imap_get_structure(tmpmes = g_mime_message_part_get_message(GMIME_MESSAGE_PART(part)), extension);
	list = g_list_append_printf(list,"%s", b?b:"NIL");
	g_free(b);

	/* lines */
	list = g_list_append_printf(list,"%d", l);
	
	/* done*/
	*(GList **)data = (gpointer)g_list_append(*(GList **)data,dbmail_imap_plist_as_string(list));
	
	g_list_destroy(list);

}

void _structure_part_text(GMimeObject *part, gpointer data, gboolean extension)
{
	char *result;
	GList *list = NULL;
	size_t s, l=0;
	GMimeObject *object;
	GMimeContentType *type;
	
	if (GMIME_IS_MESSAGE(part))
		object = g_mime_message_get_mime_part(GMIME_MESSAGE(part));
	else
		object = part;
	
	type = g_mime_object_get_content_type(object);
	if (! type){
		return;
	}
	/* type/subtype */
	list = g_list_append_printf(list,"\"%s\"", type->type);
	list = g_list_append_printf(list,"\"%s\"", type->subtype);
	/* paramlist */
	list = imap_append_hash_as_string(list, 
			g_mime_object_get_header(object, "Content-Type"));
	/* body id */
	if ((result = (char *)g_mime_object_get_content_id(object)))
		list = g_list_append_printf(list,"\"%s\"", result);
	else
		list = g_list_append_printf(list,"NIL");
	/* body description */
	list = imap_append_header_as_string(list,object,"Content-Description");
	/* body encoding */
	list = imap_append_header_as_string_default(list,object,"Content-Transfer-Encoding", "\"7BIT\"");
	/* body size */
	imap_part_get_sizes(part,&s,&l);
	
	list = g_list_append_printf(list,"%d", s);
	
	/* body lines */
	if (g_mime_content_type_is_type(type,"text","*"))
		list = g_list_append_printf(list,"%d", l);
	
	/* extension data in case of BODYSTRUCTURE */
	if (extension) {
		/* body md5 */
		list = imap_append_header_as_string(list,object,"Content-MD5");
		/* body disposition */
		list = imap_append_disposition_as_string(list,object);
		/* body language */
		list = imap_append_header_as_string(list,object,"Content-Language");
		/* body location */
		list = imap_append_header_as_string(list,object,"Content-Location");
	}
	
	/* done*/
	*(GList **)data = (gpointer)g_list_append(*(GList **)data, dbmail_imap_plist_as_string(list));
	
	g_list_destroy(list);

}



GList* dbmail_imap_append_alist_as_plist(GList *list, InternetAddressList *ialist)
{
	GList *t = NULL, *p = NULL;
	InternetAddress *ia = NULL;
	gchar *s = NULL, *st = NULL;
	gchar **tokens;
	gchar *mailbox;
	int i,j=0;

	if (ialist==NULL)
		return g_list_append_printf(list, "NIL");

	i = internet_address_list_length(ialist);
	for (j=0; j<i; j++) {
		ia = internet_address_list_get_address(ialist,j);
		
		g_return_val_if_fail(ia!=NULL, list);

		if (internet_address_group_get_members((InternetAddressGroup *)ia)) {
			TRACE(TRACE_DEBUG, "recursing into address group [%s].", internet_address_get_name(ia));
			
			/* Address list beginning. */
			p = g_list_append_printf(p, "(NIL NIL \"%s\" NIL)", internet_address_get_name(ia));

			/* Dive into the address list.
			 * Careful, this builds up the stack; it's not a tail call.
			 */
			t = dbmail_imap_append_alist_as_plist(t, internet_address_group_get_members((InternetAddressGroup *)ia));

			s = dbmail_imap_plist_as_string(t);
			/* Only use the results if they're interesting --
			 * (NIL) is the special case of nothing inside the group.
			 */
			if (strcmp(s, "(NIL)") != 0) {
				/* Lop off the extra parens at each end.
				 * Really do the pointer math carefully.
				 */
				size_t slen = strlen(s);
				if (slen) slen--;
				s[slen] = '\0';
				p = g_list_append_printf(p, "%s", (slen ? s+1 : s));
			}
			g_free(s);
			
			g_list_destroy(t);
			t = NULL;

			/* Address list ending. */
			p = g_list_append_printf(p, "(NIL NIL NIL NIL)");

		}

		if (internet_address_mailbox_get_addr((InternetAddressMailbox *)ia)) {
			const char *name = internet_address_get_name(ia);
			const char *addr = internet_address_mailbox_get_addr((InternetAddressMailbox *)ia);
			TRACE(TRACE_DEBUG, "handling a standard address [%s] [%s].", name, addr);

			/* personal name */
			if (name) {
				char * encname = g_mime_utils_header_encode_phrase(name);
				g_strdelimit(encname,"\"\\",' ');
				g_strstrip(encname);
				s = dbmail_imap_astring_as_string(encname);
				t = g_list_append_printf(t, "%s", s);
				g_free(encname);
				g_free(s);
			} else {
				t = g_list_append_printf(t, "NIL");
			}
                        
			/* source route */
			t = g_list_append_printf(t, "NIL");
                        
			/* mailbox name and host name */
			if ((mailbox = addr ? (char *)addr : NULL) != NULL) {
				/* defensive mode for 'To: "foo@bar.org"' addresses */
				g_strstrip(g_strdelimit(mailbox,"\"",' '));
				
				tokens = g_strsplit(mailbox,"@",2);
                        
				/* mailbox name */
				if (tokens[0])
					t = g_list_append_printf(t, "\"%s\"", tokens[0]);
				else
					t = g_list_append_printf(t, "NIL");
				/* host name */
				/* Note that if tokens[0] was null, we must
				 * short-circuit because tokens[1] is out of bounds! */
				if (tokens[0] && tokens[1])
					t = g_list_append_printf(t, "\"%s\"", tokens[1]);
				else
					t = g_list_append_printf(t, "NIL");
				
				g_strfreev(tokens);
			} else {
				t = g_list_append_printf(t, "NIL NIL");
			}
			
			s = dbmail_imap_plist_as_string(t);
			p = g_list_append_printf(p, "%s", s);
			g_free(s);
			
			g_list_destroy(t);
			t = NULL;
		}
	
		/* Bottom of the while loop.
		 * Advance the address list.
		 */
	}
	
	/* Tack it onto the outer list. */
	if (p) {
		s = dbmail_imap_plist_as_string(p);
		st = dbmail_imap_plist_collapse(s);
		list = g_list_append_printf(list, "(%s)", st);
		g_free(s);
		g_free(st);
        
		g_list_destroy(p);
	} else {
		list = g_list_append_printf(list, "NIL");
	}

	return list;
}

/* structure access point */
char * imap_get_structure(GMimeMessage *message, gboolean extension) 
{
	GList *structure = NULL;
	GMimeContentType *type;
	GMimeObject *part;
	char *s, *t;
	
	assert(GMIME_IS_MESSAGE(message));

	part = g_mime_message_get_mime_part(message);
	type = (GMimeContentType *)g_mime_object_get_content_type(part);
	if (! type) {
		TRACE(TRACE_DEBUG,"error getting content_type");
		return NULL;
	}
	
	s = g_mime_content_type_to_string(type);
	TRACE(TRACE_DEBUG,"message type: [%s]", s);
	g_free(s);
	
	/* multipart composite */
	if (g_mime_content_type_is_type(type,"multipart","*"))
		_structure_part_multipart(part,(gpointer)&structure, extension);
	/* message included as mimepart */
	else if (g_mime_content_type_is_type(type,"message","rfc822"))
		_structure_part_message_rfc822(part,(gpointer)&structure, extension);
	/* as simple message */
	else
		_structure_part_text(part,(gpointer)&structure, extension);
	
	s = dbmail_imap_plist_as_string(structure);
	t = dbmail_imap_plist_collapse(s);
	g_free(s);

	g_list_destroy(structure);
	
	return t;
}

static GList * envelope_address_part(GList *list, GMimeMessage *message, const char *header)
{
	const char *result;
	char *t;
	InternetAddressList *alist;
	char *result_enc;
	const char *charset;
	
	charset = message_get_charset(message);

	result = g_mime_object_get_header(GMIME_OBJECT(message),header);
	
	if (result) {
		result_enc = dbmail_iconv_str_to_utf8(result, charset);
		t = imap_cleanup_address(result_enc);
	       	g_free(result_enc);
		alist = internet_address_list_parse_string(t);
		g_free(t);
		list = dbmail_imap_append_alist_as_plist(list, (InternetAddressList *)alist);
		g_object_unref(alist);
		alist = NULL;
	} else {
		list = g_list_append_printf(list,"NIL");
	}

	return list;
}


static void  get_msg_charset_frompart(GMimeObject UNUSED *parent, GMimeObject *part, gpointer data)
{
	const char *charset=NULL;
	if (*((char **)data)==NULL && (charset=g_mime_object_get_content_type_parameter(part,"charset"))) {
	        *((char **)data)=(char *)charset;
	}
	return;
}

const char * message_get_charset(GMimeMessage *message)
{
	GMimeObject *mime_part=NULL;
	const char *mess_charset=NULL;

	if (message)
		mime_part=g_mime_message_get_mime_part(message);
	
	if (mime_part) {
		const char * charset = NULL;
		if ((charset=g_mime_object_get_content_type_parameter(mime_part,"charset")))
			mess_charset = charset;
	}

	if (mess_charset == NULL)
		g_mime_message_foreach(message,get_msg_charset_frompart,&mess_charset);

	return mess_charset;
}

/* envelope access point */
char * imap_get_envelope(GMimeMessage *message)
{
	GMimeObject *part;
	GList *list = NULL;
	char *result;
	char *s = NULL, *t = NULL;
	const char *h;

	if (! GMIME_IS_MESSAGE(message)) {
		TRACE(TRACE_ERR, "argument is not a message");
		return NULL;
	}
	
	part = GMIME_OBJECT(message);
	/* date */
	result = g_mime_message_get_date_as_string(message);
	if (result) {
		t = dbmail_imap_astring_as_string(result);
		list = g_list_append_printf(list,"%s", t);
		g_free(result);
		g_free(t);
		result = NULL;
	} else {
		list = g_list_append_printf(list,"NIL");
	}
	
	/* subject */
	result = (char *)g_mime_object_get_header(GMIME_OBJECT(message),"Subject");

	if (result) {
		const char *charset = message_get_charset(message);
		char * subj = dbmail_iconv_str_to_utf8(result, charset);
		TRACE(TRACE_DEBUG, "[%s] [%s] -> [%s]", charset, result, subj);
		if (g_mime_utils_text_is_8bit((unsigned char *)subj, strlen(subj))) {
			s = g_mime_utils_header_encode_text((const char *)subj);
			TRACE(TRACE_DEBUG, "[%s] -> [%s]", subj, s);
			g_free(subj);
			subj = s;
		}
		t = dbmail_imap_astring_as_string(subj);
		TRACE(TRACE_DEBUG, "[%s] -> [%s]", subj, t);
		g_free(subj);
		list = g_list_append_printf(list,"%s", t);
		g_free(t);
	} else {
		list = g_list_append_printf(list,"NIL");
	}
	
	/* from */
	list = envelope_address_part(list, message, "From");
	/* sender */
	h = g_mime_object_get_header(GMIME_OBJECT(message),"Sender");
	if (h && (strlen(h) > 0))
		list = envelope_address_part(list, message, "Sender");
	else
		list = envelope_address_part(list, message, "From");

	/* reply-to */
	h = g_mime_object_get_header(GMIME_OBJECT(message),"Reply-to");
	if (h && (strlen(h) > 0))
		list = envelope_address_part(list, message, "Reply-to");
	else
		list = envelope_address_part(list, message, "From");
		
	/* to */
	list = envelope_address_part(list, message, "To");
	/* cc */
	list = envelope_address_part(list, message, "Cc");
	/* bcc */
	list = envelope_address_part(list, message, "Bcc");
	
	/* in-reply-to */
	list = imap_append_header_as_string(list,part,"In-Reply-to");
	/* message-id */
	result = (char *)g_mime_message_get_message_id(message);
	if (result && (! g_strrstr(result,"=")) && (! g_strrstr(result,"@(none)"))) {
                t = g_strdup_printf("<%s>", result);
		s = dbmail_imap_astring_as_string(t);
		list = g_list_append_printf(list,"%s", s);
		g_free(s);
                g_free(t);
	} else {
		list = g_list_append_printf(list,"NIL");
	}

	s = dbmail_imap_plist_as_string(list);

	g_list_destroy(list);
	
	return s;
}


char * imap_get_logical_part(const GMimeObject *object, const char * specifier) 
{
	gchar *s = NULL, *t=NULL;
		
	if (specifier == NULL) {
		t = g_mime_object_to_string(GMIME_OBJECT(object));
		s = get_crlf_encoded(t);
		g_free(t);
	}

	else if (strcasecmp(specifier,"HEADER")==0 || strcasecmp(specifier,"MIME")==0) {
		t = g_mime_object_get_headers(GMIME_OBJECT(object));
		s = get_crlf_encoded(t);
		g_free(t);
		s = g_realloc(s, strlen(s) + 3);
		strcat(s, "\r\n");
	} 
	
	else if (strcasecmp(specifier,"TEXT")==0) {
		t = g_mime_object_get_body(GMIME_OBJECT(object));
		s = get_crlf_encoded(t);
		g_free(t);
	} 

	return s;
}

	

GMimeObject * imap_get_partspec(const GMimeObject *message, const char *partspec) 
{
	GMimeObject *object;
	GMimeContentType *type;
	char *part;
	guint index;
	guint i;

	assert(message);
	assert(partspec);
	
	GString *t = g_string_new(partspec);
	GList *specs = g_string_split(t,".");
	g_string_free(t,TRUE);
	
	object = GMIME_OBJECT(message);
	if (!object) {
		TRACE(TRACE_INFO, "message is not an object");
		return NULL;
	}
		
	for (i=0; i< g_list_length(specs); i++) {
		part = g_list_nth_data(specs,i);
		if (! (index = strtol((const char *)part, NULL, 0))) 
			break;
		
		if (GMIME_IS_MESSAGE(object))
			object = GMIME_OBJECT(GMIME_MESSAGE(object)->mime_part);
		
		type = (GMimeContentType *)g_mime_object_get_content_type(object);

		if (g_mime_content_type_is_type(type,"multipart","*")) {
			object = g_mime_multipart_get_part((GMimeMultipart *)object, (int)index-1);
			if (!object) {
				TRACE(TRACE_INFO, "object part [%d] is null", (int)index-1);
				g_list_destroy(specs);
				return NULL;
			}
			if (! GMIME_IS_OBJECT(object)) {
				TRACE(TRACE_INFO, "object part [%d] is not an object", (int)index-1);
				g_list_destroy(specs);
				return NULL;
			}

			type = (GMimeContentType *)g_mime_object_get_content_type(object);
		}

		// for message/rfc822 parts we want the contained message, 
		// not the mime-part as such

		if (g_mime_content_type_is_type(type,"message","rfc822")) {
			object = GMIME_OBJECT(GMIME_MESSAGE_PART(object)->message);
			if (!object) {
				TRACE(TRACE_INFO, "rfc822 part is null");
				g_list_destroy(specs);
				return NULL;
			}
			if (! GMIME_IS_OBJECT(object)) {
				TRACE(TRACE_INFO, "rfc822 part is not an object");
				g_list_destroy(specs);
				return NULL;
			}
		}
	}

	g_list_destroy(specs);

	return object;
}

/* Ugly hacks because sometimes GMime is too strict. */
char * imap_cleanup_address(const char *a) 
{
	char *r, *t;
	char *inptr;
	char prev, next=0;
	unsigned incode=0, inquote=0;
	size_t i, l;
	GString *s;

	if (!a || !a[0])
		return g_strdup("");
	
	s = g_string_new("");
	t = g_strdup(a);

	// un-fold and collapse tabs and spaces
	g_strdelimit(t,"\n",' ');
	dm_pack_spaces(t);
	inptr = t;
	inptr = g_strstrip(inptr);
	prev = inptr[0];
	
	l = strlen(inptr);

	TRACE(TRACE_DEBUG, "[%s]", inptr);
	for (i = 0; i < l - 1; i++) {

		next = inptr[i+1];

		if (incode && (inptr[i] == '"' || inptr[i] == ' '))
			continue; // skip illegal chars inquote

		if ((! inquote) && inptr[i]=='"')
			inquote = 1;
		else if (inquote && inptr[i] == '"')
			inquote = 0;

		// quote encoded string
		if (inptr[i] == '=' && next == '?' && (! incode)) {
			incode=1;
			if (prev != '"' && (! inquote)) {
				g_string_append_c(s,'"');
				inquote = 1;
			}
		} 

		g_string_append_c(s,inptr[i]); 

		if (inquote && incode && prev == '?' && inptr[i] == '=' && (next == '"' || next == ' ' || next == '<')) {
			if ((next != '"' ) && ((i < l-2) && (inptr[i+2] != '='))) {
				g_string_append_c(s, '"');
				inquote = 0;
			}
			if (next == '<')
				g_string_append_c(s,' ');
			incode=0;
		}

		prev = inptr[i];
	}

	inptr+=i;

	if (*inptr)
		g_string_append(s,inptr);

	if (incode && inquote)
		g_string_append_c(s,'"');

	g_free(t);
	
	if (g_str_has_suffix(s->str,";"))
		s = g_string_truncate(s,s->len-1);

	/* This second hack changes semicolons into commas when not preceded by a colon.
	 * The purpose is to fix broken syntax like this: "one@dom; two@dom"
	 * But to allow correct syntax like this: "Group: one@dom, two@dom;"
	 */
	int colon = 0;

	for (i = 0; i < s->len; i++) {
		switch (s->str[i]) {
		case ':':
			colon = 1;
			break;
		case ';':
			s->str[i] = ',';
			break;
		}
		if (colon)
			break;
	}

	r = s->str;
	g_string_free(s,FALSE);
	TRACE(TRACE_DEBUG,"[%s]", r);
	return r;
}

uint64_t dm_strtoull(const char *nptr, char **endptr, int base)
{
	errno = 0;
	long long int r = strtoll(nptr, endptr, base);
	if (errno)
		return (long long unsigned)0;

	if (r < 0) {
		errno = EINVAL;
		return (long long unsigned)0;
	}
	return (long long unsigned)r;
}

/* A frontend to the base64_decode_internal() that deals with embedded strings. */
char **base64_decodev(char *str)
{
	size_t i, j, n;
	int numstrings = 0;
	size_t decodelen = 0;
	char *decoded;
	char **ret = NULL;

	/* Base64 always decodes to a shorter string. */
	decoded = (char *)g_base64_decode((const gchar *)str, &decodelen);

	/* Count up the number of embedded strings... */
	for (i = 0; i <= decodelen; i++) {
		if (decoded[i] == '\0') {
			numstrings++;
		}
	}

	/* Allocate an array large enough
	 * for the strings and a final NULL. */
	ret = g_new0(char *, (numstrings + 1));

	/* Copy each nul terminated string to the array. */
	for (i = j = n = 0; i <= decodelen; i++) {
		if (decoded[i] == '\0') {
			ret[n] = g_strdup(decoded + j);
			j = i + 1;
			n++;
		}
	}

	/* Put the final NULL on the end of the array. */
	ret[n] = NULL;

	g_free(decoded);

	return ret;
}

int dm_get_hash_for_string(const char *buf, char *digest)
{
	Field_T hash_algorithm;
	static hashid type;
	static int initialized=0;
	int result=0;

	if (! initialized) {
		if (config_get_value("hash_algorithm", "DBMAIL", hash_algorithm) < 0)
			g_strlcpy(hash_algorithm, "sha1", FIELDSIZE);

		if (MATCH(hash_algorithm,"md5"))
			type=MHASH_MD5;
		else if (MATCH(hash_algorithm,"sha1"))
			type=MHASH_SHA1;
		else if (MATCH(hash_algorithm,"sha256"))
			type=MHASH_SHA256;
		else if (MATCH(hash_algorithm,"sha512"))
			type=MHASH_SHA512;
		else if (MATCH(hash_algorithm,"whirlpool"))
			type=MHASH_WHIRLPOOL;
		else if (MATCH(hash_algorithm,"tiger"))
			type=MHASH_TIGER;
		else {
			TRACE(TRACE_INFO,"hash algorithm not supported. Using SHA1.");
			type=MHASH_SHA1;
		}
		initialized=1;
	}

	switch(type) {
		case MHASH_MD5:
			result=dm_md5(buf,digest);
		break;
		case MHASH_SHA1:
			result=dm_sha1(buf,digest);		
		break;
		case MHASH_SHA256:
			result=dm_sha256(buf,digest);		
		break;
		case MHASH_SHA512:
			result=dm_sha512(buf,digest);		
		break;
		case MHASH_WHIRLPOOL:
			result=dm_whirlpool(buf,digest);		
		break;
		case MHASH_TIGER:
			result=dm_tiger(buf,digest);
		break;
		default:
			result=1;
			TRACE(TRACE_EMERG,"unhandled hash algorithm");
		break;
	}

	return result;
}

gchar *get_crlf_encoded_opt(const char *in, int dots)
{
	char prev = 0, curr = 0, *t, *out;
	const char *p = in;
	int i=0, nl = 0;
	assert(in);

	while (p[i]) {
		curr = p[i];
		if ISLF(curr) nl++;
		prev = curr;
		i++;
	}

	out = g_new0(char,i+(2*nl)+1);
	t = out;
	p = in;
	i = 0;
	while (p[i]) {
		curr = p[i];
		if (ISLF(curr) && (! ISCR(prev)))
			*t++ = '\r';
		if (dots && ISDOT(curr) && ISLF(prev))
			*t++ = '.';
		*t++=curr;
		prev = curr;
		i++;
	}
	return out;
}


void strip_crlf(char *buffer)
{
	if (! (buffer && buffer[0])) return;
	size_t l = strlen(buffer);
	while (--l > 0) {
		if (buffer[l] == '\r' || buffer[l] == '\n')
			buffer[l] = '\0';
		else
			break;
	}
}

// work around glib allocation bug
char * dm_base64_decode(const gchar *s, uint64_t *len)
{
	char *r = NULL, *p = (char *)g_base64_decode((const gchar *)s, (gsize *)len);
	r = g_strndup(p, (gsize)*len);
	g_free(p);
	TRACE(TRACE_DEBUG,"[%" PRIu64 ":%s]->[%s]", *len, s, r);
	return r;
}


uint64_t stridx(const char *s, char c)
{
	uint64_t i;
	for (i = 0; s[i] && s[i] != c; i++);
	return i;
}


