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

SUBDIRS = modules man

if SIEVE
SIEVEPROGS = dbmail-sievecmd dbmail-timsieved
endif

bin_PROGRAMS = $(SIEVEPROGS)

sbin_PROGRAMS = dbmail-smtp \
	dbmail-pop3d \
	dbmail-imapd \
	dbmail-util \
	dbmail-users \
	dbmail-export \
	dbmail-lmtpd 

COMMON = dbmail-user.c \
	dbmail-message.c \
	dbmail-mailbox.c \
	dm_search.c \
	config.c \
	debug.c \
	list.c \
	dbmd5.c \
	md5.c \
	db.c \
	misc.c \
	mime.c \
	pidfile.c 
	
SERVER = server.c \
	serverchild.c \
	pool.c \
	dm_cidr.c
	
DELIVER = pipe.c \
	forward.c \
	dsn.c \
	sort.c

if SHARED
MODULES = dbmodule.c \
	authmodule.c \
	sortmodule.c

AM_LDFLAGS = -Wl,--export-dynamic
endif

# CFLAGS
#AM_CFLAGS = -fomit-frame-pointer

INCLUDES = -I$(top_srcdir)


dbmail_smtp_SOURCES = main.c
dbmail_smtp_LDADD = libdbmail.la

dbmail_pop3d_SOURCES = pop3.c pop3d.c
dbmail_pop3d_LDADD = libdbmail.la

dbmail_imapd_SOURCES = quota.c imap4.c dm_imaputil.c imapcommands.c memblock.c imapd.c acl.c dbmail-imapsession.c
dbmail_imapd_LDADD = libdbmail.la

dbmail_util_SOURCES = maintenance.c
dbmail_util_LDADD = libdbmail.la

dbmail_users_SOURCES = user.c
dbmail_users_LDADD = libdbmail.la

dbmail_export_SOURCES = export.c
dbmail_export_LDADD = libdbmail.la

dbmail_lmtpd_SOURCES = lmtp.c lmtpd.c
dbmail_lmtpd_LDADD = libdbmail.la
  
if SIEVE
dbmail_sievecmd_SOURCES = sievecmd.c 
dbmail_sievecmd_LDADD = libdbmail.la

dbmail_timsieved_SOURCES = timsieve.c timsieved.c
dbmail_timsieved_LDADD = libdbmail.la
endif


lib_LTLIBRARIES = libdbmail.la

libdbmail_la_SOURCES = $(COMMON) $(SERVER) $(DELIVER) $(MODULES)
libdbmail_la_LIBADD = @MYSQLLTLIB@ @PGSQLLTLIB@ @SQLITELTLIB@ @SORTLTLIB@ @AUTHLTLIB@ @INIPARSERLTLIB@
libdbmail_la_LDFLAGS = $(AM_LDFLAGS)
libdbmail_la_CFLAGS = $(AM_CFLAGS)


if WITHCHECK

TESTS=check_dbmail_common \
	check_dbmail_server \
	check_dbmail_deliver \
	check_dbmail_imapd \
	check_dbmail_message \
	check_dbmail_mailbox \
	check_dbmail_user \
	check_dbmail_util

noinst_PROGRAMS=$(TESTS)

check_dbmail_user_SOURCES=check_dbmail_user.c
check_dbmail_user_LDADD=libdbmail.la @CHECK_LIBS@
check_dbmail_user_INCLUDES=@CHECK_CFLAGS@

check_dbmail_util_SOURCES=check_dbmail_util.c
check_dbmail_util_LDADD=libdbmail.la @CHECK_LIBS@
check_dbmail_util_INCLUDES=@CHECK_CFLAGS@

check_dbmail_common_SOURCES=check_dbmail_common.c
check_dbmail_common_LDADD=libdbmail.la @CHECK_LIBS@
check_dbmail_common_INCLUDES=@CHECK_CFLAGS@

check_dbmail_server_SOURCES=check_dbmail_server.c
check_dbmail_server_LDADD=libdbmail.la @CHECK_LIBS@
check_dbmail_server_INCLUDES=@CHECK_CFLAGS@

check_dbmail_deliver_SOURCES=check_dbmail_deliver.c
check_dbmail_deliver_LDADD=libdbmail.la @CHECK_LIBS@
check_dbmail_deliver_INCLUDES=@CHECK_CFLAGS@

check_dbmail_message_SOURCES=$(COMMON) $(SERVER) $(DELIVER) check_dbmail_message.c
check_dbmail_message_LDADD=libdbmail.la @CHECK_LIBS@
check_dbmail_message_INCLUDES=@CHECK_CFLAGS@

check_dbmail_mailbox_SOURCES=check_dbmail_mailbox.c
check_dbmail_mailbox_LDADD=libdbmail.la @CHECK_LIBS@
check_dbmail_mailbox_INCLUDES=@CHECK_CFLAGS@

check_dbmail_imapd_SOURCES=quota.c imap4.c dm_imaputil.c imapcommands.c memblock.c dm_search.c \
	acl.c dbmail-imapsession.c check_dbmail_imapd.c
check_dbmail_imapd_LDADD=libdbmail.la @CHECK_LIBS@
check_dbmail_imapd_INCLUDES=@CHECK_CFLAGS@

endif
