
import sys
import argparse
import time
import imaplib


def bencher(args):
    host = args.host
    port = int(args.port)
    login = args.login
    password = args.password
    count = int(args.count)
    before = time.time()
    for x in range(0, count):
        conn = imaplib.IMAP4(host, port)
        conn.login(login, password)
        conn.logout()
    after = time.time()
    return after - before


if __name__ == '__main__':
    COUNT = 100
    HOST = '127.0.0.1'
    PORT = 10143
    LOGIN = 'testuser1'
    PASSWORD = 'test'

    parser = argparse.ArgumentParser(description='simple IMAP benchmark')
    parser.add_argument('--host', default=HOST)
    parser.add_argument('--port', default=PORT)
    parser.add_argument('--count', default=COUNT)
    parser.add_argument('--login', default=LOGIN)
    parser.add_argument('--password', default=PASSWORD)
    args = parser.parse_args()

    print sys.argv[0]
    print
    print "testing: login/logout"
    print "count: ", args.count
    delay = bencher(args)
    print "time: ", delay


#EOF
