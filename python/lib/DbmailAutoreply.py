#!/usr/bin/python

import os,sys,string

unimplementedError = 'Dbmail unimplemented'
configError = 'Dbmail configuration error'

from dbmail.lib.Dbmail import Dbmail, DbmailUser
 
class DbmailAutoreply(Dbmail):

    def __init__(self,userid,file=None):
        Dbmail.__init__(self,file)
        self._table="%sauto_replies" % self._prefix
        self.setUser(DbmailUser(userid))
        assert(self.getUser() != None)

    def setUser(self,user): self._user=user
    def getUser(self): return self._user

    def get(self):
        c=self.getCursor()
        q="""select reply_body from %s where user_idnr=%%s""" % self._table
        try:
            c.execute(q, (self.getUser().getUidNumber()))
            dict = c.fetchone()
            assert(dict)
        except AssertionError:
            return None
        if dict:
            return dict['reply_body']
 
    def set(self,message):
        q="""insert into %s (user_idnr,reply_body) values (%%s,%%s)""" % self._table
        self.getCursor().execute(q, (self.getUser().getUidNumber(),message,))
       
    def delete(self):
        q="""delete from %s where user_idnr=%%s""" % self._table
        self.getCursor().execute(q, (self.getUser().getUidNumber()))
    
    getReply = get
    setReply = set 
    delReply = delete

