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

/* $Id$
 * 
 * imaputil.c
 *
 * IMAP-server utility functions implementations
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include "dbmail.h"
#include "imaputil.h"
#include "imap4.h"
#include "debug.h"
#include "db.h"
#include "memblock.h"
#include "dbsearch.h"
#include "rfcmsg.h"
#include "misc.h"

#ifndef MAX_LINESIZE
#define MAX_LINESIZE (10*1024)
#endif

#define BUFLEN 2048
#define SEND_BUF_SIZE 1024
#define MAX_ARGS 512

/* cache */
extern cache_t cached_msg;

/* consts */
const char AcceptedChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "!@#$%^&*()-=_+`~[]{}\\|'\" ;:,.<>/? \n\r";

const char AcceptedTagChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "!@#$%^&-=_`~\\|'\" ;:,.<>/? ";

const char AcceptedMailboxnameChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "-=/ _.&,+@()[]";

extern const char *month_desc[];



char base64encodestring[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* returned by date_sql2imap() */
#define IMAP_STANDARD_DATE "Sat, 03-Nov-1979 00:00:00 +0000"
char _imapdate[IMAP_INTERNALDATE_LEN] = IMAP_STANDARD_DATE;

/* returned by date_imap2sql() */
#define SQL_STANDARD_DATE "1979-11-03 00:00:00"
char _sqldate[SQL_INTERNALDATE_LEN + 1] = SQL_STANDARD_DATE;


const int month_len[] = {
	31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

const char *item_desc[] = {
	"TEXT", "HEADER", "MIME", "HEADER.FIELDS", "HEADER.FIELDS.NOT"
};

const char *envelope_items[] = {
	"from", "sender", "reply-to", "to", "cc", "bcc", NULL
};

static const char *search_cost[] = { "b","b","c","c","c","d","d","d","d","c","e","e","b","b","j","j","j" };


/* some basic imap type utils */

/*
 *  build a parentisized list (4.4) from a GList
 */
char *dbmail_imap_plist_as_string(GList * list)
{
	char *p;
	GString * tmp1 = g_string_new("");
	GString * tmp2 = g_list_join(list, " ");
	g_string_printf(tmp1,"(%s)", tmp2->str);
	
	p = tmp1->str;
	g_string_free(tmp1,FALSE);
	g_string_free(tmp2,TRUE);

	// collapse "(NIL) (NIL)" to "(NIL)(NIL)"
	/* disabled: OE doesn't like this, uw-imapd doesn't do this...
	char **sublists;
	sublists = g_strsplit(p,") (",0);
	g_free(p);
	p = g_strjoinv(")(",sublists);
	*/
	
	return p;
}
/* 
 * return a quoted or literal astring
 */

char *dbmail_imap_astring_as_string(const char *s)
{
	int i;
	char *r;
	char *t, *l = NULL;
	char first, last, penult = '\\';

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
			r = g_strdup_printf("{%lu}\r\n%s", (unsigned long) strlen(l), l);
			g_free(l);
			return r;
		}
		
	}
	r = g_strdup_printf("\"%s\"", l);
	g_free(t);

	return r;

}



/* 
 * sort_search()
 * 
 * reorder searchlist by using search cost as the sort key
 * 
 */ 

int sort_search(struct dm_list *searchlist) 
{
	struct element *el;
	search_key_t *left = NULL, *right = NULL, *tmp=NULL;
	
	if (!searchlist) 
		return 0;

	el = dm_list_getstart(searchlist);
	while (el != NULL) {
		if (! el->nextnode)
			break;
		left = el->data;
		right = (el->nextnode)->data;

		/* recurse into sub_search if necessary */
		switch(left->type) {
			case IST_SUBSEARCH_AND:
			case IST_SUBSEARCH_NOT:
			case IST_SUBSEARCH_OR:
				sort_search(&left->sub_search);
				break;
				;;
		}
		/* switch elements to sorted order */
		if (strcmp((char *)search_cost[left->type],(char *)search_cost[right->type]) > 0) {
			tmp = el->data;
			el->data = el->nextnode->data;
			el->nextnode->data = tmp;
			/* when in doubt, use brute force: starting over */
			el = dm_list_getstart(searchlist);
			continue;
		}
		
		el = el->nextnode;
	}
	return 0;
}


/* 
 * get_part_by_num()
 *
 * retrieves a msg part by it's numeric specifier
 * 'part' is assumed to be valid! (i.e '1.2.3.44')
 * returns NULL if there is no such part 
 */
mime_message_t *get_part_by_num(mime_message_t * msg, const char *part)
{
	int nextpart, j;
	char *endptr;
	struct element *curr;

	if (part == NULL || strlen(part) == 0 || msg == NULL)
		return msg;
	trace(TRACE_DEBUG,"%s,%s: partspec [%s]", __FILE__, __func__, part);

	nextpart = strtoul(part, &endptr, 10);	/* strtoul() stops at '.' */

	for (j = 1, curr = dm_list_getstart(&msg->children);
	     j < nextpart && curr; j++, curr = curr->nextnode);

	if (!curr)
		return NULL;

	if (*endptr)
		return get_part_by_num((mime_message_t *) curr->data, &endptr[1]);	/* skip dot in part */

	return (mime_message_t *) curr->data;
}


/*
 * rfcheader_dump()
 * 
 * dumps rfc-header fields belonging to rfcheader
 * the fields to be dumped are specified in fieldnames, an array containing nfields items
 *
 * if equal_type == 0 the field match criterium is inverted and non-matching fieldnames
 * will be selected
 *
 * to select every headerfield it suffices to set nfields and equal_type to 0
 *
 * returns number of bytes written to outmem
 */
