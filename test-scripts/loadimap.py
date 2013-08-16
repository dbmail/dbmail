#!/usr/bin/python

import os
import imaplib
import thread
import time
import sys
import mailbox
from optparse import OptionParser


# default number of concurrent clients to create
CLIENTS = 20

# default number of fetch commands per client
MESSAGES = 100

# default mailbox to append from
MAILBOX = os.path.join(os.path.dirname(__file__), "testbox")

# default imap folder to append to
IMAPBOX = "testbox"

# username
USERNAME = "testuser1"

# password
PASSWORD = "test"

# number of messages to send per session
RECONNECT = 5

DEBUG = False

tlocks = {}
tdict = {}


class IMAPClient:
    def __init__(self, hostname, port):
        self._hostname = hostname
        self._port = port
        self.conn = imaplib.IMAP4(self._hostname, self._port)
        self.conn.debug = DEBUG
        self.conn.login(USERNAME, PASSWORD)
        self.conn.create(IMAPBOX)

    def append(self, message):
        self.conn.append(IMAPBOX, (), "", message)

    def fetch(self):
        self.conn.select(IMAPBOX)
        self.conn.fetch('*', "(UID BODY[])")

    def logout(self):
        return self.conn.logout()


def fillmailbox(*args):
    c = IMAPClient('localhost', 10143)
    mb = mailbox.mbox(MAILBOX, factory=None, create=False)
    for msg in mb.values():
        c.append(msg.as_string())
        sys.stdout.write('.')
        sys.stdout.flush()
    sys.stdout.write('\n')
    sys.stdout.flush()
    c.logout()


def frontloader(*args):
    tid = args[0]
    tlocks[tid].acquire()
    c = IMAPClient('localhost', 10143)
    i = 1
    while i < MESSAGES:
        c.fetch()
        if not i % RECONNECT:
            c.logout()
            c = IMAPClient('localhost', 10143)
            sys.stdout.write('_')
        else:
            sys.stdout.write('.')
        sys.stdout.flush()
        i = i + 1
        if i >= MESSAGES:
            break

    c.logout()
    tlocks[tid].release()


if __name__ == '__main__':

    parser = OptionParser()
    parser.add_option("-c", "--clients", dest="CLIENTS",
        help="Number of concurrent clients [default: %default]",
                      default=CLIENTS)
    parser.add_option("-m", "--mailbox", dest="MAILBOX",
        help="mailbox to feed to IMAP-APPEND",
                      default=MAILBOX)
    parser.add_option("-n", "--messages", dest="MESSAGES",
                      default=MESSAGES,
                      help="number of messages to fetch [default: %default]")
    parser.add_option("-u", "--username", dest="USERNAME",
                      default=USERNAME,
        help="deliver to username [default: %default]")
    parser.add_option("-r", "--reconnect", dest="RECONNECT",
                      default=RECONNECT,
                      help="Number of messages to fetch before " \
                        "reconnecting [default: %default]")

    (options, args) = parser.parse_args()

    CLIENTS = int(options.CLIENTS)
    MESSAGES = int(options.MESSAGES)
    MAILBOX = options.MAILBOX
    USERNAME = options.USERNAME
    RECONNECT = int(options.RECONNECT)

    # start the client threads
    for i in range(0, CLIENTS):
        tlocks[i] = thread.allocate_lock()

    if MAILBOX:
        print "Add messages from file %s to imap folder %s" % (
            MAILBOX, IMAPBOX)
        fillmailbox()

    print "Doing %d fetches per client as %s" % (MESSAGES, USERNAME)
    print "Starting %d clients" % CLIENTS
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
