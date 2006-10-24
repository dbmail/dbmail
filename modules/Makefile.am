# $Id: Makefile.am 1920 2005-11-26 08:32:15Z aaron $
# Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
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

INCLUDES = -I$(top_srcdir)
AM_CFLAGS = @MYSQLINC@ @PGSQLINC@ @SQLITEINC@ @LDAPINC@ @SIEVEINC@ -fomit-frame-pointer



#AM_LDFLAGS = -Wl,--export-dynamic
if SHARED
AM_LDFLAGS = -module
else
AM_LDFLAGS = 
endif

pkglibdir = $(libdir)

pkglib_LTLIBRARIES = libmysql.la \
	libpgsql.la \
	libsqlite.la \
	libsort_sieve.la \
	libauth_sql.la \
	libauth_ldap.la 

noinst_LTLIBRARIES = libsort_null.la


if MYSQL
libmysql_la_SOURCES = dbmysql.c
libmysql_la_LIBADD = @MYSQLLIB@
endif

if PGSQL
libpgsql_la_SOURCES = dbpgsql.c
libpgsql_la_LIBADD = @PGSQLLIB@
endif

if SQLITE
libsqlite_la_SOURCES = dbsqlite.c
libsqlite_la_LIBADD = @SQLITELIB@
endif

# This one is always built.
libsort_null_la_SOURCES = sortnull.c
libsort_null_la_LIBADD = @SORTLIB@

if SIEVE
libsort_sieve_la_SOURCES = sortsieve.c
libsort_sieve_la_LIBADD = @SORTLIB@
endif

# This one is always built.
libauth_sql_la_SOURCES = authsql.c
libauth_sql_la_LIBADD = -lcrypt

if LDAP
libauth_ldap_la_SOURCES = authldap.c
libauth_ldap_la_LIBADD = -lcrypt @LDAPLIB@ 
endif

