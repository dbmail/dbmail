# Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl
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

SUBDIRS = mysql pgsql auth

bin_PROGRAMS = dbmail-smtp dbmail-pop3d dbmail-imapd dbmail-maintenance dbmail-adduser dbmail-readvut
bin_SCRIPTS = install-dbmail.sh

dbmail_smtp_SOURCES = config.c list.c debug.c dbmd5.c md5.c mime.c main.c pipe.c bounce.c forward.c memblock.c misc.c db.c
dbmail_smtp_LDADD = @SQLALIB@ $(top-builddir)auth/libauthdbmail.a
dbmail_smtp_LDFLAGS = @SQLLIB@ @LDAPLIB@

dbmail_pop3d_SOURCES = config.c server.c serverchild.c pop3.c list.c debug.c dbmd5.c md5.c mime.c misc.c memblock.c pop3d.c db.c
dbmail_pop3d_LDADD = @SQLALIB@ $(top-builddir)auth/libauthdbmail.a
dbmail_pop3d_LDFLAGS = @SQLLIB@ @LDAPLIB@

dbmail_imapd_SOURCES = acl.c quota.c config.c server.c serverchild.c imap4.c debug.c list.c dbmd5.c md5.c imaputil.c imapcommands.c mime.c misc.c memblock.c rfcmsg.c imapd.c db.c dbsearch.c dbmsgbuf.c 1
dbmail_imapd_LDADD = @SQLALIB@ $(top-builddir)auth/libauthdbmail.a
dbmail_imapd_LDFLAGS = @SQLLIB@ @LDAPLIB@

dbmail_maintenance_SOURCES = config.c debug.c list.c dbmd5.c md5.c mime.c memblock.c maintenance.c auth.h db.c misc.c 
dbmail_maintenance_LDADD = @SQLALIB@ $(top-builddir)auth/libauthdbmail.a
dbmail_maintenance_LDFLAGS = @SQLLIB@ @LDAPLIB@

dbmail_adduser_SOURCES = config.c debug.c list.c dbmd5.c md5.c mime.c memblock.c user.c db.c misc.c 
dbmail_adduser_LDADD = @SQLALIB@ $(top-builddir)auth/libauthdbmail.a
dbmail_adduser_LDFLAGS = @SQLLIB@ @LDAPLIB@

dbmail_readvut_SOURCES = config.c debug.c list.c dbmd5.c md5.c mime.c vut2dbmail.c db.c misc.c 
dbmail_readvut_LDADD = @SQLALIB@ $(top-builddir)auth/libauthdbmail.a
dbmail_readvut_LDFLAGS = @SQLLIB@ @LDAPLIB@
