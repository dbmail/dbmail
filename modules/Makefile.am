# 
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
AM_CFLAGS = @MYSQLINC@ @PGSQLINC@ @SQLITEINC@ @LDAPINC@ @SIEVEINC@



#AM_LDFLAGS = -Wl,--export-dynamic
if SHARED
AM_LDFLAGS = -module
else
AM_LDFLAGS = 
endif

noinst_LTLIBRARIES = libsort_null.la


if MYSQL
libmysql_la_SOURCES = dbmysql.c
libmysql_la_LIBADD = @MYSQLLIB@
dbmail_mysql_la = libmysql.la
endif

if PGSQL
libpgsql_la_SOURCES = dbpgsql.c
libpgsql_la_LIBADD = @PGSQLLIB@
dbmail_pgsql_la = libpgsql.la
endif

if SQLITE
libsqlite_la_SOURCES = dbsqlite.c
libsqlite_la_LIBADD = @SQLITELIB@
dbmail_sqlite_la = libsqlite.la
endif

if INGRES
libmod_ingres_la_SOURCES = dbingres.c
libmod_ingres_la_LIBADD = @INGRESLIB@
endif

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

pkglib_LTLIBRARIES = $(dbmail_mysql_la) \
	$(dbmail_pgsql_la) \
	$(dbmail_sqlite_la) \
	$(dbmail_sieve_la) \
	$(dbmail_ldap_la) \
	libauth_sql.la
	 
