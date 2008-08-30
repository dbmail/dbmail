# Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
# Copyright (c) 2004-2006 NFG Net Facilities Group BV support@nfg.nl
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
	dbmail-lmtpd $(SIEVEPROGS)

COMMON = dbmail-user.c \
	dbmail-message.c \
	dbmail-mailbox.c \
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
	dm_dsn.c $(DM_GETOPT)
	
SERVER = server.c \
	clientsession.c \
	dm_cidr.c

MODULES = authmodule.c sortmodule.c

IMAPD = dm_quota.c \
	dm_memblock.c \
	dm_cache.c \
	imap4.c \
	imapcommands.c \
	dbmail-imapsession.c

AM_CFLAGS = @SIEVEINC@ @LDAPINC@

INCLUDES = -I$(top_srcdir)


dbmail_deliver_SOURCES = main.c
dbmail_deliver_LDADD = libdbmail.la

dbmail_pop3d_SOURCES = pop3.c pop3d.c
dbmail_pop3d_LDADD = libdbmail.la

dbmail_imapd_SOURCES = $(IMAPD) imapd.c 
dbmail_imapd_LDADD = libdbmail.la

#dbmail_top_SOURCES = top.c
#dbmail_top_LDADD = libdbmail.la -lncurses

dbmail_util_SOURCES = maintenance.c
dbmail_util_LDADD = libdbmail.la

dbmail_users_SOURCES = user.c
dbmail_users_LDADD = libdbmail.la

dbmail_export_SOURCES = $(IMAPD) export.c
dbmail_export_LDADD = libdbmail.la

dbmail_lmtpd_SOURCES = lmtp.c lmtpd.c
dbmail_lmtpd_LDADD = libdbmail.la
  
if SIEVE
dbmail_sievecmd_SOURCES = sievecmd.c 
dbmail_sievecmd_LDADD = libdbmail.la

dbmail_timsieved_SOURCES = timsieve.c timsieved.c
dbmail_timsieved_LDADD = libdbmail.la
endif


pkglib_LTLIBRARIES = libdbmail.la


if SHARED
AM_LDFLAGS = -export-dynamic @CRYPTLIB@
libdbmail_la_SOURCES = $(COMMON) $(SERVER) $(MODULES)
else
libdbmail_la_SOURCES = $(COMMON) $(SERVER)
libdbmail_la_LIBADD = @SORTLTLIB@ @AUTHLTLIB@
endif

libdbmail_la_LDFLAGS = $(AM_LDFLAGS) @SOCKETLIB@
libdbmail_la_CFLAGS = $(AM_CFLAGS)


