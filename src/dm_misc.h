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

#ifndef DM_MISC_H
#define DM_MISC_H

#include "dbmail.h"

#define BUFLEN 2048
 
/*
 * Resize the allocated size for a GString
 *
 * GLib never scales down the allocated space 
 * for GString.
 */
void g_string_maybe_shrink(GString *s);

/**
   \brief drop process privileges. Change change euid and egid to
   uid and gid of newuser and newgroup
   \param newuser user to change to
   \param newgroup group to change to
   \return 
        - -1 on error
	-  0 on success
*/
int drop_privileges(char *newuser, char *newgroup);

/**
   \brief get the number of opened files (requires /proc mounted)
   \return
        - -1 on error
        - number of opened files
*/
int get_opened_fd_count(void);

/**
 * \brief create a unique id for a message (used for pop, stored per message)
 * \param target target string. Length should be UID_SIZE 
 * \param message_idnr message_idnr of message
 */
void create_unique_id(/*@out@*/ char *target, uint64_t message_idnr);

/**
 * \brief create a timestring with the current time.
 * \param timestring an allocated timestring object.
 */
void create_current_timestring(TimeString_T * timestring);

/**
 * \brief decorate a mailbox name with a namespace if needed
 * \param mailbox_name name of mailbox
 * \param owner_idnr owner idnr of mailbox
 * \param user_idnr idnr of current user
 * \return
 *     - NULL on error
 *     - fully qualified mailbox name otherwise (e.g. #Users/username/INBOX)
 * \note caller should free returned string
 */
char *mailbox_add_namespace(const char *mailbox_name, uint64_t owner_idnr,
			    uint64_t user_idnr);

/**
 * \brief remove the namespace from the fully qualified name
 * \param fq_name full name (with possible namespace) of mailbox
 * \param namespace returns static pointer to namespace; ignored if NULL.
 * \param username returns allocated username;
 * 	only allocated if return value is non-null;
 * 	ignored if NULL;
 * \return
 *     - NULL on error
 *     - simple name of mailbox
 */
char *mailbox_remove_namespace(char *fq_name, char **namespace, char **username);

/**
 * \brief converts an IMAP date to a number (strictly ascending in date)
 * valid IMAP dates:
 *     - d-mon-yyyy
 *     - dd-mon-yyyy  ('-' may be a space)
 * \param date the IMAP date
 * \return integer representation of the date
 */
int num_from_imapdate(const char *date);


int find_bounded(const char * const value, char left, char right,
		char **retchar, size_t * retsize, size_t * retlast);
int zap_between(const char * const instring, signed char left, signed char right,
		char **outstring, size_t *outlen, size_t *zaplen);

GList * g_string_split(GString * string, const gchar * sep);

char * g_strcasestr(const char *haystack, const char *needle);

gint ucmpdata(const uint64_t *a, const uint64_t *b, gpointer data);
gint ucmp(const uint64_t *a, const uint64_t *b);
gint dm_strcmpdata(gconstpointer a, gconstpointer b, gpointer data);
gint dm_strcasecmpdata(gconstpointer a, gconstpointer b, gpointer data);

GList * g_tree_keys(GTree *tree);
GList * g_tree_values(GTree *tree);
void tree_dump(GTree *t);
int g_tree_merge(GTree *a, GTree *b, int condition);
int g_tree_copy_MessageInfo(GTree *a, GTree *b);
int g_tree_copy_String(GTree *a, GTree *b);
    
void pack_char(char *in, char c);

char * dm_shellesc(const char * command);
void dm_pack_spaces(char *in);
char * dm_base_subject(const char *subject);
int listex_match(const char *p, const char *s, const char *x, int flags);
uint64_t dm_getguid(unsigned int serverid);

int dm_sock_score(const char *base, const char *test);
int dm_sock_compare(const char *clientsock, const char *sock_allow, const char *sock_deny);
int dm_valid_format(const char *str);


char *date_sql2imap(const char *sqldate);
int date_imap2sql(const char *imapdate, char *);

int checkmailboxname(const char *s);
int check_msg_set(const char *s);
int check_date(const char *date);

/**
 * \brief discards all input coming from instream
 * \param instream FILE stream holding input from a client
 * \return 
 *      - -1 on error
 *      -  0 on success
 */
int discard_client_input(ClientBase_T *ci);

char * dbmail_imap_astring_as_string(const char *s);
char * dbmail_imap_plist_as_string(GList *plist);
GList* dbmail_imap_append_alist_as_plist(GList *list, InternetAddressList *ialist);
char * dbmail_imap_plist_collapse(const char *in);
void dbmail_imap_plist_free(GList *l);

char * imap_get_structure(GMimeMessage *message, gboolean extension);
char * imap_get_envelope(GMimeMessage *message);
GMimeObject * imap_get_partspec(const GMimeObject *message, const char *partspec);
char * imap_get_logical_part(const GMimeObject *object, const char * specifier);

char * imap_message_fetch_headers(uint64_t physid, const GList *headers, gboolean not);

char * imap_flags_as_string(MailboxState_T S, MessageInfo *msginfo);
char * imap_cleanup_address(const char *a);

const char * message_get_charset(GMimeMessage *self);

uint64_t dm_strtoull(const char *nptr, char **endptr, int base);

/* Free the result with g_strfreev. */
char **base64_decodev(char *in);

/* create a string containing the cryptographic checksum for buf */
int dm_get_hash_for_string(const char *buf, char *hash);

char * dm_base64_decode(const gchar *s, uint64_t *len);

uint64_t stridx(const char *s, char c);

#define get_crlf_encoded(string) get_crlf_encoded_opt(string, 0)
#define get_crlf_encoded_dots(string) get_crlf_encoded_opt(string, 1)
gchar * get_crlf_encoded_opt(const gchar *string, int dots);
void strip_crlf(char *buffer);
void uint64_free(void *);

int diff_time(struct timeval before, struct timeval after);

#endif
