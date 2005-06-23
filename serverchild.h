/*
  $Id$
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

/*
 * serverchild.h
 *
 * function prototypes for the children of the main server process;
 * the children will be responsible for handling client connections.
 */

#ifndef SERVERCHILD_H
#define SERVERCHILD_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <signal.h>
#include "dbmailtypes.h"

void active_child_sig_handler(int sig, siginfo_t *info, void *data);
void noop_child_sig_handler(int sig, siginfo_t *info, void *data);
int SetChildSigHandler(void);
int DelChildSigHandler(void);
pid_t CreateChild(ChildInfo_t * info);

int manage_start_cli_server(ChildInfo_t * info);

#endif
