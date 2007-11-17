/*
 Copyright (c) 2007 NFG Net Facilities Group BV support@nfg.nl

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

/* Iconv utility functions */


#include "dbmail.h"

#define THIS_MODULE "iconv"

struct DbmailIconv *ic;

void dbmail_iconv_init(void)
{
	static gboolean initialized = FALSE;
	iconv_t tmp_i = (iconv_t)-1;

	if (initialized)
		return;

	ic = g_new0(struct DbmailIconv,1);	

	memset(ic->db_charset,'\0', FIELDSIZE);
	memset(ic->msg_charset,'\0', FIELDSIZE);

	ic->to_db = (iconv_t)-1;
	ic->from_msg = (iconv_t)-1;

	GETCONFIGVALUE("ENCODING", "DBMAIL", ic->db_charset);
	GETCONFIGVALUE("DEFAULT_MSG_ENCODING", "DBMAIL", ic->msg_charset);

	if(ic->db_charset[0]) {
		if ((tmp_i=g_mime_iconv_open(ic->db_charset,"UTF-8")) == (iconv_t)-1) {
			g_strlcpy(ic->db_charset, g_mime_locale_charset(),FIELDSIZE);
		} else {
			g_mime_iconv_close(tmp_i);
		}
	} else {
		g_strlcpy(ic->db_charset,g_mime_locale_charset(), FIELDSIZE);
	}

	if (ic->msg_charset[0]) {
		if ((tmp_i = g_mime_iconv_open(ic->msg_charset,"UTF-8")) == (iconv_t)-1) {
			g_strlcpy(ic->msg_charset, g_mime_locale_charset(), FIELDSIZE);

		} else {
			g_mime_iconv_close(tmp_i);
		}
	} else {
		g_strlcpy(ic->msg_charset, g_mime_locale_charset(), FIELDSIZE);
	}


	TRACE(TRACE_DEBUG,"Initialize DB encoding surface [UTF-8..%s]", ic->db_charset);
	ic->to_db = g_mime_iconv_open(ic->db_charset,"UTF-8");
	assert(ic->to_db != (iconv_t)-1);

	TRACE(TRACE_DEBUG,"Initialize DB decoding surface [%s..UTF-8]", ic->db_charset);
	ic->from_db = g_mime_iconv_open("UTF-8", ic->db_charset);
	assert(ic->to_db != (iconv_t)-1);

	TRACE(TRACE_DEBUG,"Initialize default MSG decoding surface [%s..UTF-8]", ic->msg_charset);
	ic->from_msg=g_mime_iconv_open("UTF-8",ic->msg_charset);
	assert(ic->from_msg != (iconv_t)-1);
	
	initialized = TRUE;
}

/* convert not encoded field to utf8 */
char * dbmail_iconv_str_to_utf8(const char* str_in, const char *charset)
{
	char * subj=NULL;
	iconv_t conv_iconv = (iconv_t)-1;

	dbmail_iconv_init();

	if (str_in==NULL)
		return NULL;

	if (g_utf8_validate((const gchar *)str_in, -1, NULL) || !g_mime_utils_text_is_8bit((unsigned char *)str_in, strlen(str_in)))
		return g_strdup(str_in);

 	if (charset) {
 		if ((conv_iconv=g_mime_iconv_open("UTF-8",charset)) != (iconv_t)-1) {
			subj=g_mime_iconv_strdup(conv_iconv,str_in);
			g_mime_iconv_close(conv_iconv);
		}
	}
	if (subj==NULL)
		subj=g_mime_iconv_strdup(ic->from_msg,str_in);
	    
	if (subj==NULL) {
		subj=g_strdup(str_in);
		char *p;
		for(p=subj;*p;p++)
		    if(*p & 0x80) *p='?';
	}
 
	return subj;
}

