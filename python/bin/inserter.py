#!/usr/bin/python2.2

""" Simple python-based inserter for dbmail <www.dbmail.org>
"""

import email, sys, MySQLdb

testfile = "/home/paul/message.eml"

msg = email.message_from_file(open(testfile))
for part in msg.walk():
    print part.get_content_type()
