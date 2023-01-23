#!/usr/bin/python
# -*- coding: utf-8 -*-

# Copyright (C) 2013 Paul J Stevens paul at nfg dot nl
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
#
# mimic long-polling imap client


import pdb
import time
import imaplib
import string
from email.MIMEText import MIMEText
from email.MIMEMultipart import MIMEMultipart

# for network testing
HOST, PORT = "localhost", 10143


TESTMSG = {}


def getMessageStrict():
    m = MIMEText("""
    this is a test message
    """)
    m.add_header("To", """"test user" <testuser@foo.org>""")
    m.add_header("From", """"somewhere.foo" <somewher@foo.org>""")
    m.add_header("CC", """"somewher@foo.org", "other@bar.org" """)
    m.add_header("In-Reply-To",
                 """"Message from "Test User" <testuser@test.org> """
                 """of "Sat, 14 Dec 2002 09:17:00 CST." """)
    m.add_header("Date", "Mon, 26 Sep 2005 13:26:39 +0200")
    m.add_header("Subject", """dbmail "test" message""")
    m.add_header("Message-Id", """<"114.5862946l.21522l.0l"@localhost>""")
    return m


def getMultiPart():
    m = MIMEMultipart()
    m.attach(getMessageStrict())
    m.add_header("To", "testaddr@bar.org")
    m.add_header("From", "testuser@foo.org")
    m.add_header("Date", "Sun, 25 Sep 2005 13:26:39 +0200")
    m.add_header("Subject", "dbmail multipart message")
    return m

TESTMSG['strict822'] = getMessageStrict()
TESTMSG['multipart'] = getMultiPart()


def getsock():
    return imaplib.IMAP4(HOST, PORT)


def strip_crlf(s):
    return string.replace(s, '\r', '')


class LongRunningImap(object):
    """
x login testuser1 test
A001 SELECT "INBOX"
A002 NOOP
A105 STATUS INBOX (MESSAGES)
A152 NOOP
A153 SEARCH NOT Deleted
A154 FETCH 1 (FLAGS BODY[HEADER])
A155 FETCH 1 BODY[1.MIME]
A156 FETCH 1 (BODY[1])
A157 FETCH 1 BODY[2.MIME]
A158 NOOP
A159 STORE 1 +FLAGS.SILENT (\Deleted)
A164 EXPUNGE
A702 NOOP
A703 SEARCH NOT Deleted
A704 EXPUNGE
A702 NOOP
A703 SEARCH NOT Deleted
A704 EXPUNGE
A702 NOOP
A703 SEARCH NOT Deleted
A704 EXPUNGE
A702 NOOP
A703 SEARCH NOT Deleted
A704 EXPUNGE
A702 NOOP
A703 SEARCH NOT Deleted
A704 EXPUNGE
x logout
"""
    def __init__(self):
        self.imap = getsock()
        self.login()
        self.start_loop()

    def login(self):
        self.imap.login("testuser1", "test")

    def exists(self):
        return int(self.imap.untagged_responses.get('EXISTS', ['0'])[-1])

    def start_loop(self):
        self.imap.select("INBOX")
        self.noop_status_loop()
        ids = self.noop_search()
        self.fetch_message(ids)
        while 1:
            ids = self.noop_search()
            self.fetch_message(ids)
            time.sleep(5)
        self.imap.logout()

    def noop_status_loop(self):
        """ wait for new messages """
        exists = self.exists()
        while 1:
            for counter in range(0, 25):
                self.imap.noop()
                if self.exists() > exists:
                    return
                time.sleep(5)
            result = self.imap.status("INBOX", "(MESSAGES)")
            count = int(result[1][0].split()[-1][:-1])
            if count > exists:
                return
            time.sleep(5)

    def noop_search(self):
        """ check for messages """
        self.imap.noop()
        res = self.imap.search("NOT Deleted")
        ids = [int(x) for x in res[1][0].split()]
        ids.sort()
        ids.reverse()
        self.imap.expunge()
        return ids

    def fetch_message(self, ids):
        """ dump and delete messages """

        def is_empty(res):
            payload = res[1][0]
            return isinstance(payload, str)

        try:
            for id in ids:
                self.imap.fetch("%d" % id, "(FLAGS BODY[HEADER])")
                part = 1
                while 1:
                    res = self.imap.fetch("%d" % id, "(BODY[%d.MIME])" % part)
                    if is_empty(res):
                        break
                    res = self.imap.fetch("%d" % id, "(BODY[%d])" % part)
                    if is_empty(res):
                        break
                    part += 1

                self.imap.noop()
                self.imap.store("%d" % id, "+FLAGS.SILENT", "(\Deleted)")
                self.imap.expunge()
                print "deleted message", id
        except:
            pass


if __name__ == '__main__':
    LongRunningImap()
