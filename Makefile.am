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

SUBDIRS = mysql pgsql auth sort man lib

if SIEVE
SIEVEPROGS = dbmail-sievecmd dbmail-timsieved
endif

bin_PROGRAMS = $(SIEVEPROGS)
sbin_PROGRAMS = dbmail-smtp dbmail-pop3d dbmail-imapd dbmail-util dbmail-users dbmail-lmtpd 

COMMON = config.c debug.c list.c dbmd5.c md5.c db.c misc.c mime.c pidfile.c dbmail-message.c
SERVER = server.c serverchild.c pool.c
DELIVER = header.c pipe.c forward.c dsn.c

# CFLAGS
AM_CFLAGS = -fomit-frame-pointer
INCLUDES = -I$(top_srcdir)

dbmail_smtp_SOURCES = main.c
dbmail_smtp_LDADD = libdbmail.la @SQLLTLIB@ @SORTLTLIB@ @AUTHLTLIB@ @INIPARSERLTLIB@ @SQLLIB@ @SIEVELIB@


dbmail_pop3d_SOURCES = pop3.c pop3d.c
dbmail_pop3d_LDADD = libdbmail.la @SQLLTLIB@ @SORTLTLIB@ @AUTHLTLIB@ @INIPARSERLTLIB@ @SQLLIB@ @LDAPLIB@ @SIEVELIB@

dbmail_imapd_SOURCES = quota.c imap4.c imaputil.c imapcommands.c memblock.c rfcmsg.c imapd.c dbsearch.c dbmsgbuf.c acl.c dbmail-imapsession.c
dbmail_imapd_LDADD = libdbmail.la @SQLLTLIB@ @SORTLTLIB@ @AUTHLTLIB@ @INIPARSERLTLIB@ @SQLLIB@ @LDAPLIB@

dbmail_util_SOURCES = maintenance.c
dbmail_util_LDADD = libdbmail.la @SQLLTLIB@ @SORTLTLIB@ @AUTHLTLIB@ @INIPARSERLTLIB@ @SQLLIB@ @LDAPLIB@ @SIEVELIB@

dbmail_users_SOURCES = user.c
dbmail_users_LDADD = libdbmail.la @SQLLTLIB@ @SORTLTLIB@ @AUTHLTLIB@ @INIPARSERLTLIB@ @SQLLIB@ @LDAPLIB@ @SIEVELIB@

dbmail_lmtpd_SOURCES = lmtp.c lmtpd.c
dbmail_lmtpd_LDADD = libdbmail.la @SQLLTLIB@ @SORTLTLIB@ @AUTHLTLIB@ @INIPARSERLTLIB@ @SQLLIB@ @LDAPLIB@ @SIEVELIB@
  
if SIEVE
dbmail_sievecmd_SOURCES = sievecmd.c 
dbmail_sievecmd_LDADD = libdbmail.la @SQLLTLIB@ @SORTLTLIB@ @AUTHLTLIB@ @INIPARSERLTLIB@ @SQLLIB@ @LDAPLIB@ @SIEVELIB@

dbmail_timsieved_SOURCES = timsieve.c timsieved.c
dbmail_timsieved_LDADD = libdbmail.la @SQLLTLIB@ @SORTLTLIB@ @AUTHLTLIB@ @INIPARSERLTLIB@ @SQLLIB@ @LDAPLIB@ @SIEVELIB@
endif

lib_LTLIBRARIES = libdbmail.la
libdbmail_la_SOURCES = $(COMMON) $(SERVER) $(DELIVER)
libdbmail_la_LIBADD = @SQLLTLIB@ @SORTLTLIB@ @AUTHLTLIB@ @INIPARSERLTLIB@ @SQLLIB@ @LDAPLIB@ @SIEVELIB@
