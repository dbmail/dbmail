#!/usr/bin/python

import os,sys,string

unimplementedError = 'Dbmail unimplemented'
configError = 'Dbmail configuration error'

class DbmailConfig:
    _file="/etc/dbmail.conf"
    _config={}
    _prefix='dbmail_'
    
    def __init__(self,file=None):
        if file: self.setFile(file)
        assert(os.path.isfile(self.getFile()))
        self._parse()
        
    def setFile(self,_file): self._file=_file
    def getFile(self): return self._file

    def _parse(self):
        lines=open(self.getFile()).readlines()
        #strip cruft
        lines=map(lambda x: string.split(x,'#')[0],lines)
        lines=map(lambda x: string.strip(x),lines)
        lines=filter(lambda x: len(x)>0 and x[0] not in ('\n'),lines)
        #split stanzas and attributes
        for l in lines:
            k=v=None
            if l[0]=='[': 
                stanza=l[1:-1]
                self._config[stanza]={}
                continue
            else: 
                try:
                    k,v=string.split(l,'=',1)
                    k = string.lower(k)
                except:
                    print "[%s]" % string.split(l,'=')
                self._config[stanza][k]=v
    
    def _getConfig(self,stanza=None,key=None):
        if stanza:
            if key: 
                try:
                    key=string.lower(key)
                    return self._config[stanza][key]
                except KeyError:
                    raise configError, 'missing key [%s] for stanza [%s]' % (key,stanza)

            else: return self._config[stanza]
        return self._config

    getConfig=_getConfig
 
