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
 * proctitleutils.h
 *
 * headerfile declaring prototypes for functions to adjust their title 
 * in the process list
 * 
 */

#ifndef PROC_TITLE_UTILS_H
#define PROC_TITLE_UTILS_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

void set_proc_title(char *fmt, ...);
void init_set_proc_title(int argc, char *argv[], char *envp[],
			 const char *name);

#endif
