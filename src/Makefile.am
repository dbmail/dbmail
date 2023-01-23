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

SUBDIRS = modules



if SIEVE
SIEVEPROGS = dbmail-sievecmd dbmail-timsieved
endif

if USE_DM_GETOPT
DM_GETOPT = dm_getopt.c
endif

sbin_PROGRAMS = dbmail-deliver \
	dbmail-pop3d \
	dbmail-imapd \
	dbmail-util \
	dbmail-users \
	dbmail-export \
	dbmail-httpd \
	dbmail-lmtpd $(SIEVEPROGS)

COMMON = dm_user.c \
	dm_message.c \
	dm_mailbox.c \
	dm_mailboxstate.c \
	dm_cram.c \
	dm_capa.c \
	dm_config.c \
	dm_debug.c \
	dm_list.c \
	dm_db.c \
	dm_sievescript.c \
	dm_acl.c \
	dm_misc.c \
	dm_pidfile.c \
	dm_digest.c \
	dm_match.c \
	dm_iconv.c \
	dm_dsn.c \
	dm_sset.c \
	dm_string.c \
	$(top_srcdir)/src/mpool/mpool.c \
	dm_mempool.c $(DM_GETOPT)
	

SERVER = server.c \
	clientsession.c \
	clientbase.c \
	dm_tls.c \
	dm_http.c \
	dm_request.c \
	dm_cidr.c

IMAPD = dm_quota.c \
	imap4.c \
	imapcommands.c \
	dm_imapsession.c

AM_CPPFLAGS = -I$(top_srcdir) -DSYSCONFDIR='"$(sysconfdir)"' -DLIBDIR='"$(libdir)"' -DLOCALSTATEDIR='"$(localstatedir)"'
AM_CFLAGS = @SIEVEINC@ @LDAPINC@
AM_LDFLAGS = -export-dynamic @CRYPTLIB@

if SHARED
MODULES = authmodule.c sortmodule.c
STATIC_MODULES =
else
MODULES =
STATIC_MODULES = @SORTLTLIB@ @AUTHLTLIB@
endif

dbmail_deliver_SOURCES = main.c
dbmail_deliver_LDADD = $(STATIC_MODULES) libdbmail.la

dbmail_pop3d_SOURCES = pop3.c pop3d.c
dbmail_pop3d_LDADD = $(STATIC_MODULES) libdbmail.la

dbmail_imapd_SOURCES = $(IMAPD) imapd.c 
dbmail_imapd_LDADD = $(STATIC_MODULES) libdbmail.la

dbmail_util_SOURCES = maintenance.c
dbmail_util_LDADD = $(STATIC_MODULES) libdbmail.la

dbmail_users_SOURCES = user.c
dbmail_users_LDADD = $(STATIC_MODULES) libdbmail.la

dbmail_export_SOURCES = $(IMAPD) export.c
dbmail_export_LDADD = $(STATIC_MODULES) libdbmail.la

dbmail_lmtpd_SOURCES = lmtp.c lmtpd.c
dbmail_lmtpd_LDADD = $(STATIC_MODULES) libdbmail.la
 
dbmail_httpd_SOURCES = dm_http.c httpd.c
dbmail_httpd_LDADD = $(STATIC_MODULES) libdbmail.la
  
if SIEVE
dbmail_sievecmd_SOURCES = sievecmd.c 
dbmail_sievecmd_LDADD = $(STATIC_MODULES) libdbmail.la

dbmail_timsieved_SOURCES = timsieve.c timsieved.c
dbmail_timsieved_LDADD = $(STATIC_MODULES) libdbmail.la
endif

if SHARED
pkglib_LTLIBRARIES = libdbmail.la
else
noinst_LTLIBRARIES = libdbmail.la
endif

libdbmail_la_SOURCES = $(COMMON) $(SERVER) $(MODULES)
libdbmail_la_LIBADD = $(STATIC_MODULES)
libdbmail_la_LDFLAGS = $(AM_LDFLAGS) @SOCKETLIB@
libdbmail_la_CFLAGS = $(AM_CFLAGS)


