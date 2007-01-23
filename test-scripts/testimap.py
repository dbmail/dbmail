#!/usr/bin/python

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
# $Id: testimap.py 2427 2007-01-23 10:13:01Z paul $

# For a protocol trace set to 4
DEBUG = 0
#DEBUG = 4

# select 'stream' for non-forking mode
TYPE = 'stream'
# select 'network' for forking mode
#TYPE = 'network'


import unittest, imaplib, re
import sys, traceback, getopt, string
from email.MIMEText import MIMEText
from email.MIMEMultipart import MIMEMultipart
import email

unimplementedError = 'Dbmail testcase unimplemented'

# for network testing
HOST,PORT = "localhost", 143

# for stdin/stdout testing
DAEMONBIN = "./dbmail-imapd -n -f /etc/dbmail/dbmail-test.conf"
# with valgrind
#DAEMONBIN = "valgrind --suppressions=./contrib/dbmail.supp --leak-check=full %s" % DAEMONBIN


TESTMSG={}

def getMessageStrict():
    m=MIMEText("""
    this is a test message
    """)
    m.add_header("To",""""test user" <testuser@foo.org>""")
    m.add_header("From",""""somewhere.foo" <somewher@foo.org>""")
    m.add_header("CC",""""somewher@foo.org", "other@bar.org" """)
    m.add_header("In-Reply-To",""""Message from "Test User" <testuser@test.org>    of "Sat, 14 Dec 2002 09:17:00 CST." """)
    m.add_header("Date","Mon, 26 Sep 2005 13:26:39 +0200")
    m.add_header("Subject","""dbmail "test" message""")
    m.add_header("Message-Id","""<"114.5862946l.21522l.0l"@localhost>""")
    return m

def getMultiPart():
    m=MIMEMultipart()
    m.attach(getMessageStrict())
    m.add_header("To","testaddr@bar.org")
    m.add_header("From","testuser@foo.org")
    m.add_header("Date","Sun, 25 Sep 2005 13:26:39 +0200")
    m.add_header("Subject","dbmail multipart message")
    return m
   
TESTMSG['strict822']=getMessageStrict()
TESTMSG['multipart']=getMultiPart()

def getsock():
    if TYPE == 'network':
        return imaplib.IMAP4(HOST,PORT)
    elif TYPE == 'stream':
        return imaplib.IMAP4_stream(DAEMONBIN)

def strip_crlf(s):
    return string.replace(s,'\r','')

class testImapServer(unittest.TestCase):

    def setUp(self,username="testuser1",password="test"):
        self.o = getsock()
        self.o.debug = DEBUG
        return self.o.login(username,password)

    def testAppend(self):
        """ 
        'append(mailbox, flags, date_time, message)'
             Append message to named mailbox.
        """
        # check for OK
        self.assertEquals(self.o.append('INBOX',(),"",str(TESTMSG['strict822']))[0],'OK')
        # check for TRYCREATE
        result=self.o.append('nosuchbox',(),"",str(TESTMSG['strict822']))
        self.assertEquals(result[0],'NO')
        self.assertEquals(result[1][0][:11],'[TRYCREATE]')
        # test flags
        self.o.create('testappend')
        self.o.append('testappend','\Flagged',"\" 3-Mar-2006 07:15:00 +0200 \"",str(TESTMSG['strict822']))
        self.o.select('testappend')
        id=self.o.recent()[1][0]
        
        result = self.o.fetch(id,"(UID BODY[])")
        self.assertEquals(result[0],'OK')
        
        result = self.o.fetch(id,"(UID BODY[TEXT])")
        self.assertEquals(result[0],'OK')
        expectlen = int(string.split(result[1][0][0])[-1][1:-1])
        expectmsg = result[1][0][1]
        self.assertEquals(len(expectmsg),expectlen)
        self.assertEquals(strip_crlf(expectmsg),TESTMSG['strict822'].get_payload())

        result = self.o.fetch(id,"(UID BODY.PEEK[TEXT])")
        self.assertEquals(result[0],'OK')
        self.assertEquals(strip_crlf(result[1][0][1]),TESTMSG['strict822'].get_payload())

