#!/usr/bin/env python

# Copyright (C) 2004 Paul J Stevens paul at nfg dot nl
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

import unittest, imaplib, re
import traceback

unimplementedError = 'Dbmail testcase unimplemented'

# name of mail host and port of pop3 daemon
SERVER = ('test01', 10143)

class testDbmailImap(unittest.TestCase):

    def setUp(self):
        #self.o.debug = 4
        self.o = imaplib.IMAP4(SERVER[0], SERVER[1])
        self.assertEquals(self.o.login('testuser1','test'),('OK',['LOGIN completed']))

    def testAppend(self):
        """ test:
        'append(mailbox, flags, date_time, message)'
             Append message to named mailbox.
        """
        #raise unimplementedError

    def testCheck(self):
        """ test:
        'check()'
            Checkpoint mailbox on server.
        """     
        self.o.select('INBOX')
        self.assertEquals(self.o.check(),('OK', ['CHECK completed']))

    def testClose(self):
        """ test:
        'close()'
             Close currently selected mailbox. Deleted messages are removed from
            writable mailbox. This is the recommended command before `LOGOUT'.
        """
        self.o.select('INBOX')
        self.assertEquals(self.o.close(),('OK', ['CLOSE completed']))

    def testCopy(self):
        """ test:
        'copy(message_set, new_mailbox)'
            Copy MESSAGE_SET messages onto end of NEW_MAILBOX.
        """
        #raise unimplementedError

    def testCreate(self):
        """ test:
        'create(mailbox)'
            Create new mailbox named MAILBOX.
        """
        self.assertEquals(self.o.create('testbox'),('OK',['CREATE completed']))

    def testDelete(self):
        """ test:
        'delete(mailbox)'
            Delete old mailbox named MAILBOX.
        """
        self.o.create('testdelete')
        self.assertEquals(self.o.delete('testdelete'),('OK',['DELETE completed']))

    def testExpunge(self):
        """ test:
        expunge()'
            Permanently remove deleted items from selected mailbox. Generates
            an `EXPUNGE' response for each deleted message. Returned data
            contains a list of `EXPUNGE' message numbers in order received.
        """
        self.o.select('INBOX')
        self.assertEquals(self.o.expunge(),('OK', [None]))

    def testFetch(self):
        """ test:
        fetch(message_set, message_parts)'
            Fetch (parts of) messages.  MESSAGE_PARTS should be a string of
            message part names enclosed within parentheses, eg: `"(UID
            BODY[TEXT])"'.  Returned data are tuples of message part envelope
            and data.
        """
        #raise unimplementedError

    def testGetacl(self):
        """ test:
        `getacl(mailbox)'
            Get the `ACL's for MAILBOX. 
        """
        self.assertEquals(self.o.getacl('INBOX'),('OK', ['"INBOX" testuser1 lrswipcda ']))
        
    def getQuota(self):
        """ test:
        getquota(root)
            Get the `quota' ROOT's resource usage and limits.  This method is
            part of the IMAP4 QUOTA extension defined in rfc2087.
        """
        #raise unimplementedError
        
    def getQuotaroot(self):
        """ test:
        getquotaroot(mailbox)
            Get the list of `quota' `roots' for the named MAILBOX.  This
            method is part of the IMAP4 QUOTA extension defined in rfc2087.
        """
        #raise unimplementedError
        
    def testList(self):
        """ test:
        list([directory[, pattern]])
            List mailbox names in DIRECTORY matching PATTERN.  DIRECTORY
            defaults to the top-level mail folder, and PATTERN defaults to
            match anything.  Returned data contains a list of `LIST' responses.
        """
        dirlist=['dir1','dir1/sub1','dir2/sub2','dir2/sub 2a','dir3/sub 3/ssub1','dir3/sub 3/.ssub2']
        for d in dirlist: self.o.create(d)
        try:    
            self.assertEquals(self.o.list('"dir1"')[0],'OK')
        except: 
            traceback.print_exc()
            
        try:
            self.assertEquals(self.o.list('"dir2"')[0],'OK')
        except:
            traceback.print_exc()

        try:
            self.assertEquals(self.o.list('"dir3"')[0],'OK')
        except:
            traceback.print_exc()
            
        for d in dirlist: self.o.delete(d)

    def testLogin(self):
        """ test:
        login(user, password)
            Identify the client using a plaintext password.  The PASSWORD will
            be quoted.
        """
        #raise unimplementedError

    def testLogin_cram_md5(self):
        """ test:
        login_cram_md5(user, password)
            Force use of `CRAM-MD5' authentication when identifying the client
            to protect the password.  Will only work if the server
            `CAPABILITY' response includes the phrase `AUTH=CRAM-MD5'.
        """
        #raise unimplementedError

    def testLogout(self):
        """ test:
        logout()
             Shutdown connection to server. Returns server `BYE' response.
        """
        self.assertEquals(self.o.logout()[0],'BYE')

    def testLsub(self):
        """ test:
        lsub([directory[, pattern]])'
            List subscribed mailbox names in directory matching pattern.
            DIRECTORY defaults to the top level directory and PATTERN defaults
            to match any mailbox.  Returned data are tuples of message part
            envelope and data.
        """
        #raise unimplementedError

    def testNoop(self):
        """ test:
        noop()
             Send `NOOP' to server.
        """
        self.assertEquals(self.o.noop(),('OK', ['NOOP completed']))

    def testPartial(self):
        """ test:
        partial(message_num, message_part, start, length)
            Fetch truncated part of a message.  Returned data is a tuple of
            message part envelope and data.
                  
        """
        #raise unimplementedError
        
    def testProxyauth(self):
        """ test:
        proxyauth(user)
            Assume authentication as USER.  Allows an authorised administrator
            to proxy into any user's mailbox.
        """
        #raise unimplementedError

    def testRecent(self):
        """ test:
        recent()
            Prompt server for an update. Returned data is `None' if no new
            messages, else value of `RECENT' response.
        """
        self.assertEquals(self.o.recent(),('OK', [None]))

    def testRename(self):
        """ test:
        rename(oldmailbox, newmailbox)
             Rename mailbox named OLDMAILBOX to NEWMAILBOX.
        """
        self.o.create('testrename')
        self.assertEquals(self.o.rename('testrename','testrename1'),('OK', ['RENAME completed']))
        self.failUnless('() "/" "testrename1"' in self.o.list()[1])
        self.assertEquals(self.o.rename('testrename1','nodir/testrename2'),('NO', ['new mailbox would invade mailbox structure']))
        self.o.create('testdir')
        self.assertEquals(self.o.rename('testrename1','testdir/testrename2'),('OK', ['RENAME completed']))
        self.failUnless('() "/" "testdir/testrename2"' in self.o.list()[1])
        self.o.delete('dir1/testrename2')

    def testSearch(self):
        """ test:
        search(charset, criterion[, ...])
            Search mailbox for matching messages.  Returned data contains a
            space separated list of matching message numbers.  CHARSET may be
            `None', in which case no `CHARSET' will be specified in the
            request to the server.  The IMAP protocol requires that at least
            one criterion be specified; an exception will be raised when the
            server returns an error.
        """
        #raise unimplementedError


    def testSelect(self):
        """ test:
        select([mailbox[, readonly]])
            Select a mailbox. Returned data is the count of messages in
            MAILBOX (`EXISTS' response).  The default MAILBOX is `'INBOX''.
            If the READONLY flag is set, modifications to the mailbox are not
            allowed.
        """
        #raise unimplementedError
        
        

    def testSetacl(self):
        """ test:
        setacl(mailbox, who, what)
            Set an `ACL' for MAILBOX.
        """
        
        self.o.create('testaclbox')
        self.o.setacl('testaclbox','testuser2','slrw')

        p = imaplib.IMAP4(SERVER[0], SERVER[1])
        p.login('testuser2','test'),('OK',['LOGIN completed'])
        p.debug = 4
        try:
            #print p.list()
            pass
        except:
            traceback.print_exc()

        self.o.delete('testaclbox')

    def testSetquota(self):
        """ test:
        setquota(root, limits)'
            Set the `quota' ROOT's resource LIMITS.  This method is part of
            the IMAP4 QUOTA extension defined in rfc2087
        """
        #raise unimplementedError
        

    def testSort(self):
        """ test:
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
        #raise unimplementedError

    def testStatus(self):
        """ test:
        status(mailbox, names)
            Request named status conditions for MAILBOX.
        """
        #raise unimplementedError

    def testStore(self):
        """ test:
        store(message_set, command, flag_list)
            Alters flag dispositions for messages in mailbox.
        """
        #raise unimplementedError
        
    def testSubscribe(self):
        """ test:
        subscribe(mailbox)
            Subscribe to new mailbox.
        """
        #raise unimplementedError

    def testUid(self):
        """
        uid(command, arg[, ...])
            Execute command args with messages identified by UID, rather than
            message number.  Returns response appropriate to command.  At least
            one argument must be supplied; if none are provided, the server
            will return an error and an exception will be raised.
        """                
        #raise unimplementedError
        
    def testUnsubscribe(self):
        """
        unsubscribe(mailbox)
            Unsubscribe from old mailbox.
        """
        #raise unimplementedError
        
    def tearDown(self):
        try:
            dirs = []
            for d in self.o.list()[1]:
                d=re.match('(\(.*\)) "([^"])" "([^"]+)"',d).groups()[-1]
                dirs.append(d)
            for i in range(0,len(dirs)):
                if dirs[-i]=='INBOX': continue
                self.o.delete(dirs[-i])
            self.o.logout()
        except: pass


if __name__=='__main__': unittest.main()

        
