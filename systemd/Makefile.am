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


EXTRA_DIST = dbmail-imapd.service.in  dbmail-lmtpd.service.in  dbmail-pop3d.service.in  dbmail-timsieved.service.in
SYSTEMD_UNIT_DIR = /usr/lib/systemd/system
SYSTEMD_TMPFILES_DIR = /usr/lib/tmpfiles.d
PID_DIR=/run/dbmail

if SYSTEMD
install-systemd: dbmail-imapd.service dbmail-lmtpd.service dbmail-pop3d.service dbmail-timsieved.service dbmail.tmpfiles
	$(MKDIR_P) $(DESTDIR)$(SYSTEMD_UNIT_DIR)
	$(INSTALL_DATA) dbmail-imapd.service \
	  $(DESTDIR)$(SYSTEMD_UNIT_DIR)/dbmail-imapd.service
	$(INSTALL_DATA) dbmail-lmtpd.service \
	  $(DESTDIR)$(SYSTEMD_UNIT_DIR)/dbmail-lmtpd.service
	$(INSTALL_DATA) dbmail-pop3d.service \
	  $(DESTDIR)$(SYSTEMD_UNIT_DIR)/dbmail-pop3d.service
	$(INSTALL_DATA) dbmail-timsieved.service \
	  $(DESTDIR)$(SYSTEMD_UNIT_DIR)/dbmail-timsieved.service
	$(MKDIR_P) $(DESTDIR)$(SYSTEMD_TMPFILES_DIR)
	$(INSTALL_DATA) dbmail.tmpfiles \
	  $(DESTDIR)$(SYSTEMD_TMPFILES_DIR)/dbmail.conf

uninstall-systemd:
	rm -f $(DESTDIR)$(SYSTEMD_UNIT_DIR)/dbmail-imapd.service
	rm -f $(DESTDIR)$(SYSTEMD_UNIT_DIR)/dbmail-lmtpd.service
	rm -f $(DESTDIR)$(SYSTEMD_UNIT_DIR)/dbmail-pop3d.service
	rm -f $(DESTDIR)$(SYSTEMD_UNIT_DIR)/dbmail-timsieved.service
	rm -f $(DESTDIR)$(SYSTEMD_TMPFILES_DIR)/dbmail.tmpfiles
	rmdir $(DESTDIR)$(SYSTEMD_UNIT_DIR) ||:

SUFFIXES=.service.in .service .tmpfiles.in .tmpfiles

.service.in.service:
	$(AM_V_GEN)sed						\
	    -e 's|[@]sbindir[@]|$(sbindir)|g'			\
	    < $< > $@-t &&					\
	    mv $@-t $@

.tmpfiles.in.tmpfiles:
	$(AM_V_GEN)sed						\
	    -e 's|[@]piddir[@]|$(PID_DIR)|g'		\
	    < $< > $@-t &&					\
	    mv $@-t $@

else
install-systemd:
uninstall-systemd:
endif

install-data-local: install-systemd
uninstall-local: uninstall-systemd
