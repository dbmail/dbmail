/*
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

#ifndef DBMAIL_USER_H
#define DBMAIL_USER_H

#include "dbmail.h"


#define qverbosef(fmt, args...) if (verbose) printf(fmt, ##args)
#define qprintf(fmt, args...) if (! (quiet||reallyquiet)) printf(fmt, ##args) 
#define qerrorf(fmt, args...) if (! reallyquiet) fprintf(stderr, fmt, ##args) 

#define null_strncpy(dst, src, len) if (src) strncpy(dst, src, len)
#define null_crypt(src, dst) (src ? crypt(src, dst) : "" )


/* These are the available password types. */
typedef enum {
	PLAINTEXT = 0, PLAINTEXT_RAW, CRYPT, CRYPT_RAW,
	MD5_HASH, MD5_HASH_RAW, MD5_DIGEST, MD5_DIGEST_RAW,
	MD5_BASE64, MD5_BASE64_RAW, SHADOW, PWTYPE_NULL,
	DM_WHIRLPOOL, DM_SHA512, DM_SHA256, DM_SHA1, DM_TIGER
} pwtype;

int mkpassword(const char * const user, const char * const passwd,
               const char * const passwdtype, const char * const passwdfile,
               char ** password, char ** enctype);

/* The prodigious use of const ensures that programming
 * mistakes inside of these functions don't cause us to
 * use incorrect values when calling auth_ and db_ internals.
 * */

/* Core operations */
int do_add(const char * const user,
           const char * const password,
           const char * const enctype,
           const uint64_t maxmail, const uint64_t clientid,
	   GList * alias_add,
	   GList * alias_del);
int do_delete(const uint64_t useridnr, const char * const user);
int do_show(const char * const user);
int do_empty(const uint64_t useridnr);
/* Change operations */
int do_username(const uint64_t useridnr, const char *newuser);
int do_maxmail(const uint64_t useridnr, const uint64_t maxmail);
int do_clientid(const uint64_t useridnr, const uint64_t clientid);
int do_password(const uint64_t useridnr,
                const char * const password,
                const char * const enctype);
int do_spasswd(const uint64_t useridnr, const char * const password);
int do_saction(const uint64_t useridnr, long int saction);
int do_enable(const uint64_t useridnr, gboolean enable);
int do_aliases(const uint64_t useridnr,
               GList * alias_add,
               GList * alias_del);
/* External forwards */
int do_forwards(const char *alias, const uint64_t clientid,
                GList * fwds_add,
                GList * fwds_del);

/* Helper functions */
uint64_t strtomaxmail(const char * const str);

#endif
