#!/usr/bin/python

# default host
HOST = "localhost"

# default port
PORT = 10024

# default number of concurrent lmtp clients to create
CLIENTS = 20

# default number of messages to send per lmtp client
MESSAGES = 100

# default mailbox
MAILBOX = "testbox"

# username
USERNAME = "testuser1"

# number of messages to send per session
RECONNECT = 5

import smtplib
import thread
import time
import sys
import mailbox
import string
from optparse import OptionParser

DEBUG = False

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
        r = self.conn.mail(fromaddr)
        assert(r[0] == 250)
        r = self.conn.rcpt(toaddrs)
        assert(r[0] == 250)
        r = self.conn.data(message)
        assert(r[0] == 215)
        r = self.conn.rset()
        assert(r[0] == 250)

    def quit(self):
        return self.conn.quit()


def frontloader(*args):
    tid = args[0]
    tlocks[tid].acquire()
    c = LMTPClient(HOST, PORT)
    r = c.lhlo('host')
    assert(r[0] == 250)
    mb = mailbox.mbox(MAILBOX, factory=None, create=False)
    i = 1
    while i < MESSAGES:
        for msg in mb.values():
            addr = string.split(msg.get_from())[0]
            c.send(addr, USERNAME, msg.as_string())
            if not i % RECONNECT:
                c.quit()
                c = LMTPClient(HOST, PORT)
                c.lhlo('host')
                sys.stdout.write('_')
            else:
                sys.stdout.write('.')
            sys.stdout.flush()
            i = i + 1
            if i >= MESSAGES:
                break

    c.quit()
    tlocks[tid].release()

if __name__ == '__main__':

    parser = OptionParser()
    parser.add_option("-H", "--host", dest="HOST",
                      help="Hostname to connect to [default: %default]",
                      default=HOST)
    parser.add_option("-P", "--port", dest="PORT",
                      help="Port to connect to [default: %default]",
                      default=PORT)
    parser.add_option("-c", "--clients", dest="CLIENTS",
                      help="Number of concurrent clients [default: %default]",
                      default=CLIENTS)
    parser.add_option("-m", "--mailbox", dest="MAILBOX",
                      help="mailbox to feed to LMTP [default: %default]",
                      default=MAILBOX)
    parser.add_option("-n", "--messages", dest="MESSAGES",
                      default=MESSAGES,
                      help="number of messsages clients sends [default: %default]")
    parser.add_option("-u", "--username", dest="USERNAME",
                      default=USERNAME,
                      help="deliver to username [default: %default]")
    parser.add_option("-r", "--reconnect", dest="RECONNECT",
                      default=RECONNECT,
                      help="Number of messages to send before " \
                        "reconnecting [default: %default]")

    (options, args) = parser.parse_args()

    HOST = options.HOST
    PORT = int(options.PORT)
    CLIENTS = int(options.CLIENTS)
    MESSAGES = int(options.MESSAGES)
    MAILBOX = options.MAILBOX
    USERNAME = options.USERNAME
    RECONNECT = int(options.RECONNECT)

    # start the client threads
    for i in range(0, CLIENTS):
        tlocks[i] = thread.allocate_lock()

    print "Connect to LMTP server: %s:%d" % (HOST, PORT)
    print "Starting %d clients" % CLIENTS
    print "Deliver %d messages per client to %s" % (MESSAGES, USERNAME)
    print "Use messages from %s" % MAILBOX

    for i in range(0, CLIENTS):
        id = thread.start_new_thread(frontloader, (i,))
        tdict[i] = id
        time.sleep(1)

    time.sleep(5)
    # wait for the clients to finish
    while 1:
        for i in range(0, CLIENTS):
            done = []
            if i in tdict:
                r = tlocks[i].acquire(0)
                if r:
                    sys.stdout.write('Q')
                    sys.stdout.flush()
                    tlocks[i].release()
                    done.append(i)
            for x in done:
                del(tlocks[x])
                del(tdict[x])
        if len(tdict.items()) == 0:
            break
        time.sleep(1)


#EOF
