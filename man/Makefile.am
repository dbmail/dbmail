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

man1_MANS = dbmail.1 dbmail-smtp.1

man5_MANS = dbmail.conf.5

man8_MANS = dbmail-util.8 \
	dbmail-users.8 \
	dbmail-export.8 \
	dbmail-sievecmd.8 \
	dbmail-imapd.8 \
	dbmail-lmtpd.8 \
	dbmail-pop3d.8 \
	dbmail-timsieved.8

EXTRA_DIST = $(man1_MANS) $(man5_MANS) $(man8_MANS)

%.xml : %.txt
	asciidoc --unsafe -b docbook -d manpage $<

%.1 : %.xml
	xmlto man $< 2>/dev/null
	./fixsp.pl $@

%.5 : %.xml
	xmlto man $< 2>/dev/null
	./fixsp.pl $@

%.8 : %.xml
	xmlto man $< 2>/dev/null
	./fixsp.pl $@

clean:
	rm -f *1 *5 *8 *xml
