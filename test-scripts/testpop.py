#!/usr/bin/python

# Copyright (C) 2004 Paul J Stevens paul at nfg dot nl
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
#

# For a protocol trace set to 4
DEBUG = 0
#DEBUG = 4

# select 'stream' for non-forking mode
#TYPE = 'stream'
# select 'network' for forking mode
TYPE = 'network'


import unittest
import poplib
import string
from email.MIMEText import MIMEText
from email.MIMEMultipart import MIMEMultipart
from email import message_from_string

unimplementedError = 'Dbmail testcase unimplemented'

# for network testing
HOST, PORT = "localhost", 10110

# for stdin/stdout testing
DAEMONBIN = "./dbmail-pop3d -n -f /etc/dbmail/dbmail-test.conf"
# with valgrind
#DAEMONBIN = "valgrind --leak-check=full ./dbmail-imapd -n /etc/dbmail/dbmail-test.conf"


TESTMSG = {}


def getMessageStrict():
    m = MIMEText("""
    this is a test message
    """)
    m.add_header("To", "testuser@foo.org")
    m.add_header("From", "somewher@foo.org")
    m.add_header("Date", "Mon, 26 Sep 2005 13:26:39 +0200")
    m.add_header("Subject", "dbmail test message")
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
    if TYPE == 'network':
        return poplib.POP3(HOST, PORT)
    elif TYPE == 'stream':
        return poplib.POP3_stream(DAEMONBIN)


def strip_crlf(s):
    return string.replace(s, '\r', '')


class testPopServer(unittest.TestCase):
    def setUp(self, username="testuser1", password="test"):
        self.o = getsock()
        self.o.set_debuglevel(DEBUG)
        self.o.user(username)
        return self.o.pass_(password)

    def test_getwelcome(self):
        """
        `getwelcome()'
             Returns the greeting string sent by the POP3 server.
        """
        self.assertEquals(self.o.getwelcome()[:4], '+OK ')

    def test_user(self):
        """
        `user(username)'
             Send user command, response should indicate that a password is
             required.
        """
        o = getsock()
        o.set_debuglevel(DEBUG)
        self.assertEquals(o.user('foobar')[:12], "+OK Password")

    def test_pass(self):
        """
        `pass_(password)'
             Send password, response includes message count and mailbox size.
             Note: the mailbox on the server is locked until `quit()' is
             called.
        """
        o = getsock()
        o.set_debuglevel(DEBUG)
        o.user('testuser2')
        self.assertEquals(o.pass_('test')[:13], "+OK testuser2")

    def test_apop(self):
        """
        `apop(user, secret)'
             Use the more secure APOP authentication to log into the POP3
             server.
        """

    def test_rpop(self):
        """
        `rpop(user)'
             Use RPOP authentication (similar to UNIX r-commands) to log into
             POP3 server.

        """

    def test_stat(self):
        """
        `stat()'
             Get mailbox status.  The result is a tuple of 2 integers:
             `(MESSAGE COUNT, MAILBOX SIZE)'.

        """
        tpl = self.o.stat()
        self.assertEquals(len(tpl), 2)
        self.assertEquals(type(tpl[0]), type(123))

    def test_list(self):
        """
        `list([which])'
             Request message list, result is in the form `(RESPONSE, ['mesg_num
             octets', ...])'.  If WHICH is set, it is the message to list.
        """
        l = self.o.list()
        self.assertEquals(l[0][:3], "+OK")
        count = string.split(l[0])[1]
        n, s = string.split(l[1][int(count) - 1])
        self.assertEquals(self.o.list(n), "+OK %s %s" % (n, s))

    def test_retr(self):
        """
        `retr(which)'
             Retrieve whole message number WHICH, and set its seen flag.
             Result is in form  `(RESPONSE, ['line', ...], OCTETS)'.
        """
        l = self.o.list()
        count = string.split(l[0])[1]
        n, s = string.split(l[1][int(count) - 1])
        for i in range(1, int(n)):
            result = self.o.retr(i)
            r = string.split(result[0])
            self.assertEquals(r[0], "+OK")
            message = '\r\n'.join(result[1])
            expectedlen = len(message)
            bytestuffed = [x for x in result[1] if x.startswith('.')]
            expectedlen = expectedlen + len(bytestuffed)
            self.assertEquals(int(r[1]),
                              expectedlen,
                              "[%s] msgid %d octets %s message %d" % (
                                  result[1], i, r[1], expectedlen))

    def test_dele(self):
        """
        `dele(which)'
             Flag message number WHICH for deletion.  On most servers deletions
             are not actually performed until QUIT (the major exception is
             Eudora QPOP, which deliberately violates the RFCs by doing pending
             deletes on any disconnect).

        """
        self.assertEquals(self.o.dele(1), '+OK message 1 deleted')
        self.o.rset()

    def test_rset(self):
        """
        `rset()'
             Remove any deletion marks for the mailbox.
        """
        result = self.o.rset()
        self.assertEquals(result[:4], '+OK ')

    def test_noop(self):
        """
        `noop()'
             Do nothing.  Might be used as a keep-alive.
        """
        self.assertEquals(self.o.noop()[:3], '+OK')

    def test_quit(self):
        """
        `quit()'
             Signoff:  commit changes, unlock mailbox, drop connection.
        """

    def test_top(self):
        """
        `top(which, howmuch)'
             Retrieves the message header plus HOWMUCH lines of the message
             after the header of message number WHICH. Result is in form
             `(RESPONSE, ['line', ...], OCTETS)'.

             The POP3 TOP command this method uses, unlike the RETR command,
             doesn't set the message's seen flag; unfortunately, TOP is poorly
             specified in the RFCs and is frequently broken in off-brand
             servers.  Test this method by hand against the POP3 servers you
             will use before trusting it.
        """
        list = self.o.list()
        id, size = [x for x in [
            (int(x[0]), int(x[1])) for x in [
                x.split(' ') for x in list[1]]] if x[1] > 1000][0]

        result = self.o.top(id, 10)
        self.assertEquals(result[0], '+OK 10 lines of message %d' % id)
        result = self.o.top(id, 0)
        msg = message_from_string('\r\n'.join(result[1]))
        self.assertEquals(msg.get_payload(), "")

        result = self.o.top(id, 2)
        msg = message_from_string('\r\n'.join(result[1]))
        self.assertEquals(len(msg.get_payload().split('\r\n')), 3)

    def test_uidl(self):
        """
        `uidl([which])'
             Return message digest (unique id) list.  If WHICH is specified,
             result contains the unique id for that message in the form
             `'RESPONSE MESGNUM UID', otherwise result is list `(RESPONSE,
             ['mesgnum uid', ...], OCTETS)'.
        """
        all = self.o.uidl()
        self.assertEquals(all[0][:4], '+OK ')
        one = self.o.uidl(1)
        self.assertEquals(one[4:], all[1][0])

    def tearDown(self):
        self.o.rset()
        self.o.quit()


def usage():
    print """testpop.py:   test dbmail imapserver
    """


if __name__ == '__main__':
    unittest.main()

#EOF
