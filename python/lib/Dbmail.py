#!/usr/bin/python

import os,sys,string

unimplementedError = 'Dbmail unimplemented'
configError = 'Dbmail configuration error'

from dbmail.lib.DbmailConfig import DbmailConfig

class Dbmail(DbmailConfig):
    _stanza_='DBMAIL'
    
    def __init__(self,file=None):
        DbmailConfig.__init__(self,file)
        self.setCursor()

    def getConfig(self,key=None):
        return self._getConfig(self._stanza_,key)

    def setCursor(self):
        storage=string.lower(self.getConfig(key='DRIVER'))
        if storage=='mysql':
            self._setMysqlDictCursor()
        elif storage=='pgsql':
            self._setPgsqlDictCursor()
        elif storage=='sqlite':
            self._setSqliteDictCursor()
        else:
            raise configError, 'invalid value for config-item: DRIVER'

    def _setMysqlDictCursor(self):
        import MySQLdb
        conn=MySQLdb.connect(host=self.getConfig('host'),user=self.getConfig('user'),
            db=self.getConfig('db'),passwd=self.getConfig('pass'))
        conn.autocommit(1)
        self._cursor=conn.cursor(MySQLdb.cursors.DictCursor)
        assert(self._cursor)

    def _setPgsqlDictCursor(self):
        use=None
        try:
            from pyPgSQL import PgSQL
            use='pgsql'
        except:
            pass
            
        try:
            import psycopg2
            import psycopg2.extras
            use='psycopg2'
        except:
            pass
            
        if use=='pgsql':
            conn = PgSQL.connect(database=self.getConfig('db'),host=self.getConfig('host'),
                user=self.getConfig('user'),password=self.getConfig('pass'))
            conn.autocommit=1
            self._conn = conn
            self._cursor = conn.cursor()
        
        elif use=='psycopg2':
            conn = psycopg2.connect("host=%s dbname=%s user=%s password=%s" % (\
                    self.getConfig('host'), self.getConfig('user'), \
                    self.getConfig('db'), self.getConfig('pass')))
            conn.set_isolation_level(0)
            self._conn = conn
            self._cursor = conn.cursor(cursor_factory=psycopg2.extras.DictCursor)
        
        else:
            raise novalidDriver, "no postgres driver available (PgSQL or psycopg2)"

        assert(self._cursor)
            
        
    def _setSqliteDictCursor(self):
        raise unimplementedError, "sqlite dict-cursor"
        
    def getCursor(self): return self._cursor

class DbmailSmtp(Dbmail):
    _stanza_='SMTP'

class DbmailAlias(Dbmail):

    def get(self,alias,deliver_to=None):
        c=self.getCursor()
        filter=''
        q="select * from %saliases where alias=%%s" % self._prefix
        if deliver_to: 
            q="%s AND deliver_to=%%s" % q
            c.execute(q,(alias, deliver_to,))
        else:
            c.execute(q,(alias,))
        return c.fetchall()
    
    def set(self,alias,deliver_to):
        c=self.getCursor()
        q="""INSERT INTO %saliases (alias,deliver_to) VALUES (%%s,%%s)""" % self._prefix
        if not self.get(alias,deliver_to):
            assert(alias and deliver_to)
            c.execute(q, (alias,deliver_to,))
            
    def delete(self,alias,deliver_to):
        c=self.getCursor()
        q="""DELETE FROM %saliases WHERE alias=%%s AND deliver_to=%%s""" % self._prefix
        if self.get(alias,deliver_to):
            c.execute(q, (alias,deliver_to,))
            
class DbmailUser(Dbmail):
    _dirty=1
    
    def __init__(self,userid):
        Dbmail.__init__(self)
        self.setUserid(userid)
        self._userdict={}

    def create(self): pass
    def update(self): pass
    def delete(self): pass
        
    def setDirty(self,dirty=1): self._dirty=dirty
    def getDirty(self): return self._dirty
        
    def setUserid(self,userid): self._userid=userid
    def getUserid(self): return self._userid
    
    def getUserdict(self):
        if not self.getDirty() and self._userdict:
            return self._userdict
            
        q="select * from %susers where userid=%%s" % self._prefix
        c=self.getCursor()
        c.execute(q,(self.getUserid(),))
        dict = c.fetchone()
        assert(dict)
        self._userdict = dict
        self.setDirty(0)
        
        return self._userdict

    def getUidNumber(self): return self.getUserdict()['user_idnr']
    def getGidNumber(self): return self.getUserdict()['client_idnr']
    

