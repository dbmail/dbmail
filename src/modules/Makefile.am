# 
# Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
# Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
# Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
# Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk
#
# This program is free software; you can redistribute it and/or 
# modify it under the terms of the GNU General Public License 
# as published by the Free Software Foundation; either 
# version 2 of the License, or (at your option) any later 
# version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

AM_CPPFLAGS = -I$(top_srcdir)/src
AM_CFLAGS = @LDAPINC@ @SIEVEINC@

if SHARED
AM_LDFLAGS = -module
else
AM_LDFLAGS = 
endif

noinst_LTLIBRARIES = libsort_null.la

# This one is always built.
libsort_null_la_SOURCES = sortnull.c

if SIEVE
libsort_sieve_la_SOURCES = sortsieve.c
libsort_sieve_la_LIBADD = @SIEVELIB@
dbmail_sieve_la = libsort_sieve.la
endif

# This one is always built.
libauth_sql_la_SOURCES = authsql.c
libauth_sql_la_LIBADD = @CRYPTLIB@

if LDAP
libauth_ldap_la_SOURCES = authldap.c
libauth_ldap_la_LIBADD = @CRYPTLIB@ @LDAPLIB@ 
dbmail_ldap_la = libauth_ldap.la
endif

pkglib_LTLIBRARIES = $(dbmail_sieve_la) \
	$(dbmail_ldap_la) \
	libauth_sql.la
	 
