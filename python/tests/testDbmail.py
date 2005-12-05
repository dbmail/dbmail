#!/usr/bin/python

import os,sys,string

unimplementedError = 'Dbmail unimplemented'
configError = 'Dbmail configuration error'

from dbmail.lib.DbmailConfig import DbmailConfig
from dbmail.lib.Dbmail import Dbmail, DbmailSmtp, DbmailUser, DbmailAlias
from dbmail.lib.DbmailAutoreply import DbmailAutoreply
    
if __name__=='__main__':
    import unittest
    class testDbmailConfig(unittest.TestCase):
        def setUp(self):
            self.o=DbmailConfig()
            
        def testParse(self):
            self.failUnless(type(self.o._config)==type({}))
            self.failUnless(self.o._config.has_key('SMTP'))
        
        def testGetConfig(self):
            self.failUnless(self.o.getConfig())
            self.failUnless(self.o.getConfig('SMTP','SENDMAIL'))
            self.failUnless(type(int(self.o.getConfig('IMAP','NCHILDREN')))==type(1))
            
    class testDbmail(unittest.TestCase):
        def setUp(self):
            self.o=Dbmail()

        def testGetConfig(self):
            self.failUnless(self.o.getConfig().has_key('db'))

        def testSetCursor(self):
            self.o.setCursor()

    class testDbmailAlias(unittest.TestCase):
        def setUp(self):
            self.o=DbmailAlias()
            
        def testGet(self):
            self.o.set('testget','testget')
            self.failUnless(self.o.get('testget','testget'))

        def testSet(self):
            self.o.set('foo@bar','foo2@bar')
            self.failUnless(self.o.get('foo@bar'))
        
        def testDelete(self):
            self.o.set('foo2','bar2')
            self.o.delete('foo2','bar2')
            self.failIf(self.o.get('foo2','bar2'))

    class testDbmailUser(unittest.TestCase):
        def setUp(self):
            self.o = DbmailUser('testuser1')
            
        def testGet(self):
            self.failUnless(self.o.getUserdict())

        def testGetUidNumber(self):
            self.failUnless(self.o.getUidNumber())

        def testGetGidNumber(self):
            self.failUnless(self.o.getGidNumber() >= 0)

    class testDbmailAutoreply(unittest.TestCase):
        def setUp(self):
            self.o = DbmailAutoreply('testuser1')

        def testSet(self):
            raw=u"""test '' \0  reply message"""
            self.failIf(self.o.set(raw))
            self.failUnless(self.o.get())
            self.o.delete()
            
        def testGet(self):
            self.o.set("test reply message")
            self.failUnless(self.o.get())
            self.failIf(DbmailAutoreply('testuser2').get())
            self.failIf(DbmailAutoreply('nosuchuser').get())
            
        def tearDown(self):
            try:
                self.o.delete()
            except:
                pass
                
    unittest.main() 

