/*
  
 Copyright (C) 2005 NFG Net Facilities Group BV paul@nfg.nl

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
 * 
 * $Id$
 * 
 * provide some CIDR utilities.
 * 
 */

struct cidrfilter {
	char *sock_str;
	struct sockaddr_in *socket;
	short int mask;
};

struct cidrfilter * cidr_new(const char *str);
int cidr_repr(struct cidrfilter *self);
int cidr_match(struct cidrfilter *base, struct cidrfilter *test);
void cidr_free(struct cidrfilter *self);

