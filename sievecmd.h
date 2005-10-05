/*
 Copyright (C) 2003 Aaron Stone

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

/* $Id$ */

#ifndef _SIEVECMD_H
#define _SIEVECMD_H

#include "dbmail.h"



int do_showhelp(void);
int do_list(u64_t user_idnr);
int do_activate(u64_t user_idnr, char *name);
int do_deactivate(u64_t user_idnr, char *name);
int do_remove(u64_t user_idnr, char *name);
int do_insert(u64_t user_idnr, char *name, FILE * source);

int read_script_file(FILE * f, char **m_buf);

#endif
