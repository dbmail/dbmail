#!/usr/bin/python

# number of concurrent lmtp clients to create
THREADS = 20

# number of messages to send per lmtp client
MESSAGES = 100


import smtplib, thread, time, sys

msg="""To: user@dom
Subject: =?ISO-8859-2?Q?E-mail=20noviny=2016/06:=20Vli?=
 =?ISO-8859-2?Q?v=20p=F8=EDjm=F9=20ze=20zam=ECstn=E1n=ED=20n?=
 =?ISO-8859-2?Q?a=20z=E1lohy=20fyzick=FDch=20osob?=
Sender: =?ISO-8859-2?Q? "Verlag=20Dash=F6fer=20-=20DU.cz?=
        =?ISO-8859-2?Q?"
        ?= <e-noviny@smtp.dashofer.cz>
Reply-To: =?ISO-8859-2?Q?Verlag=20Dash=F6fer=20?= <e-noviny@dashofer.cz>
X-Spam-Level: *******
From: =?ISO-8859-2?Q? "Verlag=20Dash=F6fer=20-=20DU.cz?= =?ISO-8859-2?Q?"
        ?=
        <e-noviny@smtp.dashofer.cz

test

"""

DEBUG=False

tlocks = {}
tdict = {}

class LMTPClient:
    def __init__(self, hostname, port):
        self._hostname = hostname
        self._port = port
        self.conn = smtplib.SMTP(self._hostname, self._port)
        self.conn.set_debuglevel(DEBUG)

    def lhlo(self, hostname):
        return self.conn.docmd('LHLO', hostname)

    def send(self, fromaddr, toaddrs, message):
        self.conn.mail(fromaddr)
        self.conn.rcpt(toaddrs)
        self.conn.data(message)
        self.conn.rset()

    def quit(self):
        return self.conn.quit()

def frontloader(*args):
    tid = args[0]
    tlocks[tid].acquire()
    c = LMTPClient('localhost',10024)
    c.lhlo('host')
    for i in range(1,MESSAGES):
        c.send('nobody@nowhere.org','testuser1',msg)
        sys.stdout.write('.')
        sys.stdout.flush()

    c.quit()
    tlocks[tid].release()

if __name__ == '__main__':
    # start the client threads
    for i in range(0,THREADS):
        tlocks[i] = thread.allocate_lock()

    for i in range(0,THREADS):
        id = thread.start_new_thread(frontloader, (i,))
        tdict[i] = id
        print "thread %d started [%d]" % (i,id)

    # wait for the clients to finish
    while 1:
        for i in range(0,THREADS):
            done = []
            if tdict.has_key(i):
                r = tlocks[i].acquire(0)
                if r: 
                    print "thread %d done [%d]" % (i, tdict[i])
                    tlocks[i].release()
                    done.append(i)
            for x in done:
                del(tlocks[x])
                del(tdict[x])
        if len(tdict.items()) == 0:
            break
        time.sleep(1)


