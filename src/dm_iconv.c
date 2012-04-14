/*
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

/* Iconv utility functions */


#include "dbmail.h"

#define THIS_MODULE "iconv"

static GOnce iconv_once = G_ONCE_INIT;

struct DbmailIconv *ic;

static GStaticRecMutex mutex = G_STATIC_REC_MUTEX_INIT;

static void dbmail_iconv_close(void)
{
	TRACE(TRACE_DEBUG,"closing");
	ic->to_db = NULL;
	ic->from_db = NULL;
	ic->from_msg = NULL;
	g_free(ic);
	ic = NULL;
}

static gpointer dbmail_iconv_once(gpointer UNUSED data)
{
	ic = g_new0(struct DbmailIconv,1);	

	memset(ic->db_charset,'\0', FIELDSIZE);
	memset(ic->msg_charset,'\0', FIELDSIZE);

	ic->to_db = (iconv_t)-1;
	ic->from_msg = (iconv_t)-1;

	GETCONFIGVALUE("ENCODING", "DBMAIL", ic->db_charset);
	GETCONFIGVALUE("DEFAULT_MSG_ENCODING", "DBMAIL", ic->msg_charset);

	if (! ic->db_charset[0])
		g_strlcpy(ic->db_charset,g_mime_locale_charset(), FIELDSIZE);

	if (! ic->msg_charset[0])
		g_strlcpy(ic->msg_charset, g_mime_locale_charset(), FIELDSIZE);

	TRACE(TRACE_DEBUG,"Initialize DB encoding surface [UTF-8..%s]", ic->db_charset);
	ic->to_db = g_mime_iconv_open(ic->db_charset,"UTF-8");
	if (ic->to_db == (iconv_t)-1)
		TRACE(TRACE_EMERG, "iconv failure");

	TRACE(TRACE_DEBUG,"Initialize DB decoding surface [%s..UTF-8]", ic->db_charset);
	ic->from_db = g_mime_iconv_open("UTF-8", ic->db_charset);
	if (ic->to_db == (iconv_t)-1)
		TRACE(TRACE_EMERG, "iconv failure");

	TRACE(TRACE_DEBUG,"Initialize default MSG decoding surface [%s..UTF-8]", ic->msg_charset);
	ic->from_msg=g_mime_iconv_open("UTF-8",ic->msg_charset);
	if (ic->from_msg == (iconv_t)-1)
		TRACE(TRACE_EMERG, "iconv failure");

	atexit(dbmail_iconv_close);

	return (gpointer)NULL;
}

void dbmail_iconv_init(void)
{
	g_once(&iconv_once, dbmail_iconv_once, NULL);
}

/* convert not encoded field to utf8 */
char * dbmail_iconv_str_to_utf8(const char* str_in, const char *charset)
{
	char * subj=NULL, *t;
	iconv_t conv_iconv = (iconv_t)-1;

	dbmail_iconv_init();

	if (str_in==NULL)
		return NULL;

	t = (char *)str_in;

	if (g_utf8_validate((const gchar *)str_in, -1, NULL) || !g_mime_utils_text_is_8bit((unsigned char *)str_in, strlen(str_in)))
		return g_strdup(t);

	if (charset) {
		if ((conv_iconv=g_mime_iconv_open("UTF-8",charset)) != (iconv_t)-1) {
			subj = g_mime_iconv_strdup(conv_iconv,str_in);
			g_mime_iconv_close(conv_iconv);
		}
	}

	if (subj==NULL) {
		LOCK(&mutex);
		subj=g_mime_iconv_strdup(ic->from_msg,str_in);
		UNLOCK(&mutex);
	}

	if (subj==NULL) {
		subj=g_strdup(str_in);
		char *p;
		for(p=subj;*p;p++)
			if(*p & 0x80) *p='?';
	}

	return subj;
}

/* convert encoded field to database encoding */
char * dbmail_iconv_str_to_db(const char* str_in, const char *charset)
{
	char * subj=NULL;
	iconv_t conv_iconv;

	dbmail_iconv_init();

	if ( str_in == NULL )
		return NULL;

	if (! g_mime_utils_text_is_8bit((unsigned char *)str_in, strlen(str_in)) )
		return g_strdup(str_in);

	LOCK(&mutex);
	subj = g_mime_iconv_strdup(ic->to_db,str_in);
	UNLOCK(&mutex);

	if (subj != NULL)
		return subj;

	if (charset) {
		conv_iconv = g_mime_iconv_open(ic->db_charset,charset);
		if (conv_iconv != (iconv_t)-1) {
			subj=g_mime_iconv_strdup(conv_iconv,str_in);
			g_mime_iconv_close(conv_iconv);
		}
	}

	if (subj==NULL) {
		char *subj2;

		LOCK(&mutex);
		subj2 = g_mime_iconv_strdup(ic->from_msg,str_in);
		UNLOCK(&mutex);

		if (subj2 != NULL) {
			LOCK(&mutex);
			subj = g_mime_iconv_strdup(ic->to_db, subj2);
			UNLOCK(&mutex);
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

	if (! g_utf8_validate((const char *)str_in,-1,NULL)) {
		LOCK(&mutex);
		subj = g_mime_iconv_strdup(ic->from_db, str_in);
		UNLOCK(&mutex);
		if (subj != NULL){
			gchar *subj2;
			subj2 = g_mime_utils_header_encode_text((const char *)subj);
			g_free(subj);
			return subj2;
		}
	}

	return g_mime_utils_header_encode_text(str_in);
}

char * dbmail_iconv_decode_text(const char *in)
{
	return g_mime_utils_header_decode_text(in);
}
char * dbmail_iconv_decode_address(char *address)
{
	return g_mime_utils_header_decode_phrase(address);
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

	g_free(tmp_raw);

	return value;
}

