/*
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

#include "dbmail.h"

#define THIS_MODULE "cidr"

#define T Cidr_T

struct T {
	char *sock_str;
	struct sockaddr_in *socket;
	short int mask;
	const char repr[FIELDSIZE];
};

T cidr_new(const char *str)
{
	T self;
	char *addr, *port, *mask;
	char *haddr, *hport;
	unsigned i;
	size_t l;

	assert(str != NULL);
	
	self = (T)g_malloc0(sizeof(*self));
	self->sock_str = g_strdup(str);
	self->socket = (struct sockaddr_in *)g_malloc0(sizeof(struct sockaddr_in));
	self->mask = 32;

	addr = g_strdup(str);
	haddr = addr;
	while (*addr && *addr != ':')
		addr++;
	if (*addr == ':')
		addr++;
	
	port = g_strdup(addr);
	hport = port;
	while (*port && *port != ':')
		port++;
	if (*port == ':')
		port++;

	/* chop port */
	l = strlen(addr);
	for (i=0; i<l; i++) {
		if (addr[i] == ':') {
			addr[i]='\0';
			break;
		}
	}
	
	mask = index(addr,'/');
	if (mask && mask[1] != '\0') {
		mask++;
		self->mask = atoi(mask);

		/* chop mask */
		l = strlen(addr);
		for(i=0; i<l; i++) {
			if (addr[i] == '/') {
				addr[i]='\0';
				break;
			}
		}
	}
	

	self->socket->sin_family = AF_INET;
	self->socket->sin_port = strtol(port,NULL,10);
	if (! inet_aton(addr,&self->socket->sin_addr)) {
		g_free(haddr);
		g_free(hport);
		cidr_free(&self);
		return NULL;
	}

	if (self->socket->sin_addr.s_addr == 0)
		self->mask = 0;
		
	g_free(haddr);
	g_free(hport);

	g_snprintf((char *)self->repr, FIELDSIZE-1, "struct cidrfilter {\n"
			"\tsock_str: %s;\n"
			"\tsocket->sin_addr: %s;\n"
			"\tsocket->sin_port: %d;\n"
			"\tmask: %d;\n"
			"};\n",
			self->sock_str,
			inet_ntoa(self->socket->sin_addr), 
			self->socket->sin_port,
			self->mask
			);

	TRACE(TRACE_DEBUG,"%s", cidr_repr(self));
	return self;
}

const char * cidr_repr(T self)
{
	return self->repr;
}

int cidr_match(T base, T test)
{
	char *fullmask = "255.255.255.255";
	struct in_addr match_addr, base_addr, test_addr;
	inet_aton(fullmask, &base_addr);
	inet_aton(fullmask, &test_addr);
	unsigned result = 0;

	if (base->mask)
		base_addr.s_addr = ~((unsigned long)base_addr.s_addr >> (32-base->mask));
	if (test->mask)
		test_addr.s_addr = ~((unsigned long)test_addr.s_addr >> (32-test->mask));

	base_addr.s_addr = (base->socket->sin_addr.s_addr | base_addr.s_addr);
	test_addr.s_addr = (test->socket->sin_addr.s_addr | test_addr.s_addr);
	match_addr.s_addr = (base_addr.s_addr & test_addr.s_addr);

	/* only match ports if specified */
	if (test->socket->sin_port > 0 && (base->socket->sin_port != test->socket->sin_port))
		return 0;
	
	if (test_addr.s_addr == match_addr.s_addr) 
		result = base->mask ? base->mask : 32;
			
	if ((! base->mask) || (! test->mask))
		result = 32;

	return result;
	
}

void cidr_free(T *self)
{
	T s = *self;
	if (! s) return;

	if (s->socket)
		g_free(s->socket);
	if (s->sock_str)
		g_free(s->sock_str);
	if (s) g_free(s);

	s = NULL;
}