/* convert not encoded field to database encoding */
char * dbmail_iconv_str_to_db(const char* str_in, const char *charset)
{
	char * subj=NULL;
	iconv_t conv_iconv;

	dbmail_iconv_init();

	if ( str_in == NULL )
		return NULL;
	
	if (! g_mime_utils_text_is_8bit((unsigned char *)str_in, strlen(str_in)) )
		return g_strdup(str_in);

	if ((subj=g_mime_iconv_strdup(ic->to_db,str_in)) != NULL)
		return subj;

 	if (charset) {
 		if ((conv_iconv=g_mime_iconv_open(ic->db_charset,charset)) != (iconv_t)-1) {
 			subj=g_mime_iconv_strdup(conv_iconv,str_in);
 			g_mime_iconv_close(conv_iconv);
  		}
  	}
    
	if (subj==NULL) {
		char *subj2;
		if ((subj2 = g_mime_iconv_strdup(ic->from_msg,str_in)) != NULL) {
			subj = g_mime_iconv_strdup(ic->to_db, subj2);
			g_free(subj2);
		}
	}

	if (subj==NULL) {
		subj=g_strdup(str_in);
		char *p;
		for(p=subj;*p;p++)
		    if(*p & 0x80) *p='?';
	}

	return subj;
}
/* encode string from database encoding to mime (7-bit) */
char * dbmail_iconv_db_to_utf7(const char* str_in)
{
	char * subj=NULL;
	
	dbmail_iconv_init();

	if (str_in==NULL)
		return NULL;
	
	if (!g_mime_utils_text_is_8bit((unsigned char *)str_in, strlen(str_in)))
		return g_strdup(str_in);

	if ((! g_utf8_validate((const char *)str_in,-1,NULL)) && ((subj=g_mime_iconv_strdup(ic->from_db, str_in))!=NULL)) {
 		gchar *subj2;
		subj2 = g_mime_utils_header_encode_text((const char *)subj);
  		g_free(subj);
 		return subj2;
  	}

	return g_mime_utils_header_encode_text(str_in);
}

/* work around a bug in gmime (< 2.2.10) where utf7 strings are not completely decoded */
char * dbmail_iconv_decode_text(const char *in)
{
	size_t i=0, l=0, r=0, len, wlen=0;
	char p2=0, p = 0, c, n = 0;
	char *res, *s;
	gboolean inchar = FALSE, inword = FALSE;
	GString *buf = g_string_new("");
	GString *str = g_string_new("");

	len = strlen(in);
	for (i = 0; i<len; i++)
	{
		c = in[i];
		n = in[i+1];

		if ((c == '=') && (n == '?') && (inword == FALSE) && (inchar == FALSE)) {
			inchar = TRUE;
			l = i;
 		} else if (((p2 == 'q') || (p2 == 'Q') || (p2 == 'b') || (p2 == 'B')) && (p == '?') && inchar) {
			inchar = FALSE;
			inword = TRUE;
			wlen = 0;
		} else if ((p2 == '?') && (p == '=') && inword && wlen) {
			inword = FALSE;
			r = i;
		} else if (inword) {
			wlen++;
		}

		if (l < r) {
			s = g_mime_utils_header_decode_text(buf->str);
			g_string_append_printf(str, "%s", s);
			g_free(s);
			l = r;
			i = r;
			g_string_printf(buf,"%s","");
			g_string_append_c(str, in[i]);
		} else if (inword || inchar) {
			g_string_append_c(buf, in[i]);
		} else {
			g_string_append_c(str, in[i]);
		}

		p2 = p;
		p = c;
	}
	if (buf->len) {
		s = g_mime_utils_header_decode_text(buf->str);
		g_string_append_printf(str, "%s", s);
		g_free(s);
	}
	g_string_free(buf,TRUE);

	res = str->str;
	g_string_free(str,FALSE);

	return res;

}
/*
 * dbmail_iconv_decode_address
 * \param address the raw address header value
 * \result allocated string containing a utf8 encoded representation
 * 		of the input address
 *
 * 	paranoia rulez here, since input is untrusted
 */
char * dbmail_iconv_decode_address(char *address)
{
	InternetAddressList *l;
	char *r = NULL, *t = NULL, *s = NULL;

	if (address == NULL) return NULL;

 	// first make the address rfc2047 compliant if it happens to 
 	// contain 8bit data
	if ( (g_mime_utils_text_is_8bit((unsigned char *)address, strlen(address))) )
		s = g_mime_utils_header_encode_text((const char *)address);
	else
		s = g_strdup(address);

	// cleanup broken addresses before parsing
	t = imap_cleanup_address((const char *)s); g_free(s);

	// feed the result to gmime's address parser and
	// render it back to a hopefully now sane string
	l = internet_address_parse_string(t); g_free(t);
	s = internet_address_list_to_string(l, FALSE);
	internet_address_list_destroy(l);

	// now we're set to decode the rfc2047 address header
	// into clean utf8
	r = dbmail_iconv_decode_text(s); g_free(s);

	return r;
}

char * dbmail_iconv_decode_field(const char *in, const char *charset, gboolean isaddr)
{
	char *tmp_raw;
	char *value;

	if ((tmp_raw = dbmail_iconv_str_to_utf8((const char *)in, charset)) == NULL) {
		TRACE(TRACE_WARNING, "unable to decode headervalue [%s] using charset [%s]", in, charset);
		return NULL;
	}
	
	if (isaddr)
		value = dbmail_iconv_decode_address(tmp_raw);
	else
		value = dbmail_iconv_decode_text(tmp_raw);

	return value;
}



