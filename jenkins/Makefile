
default: buildout

buildout: bin/buildout
	bin/buildout

bin/buildout: bin/python
	bin/python ./bootstrap.py

bin/python:
	virtualenv .

test: buildout
	bin/supervisorctl shutdown || true
	sleep 3
	bin/supervisord && sleep 3 
	(cd ../ && CK_FORK=no make check)
	sbin/dbmail-users -c testuser1 -w test -p plaintext || true
	sbin/dbmail-users -c testuser2 -w test -p plaintext || true
	imaptest user=testuser1 pass=test port=10143 host=localhost test=../test-scripts/imap
	bin/py ../test-scripts/testimap.py -v
	bin/py ../test-scripts/testpop.py -v
	bin/py ../test-scripts/testlmtp.py
	timeout 5m bin/py ../test-scripts/loadimap.py
	timeout 5m bin/py ../test-scripts/loadpop.py


.PHONY: test

