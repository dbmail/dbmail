/*
 Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

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
 * clientinfo.h
 *
 * definition of clientinfo structure & initializing function prototypes
 */

#ifndef CLIENT_INFO_H
#define CLIENT_INFO_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include "dbmail.h"

#define IPNUM_LEN 32

typedef struct
{
  FILE *tx,*rx;
  char ip[IPNUM_LEN];       /* client IP-number */
  field_t clientname;       /* resolved client ip */
  char *timeoutMsg;
  int  timeout;             /* server timeout (seconds) */
  void *userData;
} clientinfo_t;


typedef clientinfo_t ClientInfo; /* FIXME alias shouldn't be necessary */

#endif
