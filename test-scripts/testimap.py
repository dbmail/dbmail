#!/usr/bin/python
# -*- coding: utf-8 -*-

# Copyright (C) 2004-2014 Paul J Stevens paul at nfg dot nl
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

DEBUG = 0

# For a protocol trace set to 4
#DEBUG = 4

# select 'stream' for inetd mode
TYPE = 'stream'
# select 'network' for networking mode
TYPE = 'network'


import unittest
import imaplib
import re
import commands
import traceback
import string
from email.MIMEText import MIMEText
from email.MIMEMultipart import MIMEMultipart
from pyparsing import nestedExpr

unimplementedError = 'Dbmail testcase unimplemented'

# for network testing
HOST, PORT = "localhost", 10143

# for stdin/stdout testing
DAEMONBIN = "./src/dbmail-imapd -n -f /etc/dbmail/dbmail-test.conf"
# with valgrind
#DAEMONBIN = "CK_FORK=no G_SLICE=always-malloc valgrind " \
#        "--suppressions=./contrib/dbmail.supp " \
#        "--leak-check=full %s" % DAEMONBIN


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
    if TYPE == 'network':
        return imaplib.IMAP4(HOST, PORT)
    elif TYPE == 'stream':
        return imaplib.IMAP4_stream(DAEMONBIN)


def strip_crlf(s):
    return string.replace(s, '\r', '')


def getFreshbox(name):
    assert(name)
    cmd = './contrib/mailbox2dbmail/mailbox2dbmail ' \
            '-u testuser1 -t mbox -m test-scripts/testbox ' \
            '-b "%s" -p ./src/dbmail-deliver ' \
            '-f /etc/dbmail/dbmail.conf' % name
    s, o = commands.getstatusoutput(cmd)
    if s:
        raise Exception(o)


def parse_parenthesized_list_internal(struct):
    """Internal parsing function for parenthesized lists"""
    # do we have an internal structure?
    if not isinstance(struct, str):
        # we need to parse an internal structure
        # loop through each element, and create a dictionary from them
        params = {}
        actual_elem = None
        i = 0
        for elem in struct:
            # odd elements are keys
            if i % 2 == 0:
                actual_elem = elem
            # even elements are values
            else:
                # call ourselves recursively
                params[actual_elem] = parse_parenthesized_list_internal(elem)
            i += 1
        return params
    else:
        return struct


def parse_parenthesized_list(s):
    """Parse a parethesized list

    Test me with:
    print parse_parenthesized_list('(A (C 1 D 2) B 5)')
    print parse_parenthesized_list('(A 2 B 5)')
    """
    # first get the string into an iterable structure
    struct = nestedExpr().parseString(s)
    # remove the unnecessary top-level structure
    struct = struct[0]
    # then call internal parsing for it
    return parse_parenthesized_list_internal(struct)


