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


if SHARED
LDFLAGS = -Wl,--export-dynamic
endif
pkglib_LTLIBRARIES = libdbmysql.la \
	libdbpgsql.la \
	libdbsqlite.la \
	libauthsql.la \
	libauthldap.la \
	libsortsieve.la

# This one is always built.
libauthsql_la_SOURCES = authsql.c

if MYSQL
libdbmysql_la_SOURCES = dbmysql.c
endif

if PGSQL
libdbpgsql_la_SOURCES = dbpgsql.c
endif

if SQLITE
libdbsqlite_la_SOURCES = dbsqlite.c
endif

if LDAP
libauthldap_la_SOURCES = authldap.c
endif

if SIEVE
libsortsieve_la_SOURCES = sortsieve.c
endif

libdbmysql_la_LIBADD = @MYSQLLIB@
libdbpgsql_la_LIBADD = @PGSQLLIB@
libdbsqlite_la_LIBADD = @SQLITELIB@
libauthsql_la_LIBADD = -lcrypt @MYSQLLIB@ @PGSQLLIB@ @SQLITELIB@
libauthldap_la_LIBADD = -lcrypt @LDAPLIB@ @MYSQLLIB@ @PGSQLLIB@ @SQLITELIB@
libsortsieve_la_LIBADD = @SORTLIB@

