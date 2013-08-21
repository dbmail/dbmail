
import os
import hashlib
import tempfile
import imaplib
import poplib

HOST = 'localhost'
USER = 'testuser1'
PASS = 'test'

IMAP_PORT = 10143
POP3_PORT = 10110


def imap_messages():
    IMAP = imaplib.IMAP4(HOST, IMAP_PORT)
    IMAP.login(USER, PASS)
    IMAP.select('INBOX')
    result = IMAP.fetch('1:*', '(RFC822)')
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


if __name__ == '__main__':
    print "fetch all messages over IMAP..."
    imap = imap_messages()
    print "fetch all messages over POP3..."
    pop = pop_messages()
    print "comparing results for %s messages..." % len(imap)
    assert(len(imap) == len(pop))
    for i in range(0, len(imap)):
        imap_row = imap[i]
        pop_row = pop[i]
        if imap_row[0] != pop_row[0]:
            f1 = tempfile.NamedTemporaryFile(delete=False)
            f2 = tempfile.NamedTemporaryFile(delete=False)
            f1.write(imap_row[1])
            f2.write(pop_row[1])
            f1.close()
            f2.close()

            raise Exception("checksum error at [%d] %s != %s\n[%s]\n[%s]" % (
                            i, imap_row[0], pop_row[0],
                            f1.name, f2.name))
            os.unlink(f1.name)
            os.unlink(f2.name)

    print "done."
