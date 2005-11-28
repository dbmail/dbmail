/* $Id: sortsieve.h 1912 2005-11-19 02:29:41Z aaron $ 

 Copyright (C) 2004 Aaron Stone aaron at serendipity dot cx

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

 * Headers for sieve.c */

#ifndef _SIEVE_H
#define _SIEVE_H

#include "dbmail.h"


#define MAX_SIEVE_SCRIPTNAME 100

int sortsieve_msgsort(u64_t useridnr, char *header, u64_t headersize,
		      u64_t messagesize, struct dm_list *actions);
int sortsieve_script_validate(u64_t user_idnr, char *scriptname, char **errmsg);

#endif
