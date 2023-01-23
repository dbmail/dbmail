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

#ifndef DM_DIGEST_H
#define DM_DIGEST_H

int dm_digest(const unsigned char * hash, hashid type, char *);
int dm_tiger(const char * const s, char *);
int dm_sha1(const char * const s, char *);
int dm_sha256(const char * const s, char *);
int dm_sha512(const char * const s, char *);
int dm_whirlpool(const char * const s, char *);
int dm_md5(const char * const s, char *);
int dm_md5_base64(const char * const s, char *);

#endif
