
/* $Id$ 
 
 Copyright (C) 1999-2004 Aaron Stone aaron at serendipity dot cx

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

/* Headers for sorting.c */

#ifndef _SORTING_H
#define _SORTING_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include "dsn.h"
#include "debug.h"
#include "dbmailtypes.h"

#define SA_KEEP		1
#define SA_DISCARD	2
#define SA_REDIRECT	3
#define SA_REJECT	4
#define SA_FILEINTO	5
#define SA_SIEVE	6

typedef struct sort_action {
	int method;
	char *destination;
	char *message;
} sort_action_t;


#endif				/* #ifndef _SORTING_H */
