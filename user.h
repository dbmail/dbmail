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
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define PNAME "dbmail/users"

/* These are the available password types. */
typedef enum {
	PLAINTEXT = 0, PLAINTEXT_RAW, CRYPT, CRYPT_RAW,
	MD5_HASH, MD5_HASH_RAW, MD5_DIGEST, MD5_DIGEST_RAW,
	SHADOW, PWTYPE_NULL
} pwtype_t;

int mkpassword(const char * const user, const char * const passwd,
               const char * const passwdtype, const char * const passwdfile,
               char ** password, char ** enctype);


