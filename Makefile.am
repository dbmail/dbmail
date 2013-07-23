ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src test

if MANPAGES
SUBDIRS += man
endif

testall:
	CK_FORK=no make check
	imaptest user=testuser1 pass=test port=10143 host=localhost test=test-scripts/imap