#        expect = '  FLAGS (\\Seen \\Flagged \\Recent))'
#        self.assertEquals(result,expect)

    def testCheck(self):
        """ 
        'check()'
            Checkpoint mailbox on server.
        """     
        self.o.select('INBOX')
        self.assertEquals(self.o.check(),('OK', ['CHECK completed']))

    def testClose(self):
        """ 
        'close()'
             Close currently selected mailbox. Deleted messages are removed from
            writable mailbox. This is the recommended command before `LOGOUT'.
        """
        self.o.select('INBOX')
        self.assertEquals(self.o.close(),('OK', ['CLOSE completed']))

    def testCopy(self):
        """ 
        'copy(message_set, new_mailbox)'
            Copy MESSAGE_SET messages onto end of NEW_MAILBOX.
        """
        self.o.create('testcopy1')
        self.o.create('testcopy2')
        self.o.append('testcopy1',"","",str(TESTMSG['strict822']))
        self.o.select('testcopy1')
        id = self.o.recent()[1][0]
        self.assertEquals(self.o.copy(id,'testcopy2'),('OK', ['COPY completed']))

    def testCreate(self):
        """ 
        'create(mailbox)'
            Create new mailbox named MAILBOX.
        """
        self.assertEquals(self.o.create('test create'),('OK',['CREATE completed']))
        self.o.create("dir1")
        result = self.o.list()[1]
        self.assertEquals('(\\hasnochildren) "/" "test create"' in self.o.list()[1], True)

    def testCreateListWithQuote(self):
        """ 
	Bug 314 -- Single Quote in Mailbox Name
        """
        self.assertEquals(self.o.create('Foo\'s Folder'),('OK',['CREATE completed']))
        result = self.o.list()[1]
        self.assertEquals('(\\hasnochildren) "/" "Foo\'s Folder"' in self.o.list()[1], True)


    def testDelete(self):
        """ 
        'delete(mailbox)'
            Delete old mailbox named MAILBOX.
        """
        self.o.create('testdelete')
        self.assertEquals(self.o.delete('testdelete'),('OK',['DELETE completed']))

    def testExpunge(self):
        """ 
        expunge()'
            Permanently remove deleted items from selected mailbox. Generates
            an `EXPUNGE' response for each deleted message. Returned data
            contains a list of `EXPUNGE' message numbers in order received.
        """
        self.o.select('INBOX')
        self.assertEquals(self.o.expunge(),('OK', [None]))

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
        self.o.append('tmpbox','','',str(m))
        self.o.select('tmpbox')
        self.assertEquals(self.o.fetch("1:*","(Flags)")[0],'OK')
        id=self.o.recent()[1][0]
        
        # fetch complete message. order and number of headers may differ
        result1 = self.o.fetch(id,"(UID BODY[])")
        result2 = self.o.fetch(id,"(UID RFC822)")
        self.assertEquals(result1[0],'OK')
        self.assertEquals(result2[0],'OK')
        self.assertEquals(result1[1][0][1],result2[1][0][1])
        
        # get the body. must equal input message's body
        result = self.o.fetch(id,"(UID BODY[TEXT])")
        bodytext = strip_crlf(result[1][0][1])
        self.assertEquals(bodytext,m.get_payload())
        
        result = self.o.fetch(id,"(UID BODYSTRUCTURE)")
        self.assertEquals(result[0],'OK')

        result = self.o.fetch(id,"(ENVELOPE)")
        expect = [('1 (ENVELOPE ("Mon, 26 Sep 2005 13:26:39 +0200" {21}','dbmail "test" message'), (' (("somewhere.foo" NIL "somewher" "foo.org")) (("somewhere.foo" NIL "somewher" "foo.org")) (("somewhere.foo" NIL "somewher" "foo.org")) (("test user" NIL "testuser" "foo.org")) ((NIL NIL "somewher" "foo.org")(NIL NIL "other" "bar.org")) NIL {82}', 'Message from "Test User" <testuser@test.org>    of "Sat,\t14 Dec 2002 09:17:00 CST.'), (' {36}', '<"114.5862946l.21522l.0l"@localhost>'), '))']
        self.assertEquals(result[0],'OK')
        self.assertEquals(result[1],expect)

        result = self.o.fetch(id,"(UID BODY[TEXT]<0.20>)")
        self.assertEquals(result[0],'OK')
        self.assertEquals(self.o.fetch(id,"(UID BODY.PEEK[TEXT]<0.30>)")[0],'OK')
        self.assertEquals(self.o.fetch(id,"(UID RFC822.SIZE)")[0],'OK')
        
        result=self.o.fetch(id,"(UID RFC822.HEADER)")
        self.assertEquals(result[0],'OK')
        self.assertEquals(result[1][0][1][-2:],'\r\n')
        
        # OE query
        result=self.o.fetch(id,"(BODY.PEEK[HEADER.FIELDS (References X-Ref X-Priority X-MSMail-Priority X-MSOESRec Newsgroups)] ENVELOPE RFC822.SIZE UID FLAGS INTERNALDATE)")
        self.assertEquals(result[0],'OK')

        # TB query
        result=self.o.fetch(id,"(UID RFC822.SIZE FLAGS BODY.PEEK[HEADER.FIELDS (From To Cc Subject Date Message-ID Priority X-Priority References Newsgroups In-Reply-To Content-Type)])")
        self.assertEquals(result[0],'OK')
        
        # test big folder full fetch
        self.o.select('INBOX')
        self.o.fetch("1:*","(UID FULL)")

    def testGetacl(self):
        """ 
        `getacl(mailbox)'
            Get the `ACL's for MAILBOX. 
        """
        self.assertEquals(self.o.getacl('INBOX'),('OK', ['"INBOX" testuser1 lrswipcda']))
        
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
        dirlist=['dir1','dir2','dir3','dir1/sub1','dir2/sub2','dir2/sub 2a','dir3/sub 3','dir3/sub 3/ssub1','dir3/sub 3/.ssub2']
        for d in dirlist: 
            self.o.create(d)
        #print self.o.list()
        #print self.o.list("dir1/")
        #print self.o.list("dir2/")
        #print self.o.list("dir3/")

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
        """ 
        login(user, password)
            Identify the client using a plaintext password.  The PASSWORD will
            be quoted.
        """
        self.o.logout()
        result=self.setUp("testuser1","test")
        self.assertEquals(result,('OK', ['LOGIN completed']))
        self.failUnlessRaises(Exception,self.setUp,"testuser1","blah")

    def testLogin_cram_md5(self):
        """ 
        login_cram_md5(user, password)
            Force use of `CRAM-MD5' authentication when identifying the client
            to protect the password.  Will only work if the server
            `CAPABILITY' response includes the phrase `AUTH=CRAM-MD5'.
        """
        #self.fail(unimplementedError)

    def testLogout(self):
        """ 
        logout()
             Shutdown connection to server. Returns server `BYE' response.
        """
        self.assertEquals(self.o.logout()[0],'BYE')

    def testLsub(self):
        """ 
        lsub([directory[, pattern]])'
            List subscribed mailbox names in directory matching pattern.
            DIRECTORY defaults to the top level directory and PATTERN defaults
            to match any mailbox.  Returned data are tuples of message part
            envelope and data.
        """
        mailboxes = ['test1','test1/sub1','test1/sub1/subsub1',
            'test2','test2/sub2','test2/subsub2',
            'test3']
        for mailbox in mailboxes:
            self.o.create(mailbox)
            self.o.subscribe(mailbox)
        self.assertEquals('(\\hasnochildren) "/" "%s"' % mailboxes[6], self.o.lsub()[1][7])
        self.assertEquals('(\\hasnochildren) "/" "%s"' % mailboxes[2], self.o.lsub('""','"*"')[1][3])
        self.assertEquals('(\\hasnochildren) "/" "%s"' % mailboxes[2], self.o.lsub('"%s/"' % mailboxes[1],'"%"')[1][0])

    def testNoop(self):
        """ 
        noop()
             Send `NOOP' to server.
        """
        self.assertEquals(self.o.noop(),('OK', ['NOOP completed']))

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
        self.assertEquals(self.o.recent(),('OK', [None]))

    def testRename(self):
        """ 
        rename(oldmailbox, newmailbox)
             Rename mailbox named OLDMAILBOX to NEWMAILBOX.
        """
        self.o.create('testrename')
        self.assertEquals(self.o.rename('testrename','testrename1'),('OK', ['RENAME completed']))
        self.failUnless('(\\hasnochildren) "/" "testrename1"' in self.o.list()[1])
        self.assertEquals(self.o.rename('testrename1','nodir/testrename2'),('NO', ['new mailbox would invade mailbox structure']))
        self.o.create('testdir')
        self.assertEquals(self.o.rename('testrename1','testdir/testrename2'),('OK', ['RENAME completed']))
        self.failUnless('(\\hasnochildren) "/" "testdir/testrename2"' in self.o.list()[1])
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
        for i in range(0,10):
            self.o.append('sbox','','',str(TESTMSG['strict822']))
            self.o.append('sbox','','',str(TESTMSG['multipart']))

        self.o.select('sbox')
        self.assertEquals(self.o.fetch("1:*","(Flags)")[0],'OK')

        result1=self.o.search(None, "1:*", "DELETED")
        self.assertEquals(result1[0],'OK')
        result2=self.o.search(None, "1:*", "NOT", "DELETED")
        self.assertEquals(result2[0],'OK')
        result1=self.o.search(None, "UNDELETED", "BODY", "test")
        self.assertEquals(result1[0],'OK')
        result2=self.o.search(None, "1:*", "UNDELETED", "BODY", "test")
        self.assertEquals(result2[0],'OK')
        self.assertEquals(result1,result2)
        
        result=self.o.search(None, "RECENT", "HEADER", "X-OfflineIMAP-901701146-4c6f63616c4d69726a616d-494e424f58", "1086726519-0790956581151")
        self.assertEquals(result[0],'OK')
        result=self.o.search(None, "UNDELETED", "HEADER", "TO", "testuser")
        self.assertEquals(result[0],'OK')
        result=self.o.search(None, "UNDELETED", "HEADER", "TO", "testuser", "SINCE", "1-Feb-1994")
        self.assertEquals(result[0],'OK')
        result=self.o.search(None, "UNDELETED", "HEADER", "TO", "testuser", "BEFORE", "1-Jan-2004")
        self.assertEquals(result[0],'OK')
        result=self.o.search(None, "UNDELETED", "HEADER", "TO", "testuser", "SENTSINCE", "1-Feb-1994")
        self.assertEquals(result[0],'OK')
        result=self.o.search(None, "UNDELETED", "HEADER", "TO", "testuser", "SENTBEFORE", "1-Jan-2004")
        self.assertEquals(result[0],'OK')
        result=self.o.search(None, "UNDELETED", "HEADER", "TO", "testuser", "SENTON", "26-Sep-2005")
        self.assertEquals(result[0],'OK')
        #print result

    def testSelect(self):
        """ 
        select([mailbox[, readonly]])
            Select a mailbox. Returned data is the count of messages in
            MAILBOX (`EXISTS' response).  The default MAILBOX is `'INBOX''.
            If the READONLY flag is set, modifications to the mailbox are not
            allowed.
        """
        self.assertEquals(self.o.select()[0],'OK')
        self.assertEquals(self.o.select('INBOX')[0],'OK')
        self.assertEquals(self.o.select('INBOX',1)[0],'OK')

    def testSetacl(self):
        """ 
        setacl(mailbox, who, what)
            Set an `ACL' for MAILBOX.
        """
        
        self.o.create('testaclbox')
        self.assertEquals(self.o.setacl('testaclbox','testuser2','slrw')[0],'OK')

        p = getsock()
        p.login('testuser2','test'),('OK',['LOGIN completed'])
        self.assertEquals('(\\hasnochildren) "/" "#Users/testuser1/testaclbox"' in p.list()[1],True)
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
        result=self.o.sort('(FROM)','US-ASCII','RECENT')
        self.assertEquals(result[0],'OK')
        result=self.o.sort('(FROM)','US-ASCII','RECENT','HEADER','MESSAGE-ID','<asdfasdf@nfg.nl>')
        self.assertEquals(result[0],'OK')
	result=self.o.sort('(DATE ARRIVAL)', "UNDELETED", "HEADER", "TO", "testuser", "SENTSINCE", "1-Feb-1994")
        self.assertEquals(result[0],'OK')
	result=self.o.sort('(REVERSE ARRIVAL FROM)', "UNDELETED", "HEADER", "TO", "testuser", "SENTSINCE", "1-Feb-1994")
        #print result


    def testStatus(self):
        """ 
        status(mailbox, names)
            Request named status conditions for MAILBOX.
        """
        self.o.create("test status");
        self.o.status("test status",'(UIDNEXT UIDVALIDITY MESSAGES UNSEEN RECENT)');
        self.assertEquals(self.o.status('INBOX','(UIDNEXT MESSAGES UNSEEN RECENT)')[0],'OK')


    def testStore(self):
        """ 
        store(message_set, command, flag_list)
            Alters flag dispositions for messages in mailbox.
        """
        #self.fail(unimplementedError)
        
    def testSubscribe(self):
        """
        subscribe(mailbox)
            Subscribe to new mailbox.
        """
        self.o.create('testsubscribe')
        self.assertEquals(self.o.subscribe('testsubscribe'),('OK', ['SUBSCRIBE completed']))

    def testUid(self):
        """
        uid(command, arg[, ...])
            Execute command args with messages identified by UID, rather than
            message number.  Returns response appropriate to command.  At least
            one argument must be supplied; if none are provided, the server
            will return an error and an exception will be raised.
        """               
        self.o.select('INBOX')
        result=self.o.uid('SEARCH','1,*')
        self.assertEquals(len(result[1]) < 10, True)
        result=self.o.uid('FETCH','10:*', 'FLAGS')
        self.assertEquals(len(result[1]) > 0, True)
        
    def testUnsubscribe(self):
        """
        unsubscribe(mailbox)
            Unsubscribe from old mailbox.
        """
        self.o.create('testunsub')
        self.assertEquals(self.o.unsubscribe('testunsub'),('OK', ['UNSUBSCRIBE completed']))

    def testNegativeLongLine(self):
        """
	    TBD
	"""
        self.o.select('INBOX')
        self.assertRaises(self.o.error, self.o.fetch, "1" + " "*12000,"(Flags)")
        
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

def usage():
    print """testimap.py:   test dbmail imapserver
    
    -h <host>|--host=<host>     hostname or address of imap server
    -p <port>|--port=<port>     portname or number of imap server
    -d <debuglevel>|--debug=<debuglevel>    debug level
    
    """
    

if __name__=='__main__':
    unittest.main()
#    try:
#        opts,args = getopt.getopt(sys.argv[1:], "", ["host=","port=","help","debug="])
#    except getopt.GetoptError:
#        usage()
#        sys.exit(2)
#    for o,a in opts:
#        if o == "--help":
#            usage()
#            sys.exit(0)
#        if o in ['--host']:
#            HOST=a
#        if o in ['--port']:
#            PORT=int(a)
#        if o in ['--debug']:
#            DEBUG=a
#            
#    suite=unittest.TestSuite()
#    suite.addTest(unittest.makeSuite(testImapServer))
#    unittest.TextTestRunner(verbosity=2).run(suite)
        
