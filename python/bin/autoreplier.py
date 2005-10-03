#!/usr/bin/python

#
# Copyright: NFG, Paul Stevens <paul@nfg.nl>, 2005
# License: GPL
#
# $Id: autoreplier.py,v 1.4 2004/12/01 10:15:58 paul Exp $
#
# Reimplementation of the famous vacation tool for dbmail
#
#

import os,sys,string,email,getopt,shelve,time,re,smtplib

from dbmail.dbmail import DbmailAutoreply


def usage():
    print """autoreplier: dbmail autoreply replacement
    -u <username>   --username=<username>               specify recipient
    -a <alias>      --alias=<alias>                     specify matching destination address
"""

error='AutoReplyError'

class AutoReplier:
    CACHEDIR="/var/cache/dbmail"
    TIMEOUT=3600*24*7
    OUTHOST="localhost"
    OUTPORT=25

    _username=None
    _messagefile=None

    def __init__(self):
        self.setMessage(email.message_from_file(sys.stdin))
                
    def setUsername(self,_username): self._username=_username
    def getUsername(self): return self._username
        
    def setMessage(self,_message): self._message=_message
    def getMessage(self): return self._message

    def setReply(self): self._reply=DbmailAutoreply(self.getUsername()).getReply()
    def getReply(self): return email.message_from_string(self._reply)
    
    def setAlias(self,_alias): self._alias=_alias
    def getAlias(self): return self._alias
        
    def openCache(self):
        file=os.path.join(self.CACHEDIR,"%s.db" % self.getUsername())
        self.cache=shelve.open(file,writeback=True)
        
    def closeCache(self): self.cache.close()

    def checkSender(self,bounce_senders=[]):
        for f in ('From',):
            if self.getMessage().has_key(f):
                header=string.lower(self.getMessage()[f])
                for s in bounce_senders:
                    if string.find(header,s) >= 0:
                        return True
        return False
                
    def checkDestination(self):
        for f in ('To','Cc'):
            if self.getMessage().has_key(f):
                header=string.lower(self.getMessage()[f])
                if string.find(header,self.getAlias()) >=0:
                    return True
        return False
        
    def send_message(self,msg):
        server=smtplib.SMTP(self.OUTHOST,self.OUTPORT)
        server.sendmail(msg['From'],msg['To'],msg.as_string())
        server.quit()

    def do_reply(self):
        m=self.getMessage()
        u=self.getUsername()
        if m.has_key('Reply-to'): to=m['Reply-to']
        elif m.has_key('From'): to=m['From']
        else: raise error, "No return address"

        if self.checkSender(['daemon','mailer-daemon','postmaster']):
            return
        if not self.checkDestination():
            return 
            
        if not self.cache.has_key(u):
            self.cache[u]={}
        if not self.cache[u].has_key(to) or self.cache[u][to] < int(time.time())-self.TIMEOUT:
            replymsg=self.getReply()
            print replymsg
            replymsg['To']=to
            replymsg['From']=self.getAlias()
	    body=replymsg.get_payload()
	    body="%s\n---\n\n%s\n" % ( body, self.getAlias() )
	    replymsg.set_payload(body)
            self.send_message(replymsg)
            self.cache[u][to]=int(time.time())
        
    def reply(self):
        self.openCache()
        self.do_reply()
        self.closeCache()

if __name__ == '__main__':
    try:
        opts,args = getopt.getopt(sys.argv[1:],"u:m:a:", 
            ["username=","alias="])
    except getopt.GetoptError:
        usage()
        sys.exit(0)
        
    replier=AutoReplier()
    
    for o,a in opts:
        if o in ('-u','--username'):
            replier.setUsername(a)
            replier.setReply()
        if o in ('-a','--alias'):
            replier.setAlias(a)

    replier.reply()

    
    