u64_t rfcheader_dump(MEM * outmem, struct dm_list * rfcheader,
		     char **fieldnames, int nfields, int equal_type)
{
	struct mime_record *mr;
	struct element *curr;
	u64_t size = 0;

	curr = dm_list_getstart(rfcheader);
	if (rfcheader == NULL || curr == NULL) {
		/*size += fprintf(outstream, "NIL\r\n"); */
		return 0;
	}

	curr = dm_list_getstart(rfcheader);
	while (curr) {
		mr = (struct mime_record *) curr->data;

		if (haystack_find(nfields, fieldnames, mr->field) == equal_type) {
			/* ok output this field */
			size += mwrite(mr->field, strlen(mr->field), outmem);
			size += mwrite(": ", 2, outmem);
			size += mwrite(mr->value, strlen(mr->value), outmem);
			size += mwrite("\r\n", 2, outmem);
		}

		curr = curr->nextnode;
	}
	size += mwrite("\r\n", 2, outmem);

	return size;
}


/*
 * mimeheader_dump()
 * 
 * dumps mime-header fields belonging to mimeheader
 *
 */
u64_t mimeheader_dump(MEM * outmem, struct dm_list * mimeheader)
{
	struct mime_record *mr;
	struct element *curr;
	u64_t size = 0;

	curr = dm_list_getstart(mimeheader);
	if (mimeheader == NULL || curr == NULL) {
		/*size = fprintf(outstream, "NIL\r\n"); */
		return 0;
	}

	while (curr) {
		mr = (struct mime_record *) curr->data;
		size += mwrite(mr->field, strlen(mr->field), outmem);
		size += mwrite(": ", 2, outmem);
		size += mwrite(mr->value, strlen(mr->value), outmem);
		size += mwrite("\r\n", 2, outmem);
		curr = curr->nextnode;
	}
	size += mwrite("\r\n", 2, outmem);

	return size;
}


/* 
 * find a string in an array of strings
 */
int haystack_find(int haystacklen, char **haystack, const char *needle)
{
	int i;

	for (i = 0; i < haystacklen; i++)
		if (strcasecmp(haystack[i], needle) == 0)
			return 1;

	return 0;
}


/* local defines */
#define NORMPAR 1
#define SQUAREPAR 2
#define NOPAR 0

char *the_args[MAX_ARGS];

/*
 * build_args_array()
 *
 * !! REMOVED  because unused !!
 *
 * builds an dimensional array of strings containing arguments based upon 
 * a series of arguments passed as a single string.
 * normal/square parentheses have special meaning:
 * '(body [all header])' will result in the following array:
 * [0] = '('
 * [1] = 'body'
 * [2] = '['
 * [3] = 'all'
 * [4] = 'header'
 * [5] = ']'
 * [6] = ')'
 *
 * quoted strings are those enclosed by double quotation marks and returned as a single argument
 * WITHOUT the enclosing quotation marks
 *
 * parentheses loose their special meaning if inside (double)quotation marks;
 * data should be 'clarified' (see clarify_data() function below)
 *
 * The returned array will be NULL-terminated.
 * Will return NULL upon errors.
 */

/*
 * as build_args_array(), but reads strings on cmd line specified by {##}\0
 * (\r\n had been removed from string)
 */
