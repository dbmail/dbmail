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

/* $Id: forward.h 1756 2005-04-19 07:37:27Z paul $ 
 */

#ifndef _FORWARD_H
#define _FORWARD_H

#define FW_SENDMAIL SENDMAIL

int forward(u64_t msgidnr, struct dm_list *targets, const char *from,
	    const char *header, u64_t headersize);

#endif
