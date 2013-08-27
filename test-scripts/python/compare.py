
import time
import sys
import os
import hashlib
import tempfile
import imaplib
import poplib
from multiprocessing import Pool


HOST = 'localhost'
USER = 'testuser1'
PASS = 'test'
CLIENTS = 30

IMAP_PORT = 10143
POP3_PORT = 10110

tlocks = {}
tdict = {}

IMAP_BASELINE = None
POP3_BASELINE = None


def imap_messages():
    IMAP = imaplib.IMAP4(HOST, IMAP_PORT)
    IMAP.login(USER, PASS)
    IMAP.select('INBOX')
    result = IMAP.fetch('1:*', '(RFC822)')
    IMAP.logout()
    result = [x for x in result[1] if type(x) == tuple]
    result = [x[1] for x in result]
    result = [x.replace('\r', '') for x in result]
    return [(hashlib.sha1(x).hexdigest(), x) for x in result]


def pop_messages():
    POP = poplib.POP3(HOST, POP3_PORT)
    POP.user(USER)
    POP.pass_(PASS)
    l = POP.list()
    count = int(l[0].split()[1])
    result = []
    for i in range(1, count + 1):
        message = POP.retr(i)
        message = '\n'.join(message[1])
        message.replace('\r', '')
        result.append((hashlib.sha1(message).hexdigest(), message))
    return result


def do_compare(left, right):
    assert(len(left) == len(right))
    for i in range(0, len(left)):
        left_row = left[i]
        right_row = right[i]
        if left[0] != right[0]:
            return "checksum error at [%d] %s != %s\n" % (
                            i, left[0], right[0])
    return


def imaploader(*args):
    for i in range(0, 10):
        try:
            results = imap_messages()
        except:
            continue
        error = do_compare(IMAP_BASELINE, results)
        if error:
            return error

        sys.stdout.write('.')
        sys.stdout.flush()
    return


def pop3loader(*args):
    for i in range(0, 10):
        results = pop_messages()
        do_compare(POP3_BASELINE, results)
        sys.stdout.write(',')
        sys.stdout.flush()
    return


if __name__ == '__main__':
    print "fetch all messages over IMAP..."
    IMAP_BASELINE = imap_messages()
    print "fetch all messages over POP3..."
    POP3_BASELINE = pop_messages()
    print "comparing results for %s messages..." % len(IMAP_BASELINE)
    do_compare(IMAP_BASELINE, POP3_BASELINE)

    pool = Pool(processes=CLIENTS * 2)
    pop3_result = pool.map_async(pop3loader, range(0, CLIENTS))
    imap_result = pool.map_async(imaploader, range(0, CLIENTS), 1)
    pop3_errors = pop3_result.get()
    imap_errors = imap_result.get()
    pool.close()
    pool.join()

    sys.stdout.write('\n')
    pop3_errors = [x for x in pop3_errors if x]
    imap_errors = [x for x in imap_errors if x]
    if pop3_errors:
        print "POP3 errors", pop3_errors
    if imap_errors:
        print "IMAP errors", imap_errors
    if imap_errors or pop3_errors:
        sys.exit(1)
    print "done."
    sys.exit(0)