char **build_args_array_ext(const char *originalString, clientinfo_t * ci)
{
	int nargs = 0, inquote = 0, quotestart = 0;
	int nnorm = 0, nsquare = 0, paridx = 0, argstart = 0;
	unsigned int i;
	char parlist[MAX_LINESIZE];
	char s[MAX_LINESIZE];
	char *tmp, *lastchar;
	int quotedSize, cnt, dataidx;

	/* this is done for the possible extra lines to be read from the client:
	 * the line is read into currline; s will always point to the line currently
	 * being processed
	 */
	strncpy(s, originalString, MAX_LINESIZE);

	if (!s)
		return NULL;

	/* check for empty string */
	if (!(*s)) {
		the_args[0] = NULL;
		return the_args;
	}

	/* find the arguments */
	paridx = 0;
	parlist[paridx] = NOPAR;

	inquote = 0;

	for (i = 0, nargs = 0; s[i] && nargs < MAX_ARGS - 1; i++) {
		/* check quotes */
		if (s[i] == '"' && ((i > 0 && s[i - 1] != '\\') || i == 0)) {
			if (inquote) {
				/* quotation end, treat quoted string as argument */
				if (! (the_args[nargs] = (char *)dm_malloc(sizeof(char) * (i - quotestart)))) {
					/* out of mem */
					while (--nargs >= 0) {
						dm_free(the_args[nargs]);
						the_args[nargs] = NULL;
					}
					trace(TRACE_ERROR, "%s,%s: out-of-memory error.", __FILE__, __func__);
					return NULL;
				}

				memcpy((void *) the_args[nargs], (void *) &s[quotestart + 1], i - quotestart - 1);
				the_args[nargs][i - quotestart - 1] = '\0';

				nargs++;
				inquote = 0;
			} else {
				inquote = 1;
				quotestart = i;
			}

			continue;
		}

		if (inquote)
			continue;

		/* check for (, ), [ or ] in string */
		if (s[i] == '(' || s[i] == ')' || s[i] == '[' || s[i] == ']') {
			/* check parenthese structure */
			if (s[i] == ')') {
				if (paridx < 0 || parlist[paridx] != NORMPAR)
					paridx = -1;
				else {
					nnorm--;
					paridx--;
				}
			} else if (s[i] == ']') {
				if (paridx < 0 || parlist[paridx] != SQUAREPAR)
					paridx = -1;
				else {
					paridx--;
					nsquare--;
				}
			} else if (s[i] == '(') {
				parlist[++paridx] = NORMPAR;
				nnorm++;
			} else {	/* s[i] == '[' */

				parlist[++paridx] = SQUAREPAR;
				nsquare++;
			}

			if (paridx < 0) {
				/* error in parenthesis structure */
				while (--nargs >= 0) {
					dm_free(the_args[nargs]);
					the_args[nargs] = NULL;
				}
				return NULL;
			}

			/* add this parenthesis to the arg list and continue */
			if (!  (the_args[nargs] = (char *) dm_malloc(sizeof(" ")))) {
				/* out of mem */
				while (--nargs >= 0) {
					dm_free(the_args[nargs]);
					the_args[nargs] = NULL;
				}
				trace(TRACE_ERROR, "%s,%s: out-of-memory error.", __FILE__, __func__);
				return NULL;
			}
			the_args[nargs][0] = s[i];
			the_args[nargs][1] = '\0';

			nargs++;
			continue;
		}

		if (s[i] == ' ')
			continue;

		/* check for {number}\0 */
		if (s[i] == '{') {
			quotedSize = strtoul(&s[i + 1], &lastchar, 10);

			/* only continue if the number is followed by '}\0' */
			trace(TRACE_DEBUG, "%s,%s: last char = %c", __FILE__, __func__, *lastchar);
			if ((*lastchar == '+' && *(lastchar + 1) == '}' && 
			     *(lastchar + 2) == '\0') || 
			    (*lastchar == '}' && *(lastchar + 1) == '\0')) {
				/* allocate space for this argument (could be a message when used with APPEND) */
				if (! (the_args[nargs] = (char *) dm_malloc(sizeof(char) * (quotedSize + 1)))) {
					/* out of mem */
					while (--nargs >= 0) {
						dm_free(the_args[nargs]);
						the_args[nargs] = NULL;
					}
					trace(TRACE_ERROR, "%s,%s: out-of-memory allocating [%u] bytes for extra string",
							__FILE__, __func__, quotedSize + 1);
					return NULL;
				}

				ci_write(ci->tx, "+ OK gimme that string\r\n");
				
				alarm(ci->timeout);	/* dont wait forever */
				for (cnt = 0, dataidx = 0; cnt < quotedSize; cnt++) {
					the_args[nargs][dataidx] = fgetc(ci->rx);

					if (the_args[nargs][dataidx] != '\r')
						dataidx++;	/* only store if it is not \r */
				}

				alarm(0);
				the_args[nargs][dataidx] = '\0';	/* terminate string */
				nargs++;

				if (!ci->rx || !ci->tx || ferror(ci->rx) || ferror(ci->tx)) {
					/* timeout occurred or connection has gone away */
					while (--nargs >= 0) {
						dm_free(the_args[nargs]);
						the_args[nargs] = NULL;
					}

					trace(TRACE_ERROR, "%s,%s: timeout occurred", __FILE__, __func__);
					return NULL;
				}

				/* now read the rest of this line */
				alarm(ci->timeout);
				fgets(s, MAX_LINESIZE, ci->rx);
				alarm(0);

				if (!ci->rx || !ci->tx || ferror(ci->rx) || ferror(ci->tx)) {
					/* timeout occurred */
					while (--nargs >= 0) {
						dm_free(the_args[nargs]);
						the_args[nargs] = NULL;
					}

					trace(TRACE_ERROR,
					      "build_args_array_ext(): timeout occurred");
					return NULL;
				}

				/* remove trailing \r\n */
				tmp = &s[strlen(s)];
				tmp--;	/* go before trailing \0; watch this with empty strings! */
				while (tmp >= s
				       && (*tmp == '\r' || *tmp == '\n')) {
					*tmp = '\0';
					tmp--;
				}

				trace(TRACE_DEBUG,
				      "build_args_array_ext(): got extra line [%s]",
				      s);

				/* start over! */
				i = 0;
				continue;
			}
		}

		/* at an argument start now, walk on until next delimiter
		 * and save argument 
		 */

		for (argstart = i; i < strlen(s) && !strchr(" []()", s[i]); i++)
			if (s[i] == '"') {
				if (s[i - 1] == '\\')
					continue;
				else
					break;
			}

		if (!  (the_args[nargs] = (char *) dm_malloc(sizeof(char) * (i - argstart + 1)))) {
			/* out of mem */
			while (--nargs >= 0) {
				dm_free(the_args[nargs]);
				the_args[nargs] = NULL;
			}

			trace(TRACE_ERROR,
			      "IMAPD: Not enough memory while building up argument array.");
			return NULL;
		}

		memcpy((void *) the_args[nargs], (void *) &s[argstart], i - argstart);
		the_args[nargs][i - argstart] = '\0';

		nargs++;
		i--;		/* walked one too far */
	}

	if (paridx != 0) {
		/* error in parenthesis structure */
		while (--nargs >= 0) {
			dm_free(the_args[nargs]);
			the_args[nargs] = NULL;
		}
		return NULL;
	}

	the_args[nargs] = NULL;	/* terminate */

	/* dump args (debug) */
	for (i = 0; the_args[i]; i++) {
		trace(TRACE_DEBUG, "arg[%d]: '%s'\n", i, the_args[i]);
	}

	return the_args;
}

#undef NOPAR
#undef NORMPAR
#undef RIGHTPAR


/*
 * clarify_data()
 *
 * replaces all multiple spaces by a single one except for quoted spaces;
 * removes leading and trailing spaces and a single trailing newline (if present)
 */
void clarify_data(char *str)
{
	int startidx, inquote, endidx;
	unsigned int i;


	/* remove leading spaces */
	for (i = 0; str[i] == ' '; i++);
	memmove(str, &str[i], sizeof(char) * (strlen(&str[i]) + 1));	/* add one for \0 */

	/* remove CR/LF */
	endidx = strlen(str) - 1;
	if (endidx >= 0 && (str[endidx] == '\n' || str[endidx] == '\r'))
		endidx--;

	if (endidx >= 0 && (str[endidx] == '\n' || str[endidx] == '\r'))
		endidx--;


	if (endidx == 0) {
		/* only 1 char left and it is not a space */
		str[1] = '\0';
		return;
	}

	/* remove trailing spaces */
	for (i = endidx; i > 0 && str[i] == ' '; i--);
	if (i == 0) {
		/* empty string remains */
		*str = '\0';
		return;
	}

	str[i + 1] = '\0';

	/* scan for multiple spaces */
	inquote = 0;
	for (i = 0; i < strlen(str); i++) {
		if (str[i] == '"') {
			if ((i > 0 && str[i - 1] != '\\') || i == 0) {
				/* toggle in-quote flag */
				inquote ^= 1;
			}
		}

		if (str[i] == ' ' && !inquote) {
			for (startidx = i; str[i] == ' '; i++);

			if (i - startidx > 1) {
				/* multiple non-quoted spaces found --> remove 'm */
				memmove(&str[startidx + 1], &str[i],
					sizeof(char) * (strlen(&str[i]) +
							1));
				/* update i */
				i = startidx + 1;
			}
		}
	}
}