class testImapServer(unittest.TestCase):
    maxDiff = None

    def setUp(self, username="testuser1", password="test"):
        self.o = getsock()
        self.o.debug = DEBUG
        self.o.login(username, password)

        self.p = getsock()
        self.p.debug = DEBUG
        return self.p.login(username, password)

    def testAppend(self):
        """
        'append(mailbox, flags, date_time, message)'
             Append message to named mailbox.
        """
        # check for OK
        self.assertEquals(
            self.o.append('INBOX', (), "",
                          str(TESTMSG['strict822']))[0], 'OK')
        # delete remembered response code
        self.o.response('APPENDUID')
        # check for TRYCREATE
        result = self.o.append('nosuchbox', (), "",
                             str(TESTMSG['strict822']))
        self.assertEquals(result[0], 'NO')
        self.assertEquals(result[1][0][:11], '[TRYCREATE]')

        # test appending with flags
        # start with creating a test folder for appending
        self.o.create('testappend')

        # retrieve uidvalidity and uidnext for the new folder
        #  (will be needed for checking response code)
        result = self.o.status("testappend", '(UIDVALIDITY UIDNEXT)')
        response_data = result[1][0]
        # get the string containing response parameter values
        left_parenthesis = response_data.find("(")
        self.assertNotEqual(left_parenthesis, -1)
        # parse response parameter values
        responses = parse_parenthesized_list(response_data[left_parenthesis:])
        old_uid_validity = responses["UIDVALIDITY"]
        old_uid_next = responses["UIDNEXT"]

        # append the message
        self.o.append('testappend', '\Flagged Userflag',
                      "\" 3-Mar-2006 07:15:00 +0200 \"",
                      str(TESTMSG['strict822']))
        # check response code
        code_name, code_data = self.o.response('APPENDUID')
        # check the answer's format
        self.assertEqual(code_name, 'APPENDUID')
        uid_validity, uid_set = code_data[0].split(" ")
        self.assertRegexpMatches(uid_validity, "^\d+$")
        self.assertRegexpMatches(uid_set, "^\d+$")
        #The next line would be necessary in case of multiappend
        #self.assertRegexpMatches(uid_set, "^\d+(:\d+)?(,\d+(:\d+)?)*$") # based on the BNF in rfc4315
        # check whether the uid_validity and uid_next match
        self.assertEqual(uid_validity, old_uid_validity)
        self.assertLessEqual(int(old_uid_next), int(uid_set))

        result = self.o.select('testappend')
        id = result[1][0]
        self.assertEquals(int(id), 1)

        result = self.o.fetch(id, "(UID BODY[])")
        self.assertEquals(result[0], 'OK')

        result = self.o.fetch(id, "(UID BODY[TEXT])")
        self.assertEquals(result[0], 'OK')
        expectlen = int(string.split(result[1][0][0])[-1][1:-1])
        expectmsg = result[1][0][1]
        self.assertEquals(len(expectmsg), expectlen)
        self.assertEquals(strip_crlf(expectmsg),
                          TESTMSG['strict822'].get_payload())

        result = self.o.fetch(id, "(UID BODY.PEEK[TEXT])")
        self.assertEquals(result[0], 'OK')
        self.assertEquals(strip_crlf(result[1][0][1]),
                          TESTMSG['strict822'].get_payload())

        result = self.o.fetch(id, "(FLAGS)")
        expect = '1 (FLAGS (\\Seen \\Flagged \\Recent Userflag))'
        self.assertEquals(result[1][0], expect)

    def testCheck(self):
        """
        'check()'
            Checkpoint mailbox on server.
        """
        self.o.select('INBOX')
        self.assertEquals(self.o.check(),
                          ('OK', ['CHECK completed']))

    def testClose(self):
        """
        'close()'
            Close currently selected mailbox. Deleted messages are removed from
            writable mailbox. This is the recommended command before `LOGOUT'.
        """
        self.o.select('INBOX')
        self.assertEquals(self.o.close(),
                          ('OK', ['CLOSE completed']))

    def testCopy(self):
        """
        'copy(message_set, new_mailbox)'
            Copy MESSAGE_SET messages onto end of NEW_MAILBOX.
        """
        self.o.create('testcopy1')
        self.o.create('testcopy2')
        # insert some messages to the origin folder
        for i in range(1, 20):
            self.o.append('testcopy1', "", "",
                          str(TESTMSG['strict822']))

        # check what's the uid_next value and uidvalidity in the target folder
        self.o.select('testcopy2')
        responses = self.o.untagged_responses
        old_uid_validity = responses["UIDVALIDITY"][-1]
        old_uid_next = responses["UIDNEXT"][-1]

        # switch to origin folder
        self.o.select('testcopy1')
        # get the recent message's id
        id = self.o.recent()[1][0]
        # get the message's original uid
        result = self.o.fetch(id, "(UID)")
        response_data = result[1][0]
        uid = parse_parenthesized_list(
            response_data[response_data.find("("):])['UID']
        # copy the recent message from the origin folder
        result = self.o.copy(id, 'testcopy2')
        self.assertEquals(result[0], 'OK')

        # check copy's response code
        code_name, code_data = self.o.response('COPYUID')
        # check the response code's format
        self.assertEqual(code_name, 'COPYUID')
        uid_validity, orig_uids, new_uids = code_data[0].split(" ")
        self.assertRegexpMatches(uid_validity, "^\d+$")
        self.assertRegexpMatches(
            orig_uids, "^\d+$")  # we copied just one message
        self.assertRegexpMatches(
            new_uids, "^\d+$")  # we copied just one message
        # check whether response code elements match
        self.assertEqual(
            uid_validity, old_uid_validity)  # 'Uidvalidity matches'
        # 'Old uid returned by UID COPY matches the one
        # that we actually copied'
        self.assertEqual(orig_uids, uid)
        # 'The copied message\'s uid is higher than the last uid_next value'
        self.assertLessEqual(
            int(old_uid_next), int(new_uids))

        # check again what's the uid_next value and uidvalidity in the target
        # folder
        self.o.select('testcopy2')
        responses = self.o.untagged_responses
        old_uid_validity = responses["UIDVALIDITY"][-1]
        old_uid_next = responses["UIDNEXT"][-1]

        # switch to origin folder
        self.o.select('testcopy1')
        # get the uids of all messages
        result = self.o.fetch('1:*', "(UID)")
        uids = [
            parse_parenthesized_list(x[x.find("("):])['UID'] for x in result[1]
        ]
        # copy them
        result = self.o.copy('1:*', 'testcopy2')
        self.assertEquals(result[0], 'OK')

        # check copy's response code
        code_name, code_data = self.o.response('COPYUID')
        # check the response code's format
        self.assertEqual(code_name, 'COPYUID')
        uid_validity, orig_uids, new_uids = code_data[0].split(" ")
        self.assertRegexpMatches(uid_validity, "^\d+$")
        self.assertRegexpMatches(orig_uids, "^\d+(:\d+)?(,\d+(:\d+)?)*$")
        self.assertRegexpMatches(new_uids, "^\d+(:\d+)?(,\d+(:\d+)?)*$")
        orig_uids = orig_uids.split(',')
        new_uids = new_uids.split(',')
        # check whether response code elements match
        self.assertEqual(uid_validity, old_uid_validity)
        self.assertEqual(orig_uids.sort(), uids.sort())
        self.assertEqual(len(new_uids), len(orig_uids))
        for i in new_uids:
            # check whether all new uids are above the last seen uid_next value
            self.assertLessEqual(int(old_uid_next), int(i))

    def testCreate(self):
        """
        'create(mailbox)'
            Create new mailbox named MAILBOX.
        """
        self.assertEquals(self.o.create('test create'),
                          ('OK', ['CREATE completed']))
        self.o.create("dir1")
        result = self.o.list()[1]
        self.assertEquals(
            '(\\hasnochildren) "/" "test create"' in result, True)

    def testCreateListWithQuote(self):
        """
        Bug 314 -- Single Quote in Mailbox Name
        """
        self.assertEquals(self.o.create('Foo\'s Folder'),
                          ('OK', ['CREATE completed']))
        result = self.o.list()[1]
        self.assertEquals(
            '(\\hasnochildren) "/" "Foo\'s Folder"' in result, True)

    def testDelete(self):
        """
        'delete(mailbox)'
            Delete old mailbox named MAILBOX.
        """
        self.o.create('testdelete')
        self.assertEquals(self.o.delete('testdelete'),
                          ('OK', ['DELETE completed']))

    def testExpunge(self):
        """
        expunge()'
            Permanently remove deleted items from selected mailbox. Generates
            an `EXPUNGE' response for each deleted message. Returned data
            contains a list of `EXPUNGE' message numbers in order received.
        """

        self.o.create('testexpungebox')
        for i in range(0, 11):
            self.o.append('testexpungebox', '\Flagged Userflag',
                          "\" 3-Mar-2006 07:15:00 +0200 \"",
                          str(TESTMSG['strict822']))

        p = getsock()
        #p.debug = 4
        p.login('testuser1', 'test'), ('OK', ['LOGIN completed'])

        self.o.select('testexpungebox')
        p.select('testexpungebox')

        self.o.store('5:*', '+FLAGS', '\Deleted')
        msnlist = self.o.expunge()[1]
        self.assertEquals(msnlist, ['11', '10', '9', '8', '7', '6', '5'])
        self.assertEquals(self.o.fetch('4', '(BODYSTRUCTURE FLAGS)')[0], 'OK')
        self.assertEquals(self.o.fetch('4', '(BODYSTRUCTURE FLAGS)')[0], 'OK')
        self.assertEquals(p.fetch('11', '(Flags)')[0], 'OK')
        p.noop()
        #print p.untagged_responses
        self.o.debug = 0
        p.debug = 0

    def testFetch(self):
        """
        fetch(message_set, message_parts)'
            Fetch (parts of) messages.  MESSAGE_PARTS should be a string of
            message part names enclosed within parentheses, eg: `"(UID
            BODY[TEXT])"'.  Returned data are tuples of message part envelope
            and data.
        """
        m = TESTMSG['strict822']
        self.o.create('tmpbox')
        self.o.append('tmpbox', '', '', str(m))
        self.o.select('tmpbox')
        self.assertEquals(self.o.fetch("1:*", "(Flags)")[0], 'OK')
        id = 1

        # OE query
        result = self.o.fetch(
            id,
            "(BODY.PEEK[HEADER.FIELDS (References X-Ref X-Priority "
            "X-MSMail-Priority X-MSOESRec Newsgroups)] ENVELOPE "
            "RFC822.SIZE UID FLAGS INTERNALDATE)")
        self.assertEquals(len(result[1]), 5)
        expect = [(' (("somewhere.foo" NIL "somewher" "foo.org")) '
                   '(("somewhere.foo" NIL "somewher" "foo.org")) '
                   '(("somewhere.foo" NIL "somewher" "foo.org")) '
                   '(("test user" NIL "testuser" "foo.org")) '
                   '((NIL NIL "somewher" "foo.org")(NIL NIL "other" '
                   '"bar.org")) NIL {81}', '"Message from "Test User" '
                   '<testuser@test.org> of "Sat, 14 Dec 2002 09:17:00 '
                   'CST."'),
                  (' {36}', '<"114.5862946l.21522l.0l"@localhost>'),
                  (') BODY[HEADER.FIELDS (References X-Ref X-Priority '
                   'X-MSMail-Priority X-MSOESRec Newsgroups)] {2}',
                   '\r\n'), ')']
        self.assertEquals(result[1][1:], expect)
        self.assertEquals(result[0], 'OK')

        # fetch complete message. order and number of headers may differ
        result1 = self.o.fetch(id, "(UID BODY[])")
        result2 = self.o.fetch(id, "(UID RFC822)")
        self.assertEquals(result1[0], 'OK')
        self.assertEquals(result2[0], 'OK')
        self.assertEquals(result1[1][0][1], result2[1][0][1])

        # get the body. must equal input message's body
        result = self.o.fetch(id, "(UID BODY[TEXT])")
        bodytext = strip_crlf(result[1][0][1])
        self.assertEquals(bodytext, m.get_payload())

        result = self.o.fetch(id, "(UID BODYSTRUCTURE)")
        self.assertEquals(result[0], 'OK')

        result = self.o.fetch(id, "(ENVELOPE)")
        expect = [('1 (ENVELOPE ("Mon, 26 Sep 2005 13:26:39 +0200" '
                   '{21}', 'dbmail "test" message'),
                  (' (("somewhere.foo" NIL "somewher" "foo.org")) '
                   '(("somewhere.foo" NIL "somewher" "foo.org")) '
                   '(("somewhere.foo" NIL "somewher" "foo.org")) '
                   '(("test user" NIL "testuser" "foo.org")) '
                   '((NIL NIL "somewher" "foo.org")(NIL NIL "other" '
                   '"bar.org")) NIL {81}',
                   '"Message from "Test User" <testuser@test.org> '
                   'of "Sat, 14 Dec 2002 09:17:00 CST."'),
                  (' {36}', '<"114.5862946l.21522l.0l"@localhost>'), '))']
        self.assertEquals(result[0], 'OK')
        self.assertEquals(result[1], expect)

        result = self.o.fetch(id, "(UID BODY[TEXT]<0.20>)")
        self.assertEquals(result[0], 'OK')
        self.assertEquals(self.o.fetch(
            id, "(UID BODY.PEEK[TEXT]<0.30>)")[0], 'OK')
        self.assertEquals(self.o.fetch(
            id, "(UID RFC822.SIZE)")[0], 'OK')

        result = self.o.fetch(id, "(UID RFC822.HEADER)")
        self.assertEquals(result[0], 'OK')
        self.assertEquals(result[1][0][1][-2:], '\r\n')

        # TB query
        result = self.o.fetch(
            id, "(UID RFC822.SIZE FLAGS BODY.PEEK[HEADER.FIELDS "
            "(From To Cc Subject Date Message-ID Priority X-Priority "
            "References Newsgroups In-Reply-To Content-Type)])")
        self.assertEquals(result[0], 'OK')

        # test big folder full fetch
        self.o.select('INBOX')
        self.o.fetch("1:*", "(UID FULL)")

        self.assertEquals(self.o.fetch("1:*", "(Flags)")[0], 'OK')

        # bogus ranges
        self.failUnlessRaises(Exception, self.o.fetch, "-10:10", "(Flags)")
        self.failUnlessRaises(Exception, self.o.fetch, "10:-10", "(Flags)")

    def testGetacl(self):
        """
        `getacl(mailbox)'
            Get the `ACL's for MAILBOX.
        """
        self.assertEquals(
            self.o.getacl('INBOX'),
            ('OK', ['"INBOX" "testuser1" lrswipkxteacd']))

    def getQuota(self):
        """
        getquota(root)
            Get the `quota' ROOT's resource usage and limits.  This method is
            part of the IMAP4 QUOTA extension defined in rfc2087.
        """
        #self.fail(unimplementedError)

    def getQuotaroot(self):
        """
        getquotaroot(mailbox)
            Get the list of `quota' `roots' for the named MAILBOX.  This
            method is part of the IMAP4 QUOTA extension defined in rfc2087.
        """
        #self.fail(unimplementedError)

    def testList(self):
        """
        list([directory[, pattern]])
            List mailbox names in DIRECTORY matching PATTERN.  DIRECTORY
            defaults to the top-level mail folder, and PATTERN defaults to
            match anything.  Returned data contains a list of `LIST' responses.
        """
        dirlist = ['dir1', 'dir2', 'dir3', 'dir1/sub1',
                   'dir2/sub2', 'dir2/sub 2a', 'dir3/sub 3',
                   'dir3/sub 3/ssub1', 'dir3/sub 3/.ssub2']
        for d in dirlist:
            self.o.create(d)
        #print self.o.list()
        #print self.o.list("dir1/")
        #print self.o.list("dir2/")
        #print self.o.list("dir3/")

        try:
            self.assertEquals(self.o.list('"dir1"')[0], 'OK')
        except:
            traceback.print_exc()

        try:
            self.assertEquals(self.o.list('"dir2"')[0], 'OK')
        except:
            traceback.print_exc()

        try:
            self.assertEquals(self.o.list('"dir3"')[0], 'OK')
        except:
            traceback.print_exc()

        dirlist.reverse()
        for d in dirlist:
            self.o.delete(d)

    def testLogin(self):
        """
        login(user, password)
            Identify the client using a plaintext password.  The PASSWORD will
            be quoted.
        """
        self.o.logout()
        result = self.setUp("testuser1", "test")
        self.assertEquals(result[0], 'OK')
        self.failUnlessRaises(Exception, self.setUp, "testuser1",
                              "blahRANDOMblah")

    def testLogin_cram_md5(self):
        """
        login_cram_md5(user, password)
            Force use of `CRAM-MD5' authentication when identifying the client
            to protect the password.  Will only work if the server
            `CAPABILITY' response includes the phrase `AUTH=CRAM-MD5'.
        """
        o = getsock()
        o.debug = DEBUG
        result = o.login_cram_md5("testuser1", "test")
        self.assertEquals(result[0], 'OK')
        o = getsock()
        o.debug = DEBUG
        self.failUnlessRaises(Exception, o.login_cram_md5,
                              "testuser1", "wrongpassword")

        o = getsock()
        o.debug = DEBUG
        self.failUnlessRaises(Exception, o.login_cram_md5,
                              "fakeuser", "wrongpassword")

        o = getsock()
        o.debug = DEBUG
        # testuser3 password stored as sha256 so this must fail
        self.failUnlessRaises(Exception, o.login_cram_md5,
                              "testuser3", "password123")

    def testLogout(self):
        """
        logout()
             Shutdown connection to server. Returns server `BYE' response.
        """
        self.assertEquals(self.o.logout()[0], 'BYE')

    def testLsub(self):
        """
        lsub([directory[, pattern]])'
            List subscribed mailbox names in directory matching pattern.
            DIRECTORY defaults to the top level directory and PATTERN defaults
            to match any mailbox.  Returned data are tuples of message part
            envelope and data.
        """
        mailboxes = ['test1', 'test1/sub1', 'test1/sub1/subsub1',
            'test2', 'test2/sub2', 'test2/subsub2',
            'test3']
        for mailbox in mailboxes:
            self.o.create(mailbox)
            self.o.subscribe(mailbox)
        result = self.o.lsub()[1]
        self.assertEquals('(\\hasnochildren) "/" "%s"' % mailboxes[6]
                          in result, True)
        result = self.o.lsub('""', '"*"')[1]
        self.assertEquals('(\\hasnochildren) "/" "%s"' % mailboxes[2]
                          in result, True)
        result = self.o.lsub('"%s/"' % mailboxes[1])[1]
        self.assertEquals('(\\hasnochildren) "/" "%s"' % mailboxes[2]
                          in result, True)

    def testNoop(self):
        """
        noop()
             Send `NOOP' to server.
        """
        self.assertEquals(self.o.noop(), ('OK', ['NOOP completed']))
        self.o.select('INBOX')
        p = getsock()
        p.debug = DEBUG
        p.login('testuser1', 'test'), ('OK', ['LOGIN completed'])
        p.select('INBOX')

        self.o.append('INBOX', '\Flagged',
                      "\" 3-Mar-2006 07:15:00 +0200 \"",
                      str(TESTMSG['strict822']))
        self.assertEquals(p.noop()[0], 'OK')

    def testPartial(self):
        """
        partial(message_num, message_part, start, length)
            Fetch truncated part of a message.  Returned data is a tuple of
            message part envelope and data.
        """
        #self.fail(unimplementedError)

    def testProxyauth(self):
        """
        proxyauth(user)
            Assume authentication as USER.  Allows an authorised administrator
            to proxy into any user's mailbox.
        """
        #self.fail(unimplementedError)

    def testRecent(self):
        """
        recent()
            Prompt server for an update. Returned data is `None' if no new
            messages, else value of `RECENT' response.
        """
        readonly = 1
        self.o.create('recenttestbox')
        for i in range(0, 11):
            self.o.append('recenttestbox', '\Flagged Userflag',
                          "\" 3-Mar-2006 07:15:00 +0200 \"",
                          str(TESTMSG['strict822']))

        self.o.select('recenttestbox', readonly)
        result = self.o.untagged_responses
        self.assertEquals(
            int(result['RECENT'][0]) > 1, True)
        self.assertEquals(int(self.o.recent()[1][0]) > 0, True)
        self.o.fetch("1:*", "(Flags)")
        self.o.fetch("1:*", "(Flags)")

        self.o.select('recenttestbox')
        self.o.fetch("1:*", "(Flags)")
        self.assertEquals(
            self.o.status("recenttestbox", '(RECENT)')[1][0],
            """"recenttestbox" (RECENT 0)""")

    def testRename(self):
        """
        rename(oldmailbox, newmailbox)
             Rename mailbox named OLDMAILBOX to NEWMAILBOX.
        """
        self.o.create('testrename')
        self.assertEquals(
            self.o.rename('testrename', 'testrename1'),
            ('OK', ['RENAME completed']))
        self.failUnless(
            '(\\hasnochildren) "/" "testrename1"' in self.o.list()[1])
        self.assertEquals(
            self.o.rename('testrename1', 'nodir/testrename2'),
            ('NO', ['new mailbox would invade mailbox structure']))
        self.o.create('testdir')
        self.assertEquals(
            self.o.rename('testrename1', 'testdir/testrename2'),
            ('OK', ['RENAME completed']))
        self.failUnless(
            '(\\hasnochildren) "/" "testdir/testrename2"' in self.o.list()[1])
        self.o.delete('dir1/testrename2')

    def testSearch(self):
        """
        search(charset, criterion[, ...])
            Search mailbox for matching messages.  Returned data contains a
            space separated list of matching message numbers.  CHARSET may be
            `None', in which case no `CHARSET' will be specified in the
            request to the server.  The IMAP protocol requires that at least
            one criterion be specified; an exception will be raised when the
            server returns an error.
        """
        self.o.create('sbox')
        for i in range(0, 10):
            self.o.append('sbox', '', '', str(TESTMSG['strict822']))
            self.o.append('sbox', '', '', str(TESTMSG['multipart']))

        self.o.select('sbox')
        self.assertEquals(self.o.fetch("1:*", "(Flags)")[0], 'OK')

        result1 = self.o.search(None, "1:*", "DELETED")
        self.assertEquals(result1[0], 'OK')
        result2 = self.o.search(None, "1:*", "NOT", "DELETED")
        self.assertEquals(result2[0], 'OK')
        result1 = self.o.search(None, "UNDELETED", "BODY", "test")
        self.assertEquals(result1[0], 'OK')
        result2 = self.o.search(None, "1:*", "UNDELETED", "BODY", "test")
        self.assertEquals(result2[0], 'OK')
        self.assertEquals(result1, result2)

        result = self.o.search(
            None, "RECENT", "HEADER",
            "X-OfflineIMAP-901701146-4c6f63616c4d69726a616d-494e424f58",
            "1086726519-0790956581151")
        self.assertEquals(result[0], 'OK')
        result = self.o.search(None, "UNDELETED", "HEADER", "TO", "testuser")
        self.assertEquals(result[0], 'OK')
        result = self.o.search(
            None, "UNDELETED", "HEADER", "TO", "testuser",
            "SINCE", "1-Feb-1994")
        self.assertEquals(result[0], 'OK')
        result = self.o.search(
            None, "UNDELETED", "HEADER", "TO", "testuser",
            "BEFORE", "1-Jan-2004")
        self.assertEquals(result[0], 'OK')
        result = self.o.search(
            None, "UNDELETED", "HEADER", "TO", "testuser",
            "SENTSINCE", "1-Feb-1994")
        self.assertEquals(result[0], 'OK')
        result = self.o.search(
            None, "UNDELETED", "HEADER", "TO", "testuser",
            "SENTBEFORE", "1-Jan-2004")
        self.assertEquals(result[0], 'OK')
        result = self.o.search(
            None, "UNDELETED", "HEADER", "TO", "testuser",
            "SENTON", "26-Sep-2005")
        self.assertEquals(result[0], 'OK')

    def testSelect(self):
        """
        select([mailbox[, readonly]])
            Select a mailbox. Returned data is the count of messages in
            MAILBOX (`EXISTS' response).  The default MAILBOX is `'INBOX''.
            If the READONLY flag is set, modifications to the mailbox are not
            allowed.
        """
        self.assertEquals(self.o.select()[0], 'OK')
        self.assertEquals(self.o.select('INBOX')[0], 'OK')
        self.assertEquals(self.o.select('INBOX', 1)[0], 'OK')
        self.o.create('testflag')
        self.o.append('testflag',
                      '\Flagged Userflag', "\" 3-Mar-2006 07:15:00 +0200 \"",
                      str(TESTMSG['strict822']))
        self.o.select('testflag')
        self.assertEquals(
            'Userflag' in
            self.o.untagged_responses['PERMANENTFLAGS'][0].split(' '), True)

    def testSetacl(self):
        """
        setacl(mailbox, who, what)
            Set an `ACL' for MAILBOX.
        """

        self.o.create('testaclbox')
        self.assertEquals(self.o.setacl(
            'testaclbox', 'testuser2', 'slrw')[0], 'OK')

        p = getsock()
        p.login('testuser2', 'test'), ('OK', ['LOGIN completed'])
        self.assertEquals(
            '(\\hasnochildren) "/" "#Users/testuser1/testaclbox"' in
            p.list()[1], True)
        p.logout()
        self.o.delete('testaclbox')

    def testSetquota(self):
        """
        setquota(root, limits)'
            Set the `quota' ROOT's resource LIMITS.  This method is part of
            the IMAP4 QUOTA extension defined in rfc2087
        """
        #self.fail(unimplementedError)

    def testSort(self):
        """
        sort(sort_criteria, charset, search_criterion[, ...])
            The `sort' command is a variant of `search' with sorting semantics
            for the results.  Returned data contains a space separated list of
            matching message numbers.

            Sort has two arguments before the SEARCH_CRITERION argument(s); a
            parenthesized list of SORT_CRITERIA, and the searching CHARSET.
            Note that unlike `search', the searching CHARSET argument is
            mandatory.  There is also a `uid sort' command which corresponds
            to `sort' the way that `uid search' corresponds to `search'.  The
            `sort' command first searches the mailbox for messages that match
            the given searching criteria using the charset argument for the
            interpretation of strings in the searching criteria.  It then
            returns the numbers of matching messages.
        """
        self.o.select('INBOX')
        result = self.o.sort('(FROM)', 'US-ASCII', 'RECENT')
        self.assertEquals(result[0], 'OK')
        result = self.o.sort('(FROM)', 'US-ASCII', 'RECENT',
                             'HEADER', 'MESSAGE-ID', '<asdfasdf@nfg.nl>')
        self.assertEquals(result[0], 'OK')
        result = self.o.sort('(DATE ARRIVAL)', "UNDELETED",
                             "HEADER", "TO", "testuser",
                             "SENTSINCE", "1-Feb-1994")
        self.assertEquals(result[0], 'OK')
        result = self.o.sort('(REVERSE ARRIVAL FROM)', "UNDELETED",
                             "HEADER", "TO", "testuser", "SENTSINCE",
                             "1-Feb-1994")

    def testStatus(self):
        """
        status(mailbox, names)
            Request named status conditions for MAILBOX.
        """
        self.o.create("test status")
        self.o.status("test status",
                      '(UIDNEXT UIDVALIDITY MESSAGES UNSEEN RECENT)')
        before = self.o.status('INBOX', '(UNSEEN RECENT)')
        self.assertEquals(before[0], 'OK')
        self.assertEquals(
            self.p.append('INBOX', (), "",
                          str(TESTMSG['strict822']))[0], 'OK')
        after = self.o.status('INBOX', '(UNSEEN RECENT)')
        self.assertNotEquals(before[1], after[1])

    def testStore(self):
        """
        store(message_set, command, flag_list)
            Alters flag dispositions for messages in mailbox.
        """
        self.o.select('INBOX')
        self.assertEquals(self.o.store('1:*', '+FLAGS', '\Deleted')[0], 'OK')

        p = getsock()
        p.debug = DEBUG
        p.login('testuser1', 'test'), ('OK', ['LOGIN completed'])
        p.select('INBOX')

        self.assertEquals(self.o.store('1:*', '-FLAGS', '\Deleted')[0], 'OK')
        self.assertRaises(self.o.error, self.o.store, '1:*', '-FLAGS',
                          '\Recent')
        self.assertEquals(self.o.store('1:*', '+FLAGS', '\Deleted')[0], 'OK')
        #nprint p.untagged_responses
        p.noop()
        #print p.untagged_responses
        self.o.expunge()
        p.noop()
        #print p.untagged_responses

        self.assertEquals(
            self.o.append('INBOX', (), "",
                          str(TESTMSG['strict822']))[0], 'OK')
        self.assertEquals(
            self.o.append('INBOX', (), "",
                          str(TESTMSG['strict822']))[0], 'OK')
        self.assertEquals(
            self.o.append('INBOX', (), "",
                          str(TESTMSG['strict822']))[0], 'OK')
        p.noop()
        p.store('1:*', '+FLAGS', '\Flagged')
        self.o.noop()

    def testSubscribe(self):
        """
        subscribe(mailbox)
            Subscribe to new mailbox.
        """
        self.o.create('testsubscribe')
        self.assertEquals(self.o.subscribe('testsubscribe'),
                          ('OK', ['SUBSCRIBE completed']))

    def testUid(self):
        """
        uid(command, arg[, ...])
            Execute command args with messages identified by UID, rather than
            message number.  Returns response appropriate to command.  At least
            one argument must be supplied; if none are provided, the server
            will return an error and an exception will be raised.
        """
        self.o.select('INBOX')
        result = self.o.uid('SEARCH', '1,*')
        self.assertEquals(len(result[1]) < 10, True)
        result = self.o.uid('FETCH', '10:*', 'FLAGS')
        self.assertEquals(len(result[1]) > 0, True)
        self.o.create('testuidcopy')
        result = self.o.uid('COPY', '*', 'testuidcopy')
        self.assertEquals(result[0], 'OK')
        result = self.o.uid('THREAD', 'ORDEREDSUBJECT', 'US-ASCII', 'ALL')
        self.assertEquals(result[0], 'OK')

    def testUnsubscribe(self):
        """
        unsubscribe(mailbox)
            Unsubscribe from old mailbox.
        """
        self.o.create('testunsub')
        self.assertEquals(
            self.o.unsubscribe('testunsub'), ('OK', ['UNSUBSCRIBE completed']))

    def testNegativeLongLine(self):
        """
        TBD
        """
        self.o.select('INBOX')
        #self.assertRaises(
        #    self.o.error, self.o.fetch, "1" + " " * 120000, "(Flags)")

    def testBug978(self):
        """
        Test http://www.dbmail.org/mantis/view.php?id=978
        create a hierarchy that's likely to reproduce the error
        """
        # create some test folders
        self.o.create('testfolder1')
        self.o.create('testfolder2')
        # check that the server is not dead after a simple list and noop
        result = self.o.list("", "%")
        self.assertEqual(result[0], 'OK')
        result = self.o.noop()
        self.assertEqual(result[0], 'OK')

        # create hierarchy
        base_name = "Test folder"
        second_level = base_name + "/2012"
        self.o.create(base_name)
        self.o.create(second_level)
        subfolders = ['02', '04', '09', '03', '05', '06']
        for i in subfolders:
            self.o.create(second_level + "/" + i)

        # run a list command and check the base_name folder in the results
        result = self.o.list("", "%")
        self.assertEqual(result[0], 'OK')
        # find the base_name folder in the results
        folder = [
            i for i in result[1] if
            re.search("\"" + re.escape(base_name) + "\"$", i)
        ]
        self.assertNotEqual([], folder, base_name + ' is in the results')
        folder = folder[0]
        flags = re.search("^\((.*?)\)", folder).group(1)
        # check for the \Noselect error
        self.assertEqual(
            [],
            [i for i in flags.split(" ") if i.lower() == '\\noselect'],
            'noselect is not in attributes for %s: [%s]' % (base_name, flags))

        # run a list command and check the second_level folder in the results
        result = self.o.list(base_name + "/", "%")
        self.assertEqual(result[0], 'OK')
        # find the second_level folder in the results
        folder = [i for i in result[1] if
                  re.search("\"" + re.escape(second_level) + "\"$", i)
                 ]
        self.assertNotEqual([], folder, second_level + ' is in the results')
        folder = folder[0]
        flags = re.search("^\((.*?)\)", folder).group(1)
        # check for the \Noselect error
        self.assertEqual([], [
            i for i in flags.split(" ") if i.lower() == '\\noselect'],
            'noselect is not in attributes for %s [%s]' % (
                second_level, flags))

        # check whether all subfolders are in the list
        result = self.o.list(second_level + "/", "%")
        self.assertEqual(len(subfolders), len(result[1]),
                         'number of subfolders match')
        for i in subfolders:
            folder = [j for j in result[1] if
                      re.search("\"" + re.escape(
                          second_level + "/" + i) + "\"$", j)]
            self.assertNotEqual([], folder,
                                second_level + "/" + i + ' is in the results')

    def testBug987(self):
        """
        Test http://www.dbmail.org/mantis/view.php?id=987
        create a hierarchy (with Cyrillic folder names) that's reproducing
        the error
        """
        # create hierarchy, it's important to have one Cyrillic parent folder
        base_name = "&BBoEOARXBDI-"  # 'Київ' in utf7-imap
        self.o.create(base_name)
        # these folders don't essentially need to be in Cyrillic
        subfolders = [
            "&BBwEMAQ5BDQEMAQ9-",
            "&BBcEPgQ7BD4EQgRW- &BBIEPgRABD4EQgQw-"
        ]  # 'Майдан' and 'Золоті Ворота' in utf7-imap
        for i in subfolders:
            self.o.create(base_name + "/" + i)

        # run a list command and check the base_name folder in the results
        result = self.o.list("", "%")
        self.assertEqual(result[0], 'OK')
        # find the base_name folder in the results
        folder = [i for i in result[1] if
                  re.search("\"" + re.escape(base_name) + "\"$", i)]
        self.assertNotEqual([], folder, base_name + ' is in the results')
        folder = folder[0]
        flags = re.search("^\((.*?)\)", folder).group(1)
        # check for the \Hasnochildren flag - shouldn't be in the flags
        self.assertEqual([], [i for i in flags.split(" ") if
                              i.lower() == '\\hasnochildren'],
                         'hasnochildren is not in attributes for ' + base_name)

    def tearDown(self):
        try:
            dirs = []
            for d in self.o.list()[1]:
                d = re.match('(\(.*\)) "([^"])" "([^"]+)"', d).groups()[-1]
                dirs.append(d)
            for i in range(0, len(dirs)):
                if dirs[-i] == 'INBOX' or dirs[-i][0] == '#':
                    continue
                self.o.delete(dirs[-i])
            self.o.logout()
        except:
            pass


if __name__ == '__main__':
    unittest.main()

#EOF
