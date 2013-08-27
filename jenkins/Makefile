
default: buildout

buildout: bin/buildout
	bin/buildout

bin/buildout: bin/python2.7
	bin/python ./bootstrap.py

bin/python2.7:
	virtualenv --python=python2.7 --clear .

test: buildout
	bin/supervisorctl shutdown || true
	sleep 3
	bin/supervisord && sleep 3 
	(cd ../ && CK_FORK=no make check)
	sbin/dbmail-users -c testuser1 -w test -p plaintext || true
	sbin/dbmail-users -y -e testuser1 || true
	bin/py ../contrib/mailbox2dbmail/mailbox2dbmail -u testuser1 -m ../test-scripts/testbox -t mbox -p sbin/dbmail-deliver
	timeout 300 imaptest user=testuser1 pass=test port=10143 test=../test-scripts/imap
	timeout 300 bin/py ../test-scripts/python/testimap.py -v
	bin/py ../contrib/mailbox2dbmail/mailbox2dbmail -u testuser1 -m ../test-scripts/testbox -t mbox -p sbin/dbmail-deliver
	timeout 300 bin/py ../test-scripts/python/testpop.py -v
	timeout 300 bin/py ../test-scripts/python/compare.py
	#timeout 300 bin/py ../test-scripts/python/loadlmtp.py
	#timeout 300 bin/py ../test-scripts/python/loadimap.py
	#timeout 300 bin/py ../test-scripts/python/loadpop.py
	bin/supervisorctl shutdown || true


.PHONY: test