/*
 * is_textplain()
 *
 * checks if content-type is text/plain
 */
int is_textplain(struct dm_list *hdr)
{
	struct mime_record *mr;
	int i, len;

	if (!hdr)
		return 0;

	mime_findfield("content-type", hdr, &mr);

	if (!mr)
		return 0;

	len = strlen(mr->value);
	for (i = 0; len - i >= (int) sizeof("text/plain"); i++)
		if (strncasecmp
		    (&mr->value[i], "text/plain",
		     sizeof("text/plain") - 1) == 0)
			return 1;

	return 0;
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
	struct tm *tm_imap_date;
	
	time_t ltime;
        char *last;

        last = strptime(sqldate,"%Y-%m-%d %T", &tm_sql_date);
        if ( (last == NULL) || (*last != '\0') ) {
                strcpy(_imapdate, IMAP_STANDARD_DATE);
                return _imapdate;
        }

	/* FIXME: this works fine on linux, but may cause dst offsets in netbsd. */
	ltime = mktime (&tm_sql_date);
	tm_imap_date = localtime(&ltime);

        strftime(_imapdate, sizeof(_imapdate), "%a, %d %b %Y %H:%M:%S %z", tm_imap_date);
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

/*
 *
 */
size_t stridx(const char *s, char ch)
{
	size_t i;

	for (i = 0; s[i] && s[i] != ch; i++);

	return i;
}


/*
 * checkchars()
 *
 * performs a check to see if the read data is valid
 * returns 0 if invalid, 1 otherwise
 */
int checkchars(const char *s)
{
	int i;

	for (i = 0; s[i]; i++) {
		if (!strchr(AcceptedChars, s[i])) {
			/* wrong char found */
			return 0;
		}
	}
	return 1;
}


/*
 * checktag()
 *
 * performs a check to see if the read data is valid
 * returns 0 if invalid, 1 otherwise
 */
int checktag(const char *s)
{
	int i;

	for (i = 0; s[i]; i++) {
		if (!strchr(AcceptedTagChars, s[i])) {
			/* wrong char found */
			return 0;
		}
	}
	return 1;
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
	int i, indigit;

	if (!s || !isdigit(s[0]))
		return 0;

	for (i = 1, indigit = 1; s[i]; i++) {
		if (isdigit(s[i]))
			indigit = 1;
		else if (s[i] == ',') {
			if (!indigit && s[i - 1] != '*')
				return 0;

			indigit = 0;
		} else if (s[i] == ':') {
			if (!indigit)
				return 0;

			indigit = 0;
		} else if (s[i] == '*') {
			if (s[i - 1] != ':')
				return 0;
		}
	}

	return 1;
}


/*
 * base64encode()
 *
 * encodes a string using base64 encoding
 */
void base64encode(char *in, char *out)
{
	for (; strlen(in) >= 3; in += 3) {
		*out++ = base64encodestring[(in[0] & 0xFC) >> 2U];
		*out++ =
		    base64encodestring[((in[0] & 0x03) << 4U) |
				       ((in[1] & 0xF0) >> 4U)];
		*out++ =
		    base64encodestring[((in[1] & 0x0F) << 2U) |
				       ((in[2] & 0xC0) >> 6U)];
		*out++ = base64encodestring[(in[2] & 0x3F)];
	}

	if (strlen(in) == 2) {
		/* 16 bits left to encode */
		*out++ = base64encodestring[(in[0] & 0xFC) >> 2U];
		*out++ =
		    base64encodestring[((in[0] & 0x03) << 4U) |
				       ((in[1] & 0xF0) >> 4U)];
		*out++ = base64encodestring[((in[1] & 0x0F) << 2U)];
		*out++ = '=';

		return;
	}

	if (strlen(in) == 1) {
		/* 8 bits left to encode */
		*out++ = base64encodestring[(in[0] & 0xFC) >> 2U];
		*out++ = base64encodestring[((in[0] & 0x03) << 4U)];
		*out++ = '=';
		*out++ = '=';

		return;
	}
}


/*
 * base64decode()
 *
 * decodes a base64 encoded string
 */
void base64decode(char *in, char *out)
{
	for (; strlen(in) >= 4; in += 4) {
		*out++ = (stridx(base64encodestring, in[0]) << 2U)
		    | ((stridx(base64encodestring, in[1]) & 0x30) >> 4U);

		*out++ = ((stridx(base64encodestring, in[1]) & 0x0F) << 4U)
		    | ((stridx(base64encodestring, in[2]) & 0x3C) >> 2U);

		*out++ = ((stridx(base64encodestring, in[2]) & 0x03) << 6U)
		    | (stridx(base64encodestring, in[3]) & 0x3F);
	}

	*out = 0;
}


/*
 * binary_search()
 *
 * performs a binary search on array to find key
 * array should be ascending in values
 *
 * returns -1 if not found. key_idx will hold key if found
 */
int binary_search(const u64_t * array, unsigned arraysize, u64_t key,
		  unsigned int *key_idx)
{
	unsigned low, high, mid = 1;

	assert(key_idx != NULL);
	*key_idx = 0;
	if (arraysize == 0)
		return -1;

	low = 0;
	high = arraysize - 1;

	while (low <= high) {
		mid = (high + low) / (unsigned) 2;
		if (array[mid] < key)
			low = mid + 1;
		else if (array[mid] > key) {
			if (mid > 0)
				high = mid - 1;
			else
				break;
		} else {
			*key_idx = mid;
			return 1;
		}
	}

	return -1;		/* not found */
}

/* 
 * sends a string to outstream, escaping the following characters:
 * "  --> \"
 * \  --> \\
 *
 * double quotes are placed at the beginning and end of the string.
 *
 * returns the number of bytes outputted.
 */
int quoted_string_out(FILE * outstream, const char *s)
{
	int i, cnt;

	// check wheter we must use literal string
	for (i = 0; s[i]; i++) {
		if (!(s[i] & 0xe0) || (s[i] & 0x80) || (s[i] == '"')
		    || (s[i] == '\\')) {
			cnt = ci_write(outstream, "{");
			cnt += ci_write(outstream, "%lu", (unsigned long) strlen(s));
			cnt += ci_write(outstream, "}\r\n");
			cnt += ci_write(outstream, "%s", s);
			return cnt;
		}
	}

	cnt = ci_write(outstream, "\"");
	cnt += ci_write(outstream, "%s", s);
	cnt += ci_write(outstream, "\"");

	return cnt;
}


/*
 * send_data()
 *
 * sends cnt bytes from a MEM structure to a FILE stream
 * uses a simple buffering system
 */
void send_data(FILE * to, MEM * from, int cnt)
{
	char buf[SEND_BUF_SIZE];

	for (cnt -= SEND_BUF_SIZE; cnt >= 0; cnt -= SEND_BUF_SIZE) {
		mread(buf, SEND_BUF_SIZE, from);
		fwrite(buf, SEND_BUF_SIZE, 1, to);
	}

	if (cnt < 0) {
		mread(buf, cnt + SEND_BUF_SIZE, from);
		fwrite(buf, cnt + SEND_BUF_SIZE, 1, to);
	}

	fflush(to);
}



/*
 * build_imap_search()
 *
 * builds a linked list of search items from a set of IMAP search keys
 * sl should be initialized; new search items are simply added to the list
 *
 * returns -1 on syntax error, -2 on memory error; 0 on success, 1 if ')' has been encountered
 */
int build_imap_search(char **search_keys, struct dm_list *sl, int *idx, int sorted)
{
	search_key_t key;
	int result;

	if (!search_keys || !search_keys[*idx])
		return 0;

	memset(&key, 0, sizeof(key));
	/* coming from _ic_sort */
	if(sorted && (strcasecmp(search_keys[*idx], "arrival") == 0)) {
		key.type = IST_SORT;
		strncpy(key.search, "order by pms.internal_date", MAX_SEARCH_LEN);
		(*idx)++;
	} else if(sorted && (strcasecmp(search_keys[*idx], "from") == 0)) {
		key.type = IST_SORTHDR;
		strncpy(key.hdrfld, "from", MIME_FIELD_MAX);
		(*idx)++;
	} else if(sorted && (strcasecmp(search_keys[*idx], "subject") == 0)) {
		key.type = IST_SORTHDR;
		strncpy(key.hdrfld, "subject", MIME_FIELD_MAX);
		(*idx)++;
	} else if(sorted && (strcasecmp(search_keys[*idx], "cc") == 0)) {
		key.type = IST_SORTHDR;
		strncpy(key.hdrfld, "cc", MIME_FIELD_MAX);
		(*idx)++;
	} else if(sorted && (strcasecmp(search_keys[*idx], "to") == 0)) {
		key.type = IST_SORTHDR;
		strncpy(key.hdrfld, "to", MIME_FIELD_MAX);
		(*idx)++;
		
/* no silent failures for now */
		
//	} else if(sorted && (strcasecmp(search_keys[*idx], "reverse") == 0)) {
//		/* TODO */ 
//		(*idx)++;
//	} else if(sorted && (strcasecmp(search_keys[*idx], "size") == 0)) {
//		/* TODO */ 
//		(*idx)++;
//	} else if(sorted && (strcasecmp(search_keys[*idx], "us-ascii") == 0)) {
//		/* TODO */ 
//		(*idx)++;
//	} else if(sorted && (strcasecmp(search_keys[*idx], "iso-8859-1") == 0)) {
//		/* TODO */ 
//		(*idx)++;
//	} else if(sorted && (strcasecmp(search_keys[*idx], "date") == 0)) {
//		/* TODO */ 
//		(*idx)++;
//	} else if(sorted && (strcasecmp(search_keys[*idx], "all") == 0)) {
//		/* TODO */ 
//		(*idx)++;


	} else if (strcasecmp(search_keys[*idx], "all") == 0) {
		key.type = IST_SET;
		strcpy(key.search, "1:*");
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "uid") == 0) {
		key.type = IST_SET_UID;
		if (!search_keys[*idx + 1])
			return -1;

		(*idx)++;

		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		if (!check_msg_set(key.search))
			return -1;

		(*idx)++;
	}

	/*
	 * FLAG search keys
	 */

	else if (strcasecmp(search_keys[*idx], "answered") == 0) {
		key.type = IST_FLAG;
		strncpy(key.search, "answered_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "deleted") == 0) {
		key.type = IST_FLAG;
		strncpy(key.search, "deleted_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "flagged") == 0) {
		key.type = IST_FLAG;
		strncpy(key.search, "flagged_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "recent") == 0) {
		key.type = IST_FLAG;
		strncpy(key.search, "recent_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "seen") == 0) {
		key.type = IST_FLAG;
		strncpy(key.search, "seen_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "keyword") == 0) {
		/* no results from this one */
		if (!search_keys[(*idx) + 1])	/* there should follow an argument */
			return -1;

		(*idx)++;

		key.type = IST_SET;
		strcpy(key.search, "0");
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "draft") == 0) {
		key.type = IST_FLAG;
		strncpy(key.search, "draft_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "new") == 0) {
		key.type = IST_FLAG;
		strncpy(key.search, "(seen_flag=0 AND recent_flag=1)",
			MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "old") == 0) {
		key.type = IST_FLAG;
		strncpy(key.search, "recent_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "unanswered") == 0) {
		key.type = IST_FLAG;
		strncpy(key.search, "answered_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "undeleted") == 0) {
		key.type = IST_FLAG;
		strncpy(key.search, "deleted_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "unflagged") == 0) {
		key.type = IST_FLAG;
		strncpy(key.search, "flagged_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "unseen") == 0) {
		key.type = IST_FLAG;
		strncpy(key.search, "seen_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "unkeyword") == 0) {
		/* matches every msg */
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;

		key.type = IST_SET;
		strcpy(key.search, "1:*");
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "undraft") == 0) {
		key.type = IST_FLAG;
		strncpy(key.search, "draft_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	}

	/*
	 * HEADER search keys
	 */

	else if (strcasecmp(search_keys[*idx], "bcc") == 0) {
		key.type = IST_HDR;
		strncpy(key.hdrfld, "bcc", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "cc") == 0) {
		key.type = IST_HDR;
		strncpy(key.hdrfld, "cc", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "from") == 0) {
		key.type = IST_HDR;
		strncpy(key.hdrfld, "from", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "to") == 0) {
		key.type = IST_HDR;
		strncpy(key.hdrfld, "to", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "subject") == 0) {
		key.type = IST_HDR;
		strncpy(key.hdrfld, "subject", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "header") == 0) {
		key.type = IST_HDR;
		if (!search_keys[(*idx) + 1] || !search_keys[(*idx) + 2])
			return -1;

		strncpy(key.hdrfld, search_keys[(*idx) + 1],
			MIME_FIELD_MAX);
		strncpy(key.search, search_keys[(*idx) + 2],
			MAX_SEARCH_LEN);

		(*idx) += 3;
	} else if (strcasecmp(search_keys[*idx], "sentbefore") == 0) {
		key.type = IST_HDRDATE_BEFORE;
		strncpy(key.hdrfld, "date", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "senton") == 0) {
		key.type = IST_HDRDATE_ON;
		strncpy(key.hdrfld, "date", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "sentsince") == 0) {
		key.type = IST_HDRDATE_SINCE;
		strncpy(key.hdrfld, "date", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	}

	/*
	 * INTERNALDATE keys
	 */

	else if (strcasecmp(search_keys[*idx], "before") == 0) {
		key.type = IST_IDATE;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		if (!check_date(search_keys[*idx]))
			return -1;

		strncpy(key.search, "internal_date<'", MAX_SEARCH_LEN);
		strncat(key.search, date_imap2sql(search_keys[*idx]),
			MAX_SEARCH_LEN - sizeof("internal_date<''"));
		strcat(key.search, "'");

		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "on") == 0) {
		key.type = IST_IDATE;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		if (!check_date(search_keys[*idx]))
			return -1;

		strncpy(key.search, "internal_date LIKE '",
			MAX_SEARCH_LEN);
		strncat(key.search, date_imap2sql(search_keys[*idx]),
			MAX_SEARCH_LEN - sizeof("internal_date LIKE 'x'"));
		strcat(key.search, "%'");

		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "since") == 0) {
		key.type = IST_IDATE;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		if (!check_date(search_keys[*idx]))
			return -1;

		strncpy(key.search, "internal_date>'", MAX_SEARCH_LEN);
		strncat(key.search, date_imap2sql(search_keys[*idx]),
			MAX_SEARCH_LEN - sizeof("internal_date>''"));
		strcat(key.search, "'");

		(*idx)++;
	}

	/*
	 * DATA-keys
	 */

	else if (strcasecmp(search_keys[*idx], "body") == 0) {
		key.type = IST_DATA_BODY;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "text") == 0) {
		key.type = IST_DATA_TEXT;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	}

	/*
	 * SIZE keys
	 */

	else if (strcasecmp(search_keys[*idx], "larger") == 0) {
		key.type = IST_SIZE_LARGER;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		key.size = strtoull(search_keys[(*idx)], NULL, 10);
		(*idx)++;
	} else if (strcasecmp(search_keys[*idx], "smaller") == 0) {
		key.type = IST_SIZE_SMALLER;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		key.size = strtoull(search_keys[(*idx)], NULL, 10);
		(*idx)++;
	}

	/*
	 * NOT, OR, ()
	 */
	else if (strcasecmp(search_keys[*idx], "not") == 0) {
		key.type = IST_SUBSEARCH_NOT;

		(*idx)++;
		if ((result =
		     build_imap_search(search_keys, &key.sub_search,
				       idx, sorted )) < 0) {
			dm_list_free(&key.sub_search.start);
			return result;
		}

		/* a NOT should be unary */
		if (key.sub_search.total_nodes != 1) {
			free_searchlist(&key.sub_search);
			return -1;
		}
	} else if (strcasecmp(search_keys[*idx], "or") == 0) {
		key.type = IST_SUBSEARCH_OR;

		(*idx)++;
		if ((result =
		     build_imap_search(search_keys, &key.sub_search,
				       idx, sorted)) < 0) {
			dm_list_free(&key.sub_search.start);
			return result;
		}

		if ((result =
		     build_imap_search(search_keys, &key.sub_search,
				       idx, sorted )) < 0) {
			dm_list_free(&key.sub_search.start);
			return result;
		}

		/* an OR should be binary */
		if (key.sub_search.total_nodes != 2) {
			free_searchlist(&key.sub_search);
			return -1;
		}

	} else if (strcasecmp(search_keys[*idx], "(") == 0) {
		key.type = IST_SUBSEARCH_AND;

		(*idx)++;
		while ((result =
			build_imap_search(search_keys, &key.sub_search,
					  idx, sorted)) == 0 && search_keys[*idx]);

		if (result < 0) {
			/* error */
			dm_list_free(&key.sub_search.start);
			return result;
		}

		if (result == 0) {
			/* no ')' encountered (should not happen, parentheses are matched at the command line) */
			free_searchlist(&key.sub_search);
			return -1;
		}
	} else if (strcasecmp(search_keys[*idx], ")") == 0) {
		(*idx)++;
		return 1;
	} else if (check_msg_set(search_keys[*idx])) {
		key.type = IST_SET;
		strncpy(key.search, search_keys[*idx], MAX_SEARCH_LEN);
		(*idx)++;
	} else {
		/* unknown search key */
		return -1;
	}

	if (!dm_list_nodeadd(sl, &key, sizeof(key)))
		return -2;

	return 0;
}


/*
 * perform_imap_search()
 *
 * returns 0 on succes, -1 on dbase error, -2 on memory error, 1 if result set is too small
 * (new mail has been added to mailbox while searching, mailbox data out of sync)
 */
int perform_imap_search(unsigned int *rset, int setlen, search_key_t * sk,
			mailbox_t * mb, int sorted, int condition)
{
	search_key_t *subsk;
	struct element *el;
	int result, i;
	unsigned int *newset = NULL;
	int subtype = IST_SUBSEARCH_OR;


	if (!setlen) // empty mailbox
		return 0;
	
	if (!rset) {
		trace(TRACE_ERROR,"%s,%s: error empty rset", __FILE__, __func__);
		return -2;	/* stupidity */
	}

	if (!sk) {
		trace(TRACE_ERROR,"%s,%s: error empty sk", __FILE__, __func__);
		return -2;	/* no search */
	}


	trace(TRACE_DEBUG,"%s,%s: search_key [%d] condition [%d]", __FILE__, __func__, sk->type, condition);
	
	switch (sk->type) {
	case IST_SET:
		build_set(rset, setlen, sk->search);
		break;

	case IST_SET_UID:
		build_uid_set(rset, setlen, sk->search, mb);
		break;

	case IST_SORT:
		result = db_search(rset, setlen, sk->search, mb, sk->type);
		return 0;
		break;

	case IST_SORTHDR:
		result = db_sort_parsed(rset, setlen, sk, mb, condition);
		return 0;
		break;

	case IST_FLAG:
		if ((result = db_search(rset, setlen, sk->search, mb, sk->type)))
			return result;
		break;

	case IST_HDR:
	case IST_HDRDATE_BEFORE:
	case IST_HDRDATE_ON:
	case IST_HDRDATE_SINCE:
	case IST_DATA_BODY:
	case IST_DATA_TEXT:
	case IST_SIZE_LARGER:
	case IST_SIZE_SMALLER:
		/* these all have in common that a message should be parsed before 
		   matching is possible
		 */
		result = db_search_parsed(rset, setlen, sk, mb, condition);
		break;

	case IST_IDATE:
		if ((result = db_search(rset, setlen, sk->search, mb, sk->type)))
			return result;
		break;

	case IST_SUBSEARCH_NOT:
	case IST_SUBSEARCH_AND:
		subtype = IST_SUBSEARCH_AND;

	case IST_SUBSEARCH_OR:

		if (! (newset = (int *)g_malloc0(sizeof(int) * setlen)))
			return -2;
		
		el = dm_list_getstart(&sk->sub_search);
		while (el) {
			subsk = (search_key_t *) el->data;

			if (subsk->type == IST_SUBSEARCH_OR)
				memset(newset, 0, sizeof(int) * setlen);
			else
				for (i = 0; i < setlen; i++)
					newset[i] = rset[i];

			if ((result = perform_imap_search(newset, setlen, subsk, mb, sorted, sk->type))) {
				dm_free(newset);
				return result;
			}

			if (! sorted)
				combine_sets(rset, newset, setlen, subtype);
			else {
				for (i = 0; i < setlen; i++)
					rset[i] = newset[i];
			}
 
			el = el->nextnode;
		}

		if (sk->type == IST_SUBSEARCH_NOT)
			invert_set(rset, setlen);

		break;

	default:
		dm_free(newset);
		return -2;	/* ??? */
	}

	dm_free(newset);
	return 0;
}


/*
 * frees the search-list sl
 *
 */
void free_searchlist(struct dm_list *sl)
{
	search_key_t *sk;
	struct element *el;

	if (!sl)
		return;

	el = dm_list_getstart(sl);

	while (el) {
		sk = (search_key_t *) el->data;

		free_searchlist(&sk->sub_search);
		dm_list_free(&sk->sub_search.start);

		el = el->nextnode;
	}

	dm_list_free(&sl->start);
	return;
}


void invert_set(unsigned int *set, int setlen)
{
	int i;

	if (!set)
		return;

	for (i = 0; i < setlen; i++)
		set[i] = !set[i];
}


void combine_sets(unsigned int *dest, unsigned int *sec, int setlen, int type)
{
	int i;

	if (!dest || !sec)
		return;

	if (type == IST_SUBSEARCH_AND) {
		for (i = 0; i < setlen; i++)
			dest[i] = (sec[i] && dest[i]);
	} else if (type == IST_SUBSEARCH_OR) {
		for (i = 0; i < setlen; i++)
			dest[i] = (sec[i] || dest[i]);
	}
}


/* 
 * build_set()
 *
 * builds a msn-set from a IMAP message set spec. the IMAP set is supposed to be correct,
 * no checks are performed.
 */
void build_set(unsigned int *set, unsigned int setlen, char *cset)
{
	unsigned int i;
	u64_t num, num2;
	char *sep = NULL;

	if ((! set) || (! cset))
		return;

	memset(set, 0, setlen * sizeof(int));

	do {
		num = strtoull(cset, &sep, 10);
		if (num <= setlen && num > 0) {
			if (!*sep)
				set[num - 1] = 1;
			else if (*sep == ',') {
				set[num - 1] = 1;
				cset = sep + 1;
			} else {
				/* sep == ':' here */
				sep++;
				if (*sep == '*') {
					for (i = num - 1; i < setlen; i++)
						set[i] = 1;
					cset = sep + 1;
				} else {
					cset = sep;
					num2 = strtoull(cset, &sep, 10);

					if (num2 > setlen)
						num2 = setlen;
					if (num2 > 0) {
						/* NOTE: here: num2 > 0, num > 0 */
						if (num2 < num) {
							/* swap! */
							i = num;
							num = num2;
							num2 = i;
						}

						for (i = num - 1; i < num2; i++)
							set[i] = 1;
					}
					if (*sep)
						cset = sep + 1;
				}
			}
		} else if (*sep) {
			/* invalid char, skip it */
			cset = sep + 1;
			sep++;
		}
	} while (sep && *sep && cset && *cset);
}


/* 
 * build_uid_set()
 *
 * as build_set() but takes uid's instead of MSN's
 */
void build_uid_set(unsigned int *set, unsigned int setlen, char *cset,
		   mailbox_t * mb)
{
	unsigned int i, msn, msn2;
	int result;
	int num2found = 0;
	u64_t num, num2;
	char *sep = NULL;

	if (!set)
		return;

	memset(set, 0, setlen * sizeof(int));

	if (!cset || setlen == 0)
		return;

	do {
		num = strtoull(cset, &sep, 10);
		result =
		    binary_search(mb->seq_list, mb->exists, num, &msn);

		if (result < 0 && num < mb->seq_list[mb->exists - 1]) {
			/* ok this num is not a UID, but if a range is specified (i.e. 1:*) 
			 * it is valid -> check *sep
			 */
			if (*sep == ':') {
				result = 1;
				for (msn = 0; mb->seq_list[msn] < num;
				     msn++);
				if (msn >= mb->exists)
					msn = mb->exists - 1;
			}
		}

		if (result >= 0) {
			if (!*sep)
				set[msn] = 1;
			else if (*sep == ',') {
				set[msn] = 1;
				cset = sep + 1;
			} else {
				/* sep == ':' here */
				sep++;
				if (*sep == '*') {
					for (i = msn; i < setlen; i++)
						set[i] = 1;

					cset = sep + 1;
				} else {
					/* fetch second number */
					cset = sep;
					num2 = strtoull(cset, &sep, 10);
					result =
					    binary_search(mb->seq_list,
							  mb->exists, num2,
							  &msn2);

					if (result < 0) {
						/* in a range: (like 1:1000) so this number doesnt need to exist;
						 * find the closest match below this UID value
						 */
						if (mb->exists == 0)
							num2found = 0;
						else {
							for (msn2 =
							     mb->exists -
							     1;; msn2--) {
								if (msn2 ==
								    0
								    && mb->
								    seq_list
								    [msn2]
								    > num2) {
									num2found
									    =
									    0;
									break;
								} else
								    if
								    (mb->
								     seq_list
								     [msn2]
								     <=
								     num2)
								{
									/* found! */
									num2found
									    =
									    1;
									break;
								}
							}
						}

					} else
						num2found = 1;

					if (num2found == 1) {
						if (msn2 < msn) {
							/* swap! */
							i = msn;
							msn = msn2;
							msn2 = i;
						}

						for (i = msn; i <= msn2;
						     i++)
							set[i] = 1;
					}

					if (*sep)
						cset = sep + 1;
				}
			}
		} else {
			/* invalid num, skip it */
			if (*sep) {
				cset = sep + 1;
				sep++;
			}
		}
	} while (sep && *sep && cset && *cset);
}


void dumpsearch(search_key_t * sk, int level)
{
	char *spaces = (char *) dm_malloc(level * 3 + 1);
	struct element *el;
	search_key_t *subsk;

	if (!spaces)
		return;

	memset(spaces, ' ', level * 3);
	spaces[level * 3] = 0;

	if (!sk) {
		trace(TRACE_DEBUG, "%s(null)\n", spaces);
		dm_free(spaces);
		return;
	}

	switch (sk->type) {
	case IST_SUBSEARCH_NOT:
		trace(TRACE_DEBUG, "%sNOT\n", spaces);

		el = dm_list_getstart(&sk->sub_search);
		if (el)
			subsk = (search_key_t *) el->data;
		else
			subsk = NULL;

		dumpsearch(subsk, level + 1);
		break;

	case IST_SUBSEARCH_AND:
		trace(TRACE_DEBUG, "%sAND\n", spaces);
		el = dm_list_getstart(&sk->sub_search);

		while (el) {
			subsk = (search_key_t *) el->data;
			dumpsearch(subsk, level + 1);

			el = el->nextnode;
		}
		break;

	case IST_SUBSEARCH_OR:
		trace(TRACE_DEBUG, "%sOR\n", spaces);
		el = dm_list_getstart(&sk->sub_search);

		while (el) {
			subsk = (search_key_t *) el->data;
			dumpsearch(subsk, level + 1);

			el = el->nextnode;
		}
		break;

	default:
		trace(TRACE_DEBUG, "%s[type %d] \"%s\"\n", spaces,
		      sk->type, sk->search);
	}

	dm_free(spaces);
	return;
}


/*
 * closes the msg cache
 */
void close_cache()
{
	if (cached_msg.msg_parsed)
		db_free_msg(&cached_msg.msg);

	cached_msg.num = -1;
	cached_msg.msg_parsed = 0;
	memset(&cached_msg.msg, 0, sizeof(cached_msg.msg));

	mclose(&cached_msg.memdump);
	mclose(&cached_msg.tmpdump);
}

/* 
 * init cache 
 */
int init_cache()
{
	cached_msg.num = -1;
	cached_msg.msg_parsed = 0;
	memset(&cached_msg.msg, 0, sizeof(cached_msg.msg));

	cached_msg.memdump = mopen();
	if (!cached_msg.memdump)
		return -1;

	cached_msg.tmpdump = mopen();
	if (!cached_msg.tmpdump) {
		mclose(&cached_msg.memdump);
		return -1;
	}

	cached_msg.file_dumped = 0;
	cached_msg.dumpsize = 0;
	return 0;
}

/* unwrap strings */
int mime_unwrap(char *to, const char *from) 
{
	while (*from) {
		if (((*from == '\n') || (*from == '\r')) && isspace(*(from+1))) {
			from+=2;
			continue;
		}
		*to++=*from++;
	}
	*to='\0';
	return 0;
}




