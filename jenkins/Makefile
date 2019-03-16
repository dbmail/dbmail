
default: buildout

buildout: bin/buildout
	bin/buildout

bin/buildout: bin/python
	bin/pip install zc.buildout

bin/python:
	virtualenv --clear .

test: buildout test-postgresql test-mysql

test-postgresql:
	sed -i -e 's,^dburi.*,dburi = postgresql://127.0.0.1:5432/dbmail?user=dbmail\&password=dbmail,' ./etc/dbmail.conf
	$(MAKE) run-test

test-mysql:
	sed -i -e 's,^dburi.*,dburi = mysql://127.0.0.1:3306/dbmail?user=dbmail\&password=dbmail\&charset=utf8,' ./etc/dbmail.conf
	$(MAKE) run-test

run-test:
	bin/supervisorctl shutdown || true
	sleep 3
	bin/supervisord && sleep 3 
	(cd ../ && CK_FORK=no make check)
	sbin/dbmail-users -c testuser1 -w test -p plaintext || true
	sbin/dbmail-users -a testuser2 -w test -p plaintext || true
	sbin/dbmail-users -a testuser3 -w test -p sha256 || true
	sbin/dbmail-users -y -e testuser1 || true
	bin/py ../contrib/mailbox2dbmail/mailbox2dbmail -u testuser1 -m ../test-scripts/testbox -t mbox -p sbin/dbmail-deliver
	timeout 300 imaptest user=testuser1 pass=test port=10143 test=../test-scripts/imap
	timeout 300 bin/py ../test-scripts/testimap.py -v
	bin/py ../contrib/mailbox2dbmail/mailbox2dbmail -u testuser1 -m ../test-scripts/testbox -t mbox -p sbin/dbmail-deliver
	timeout 300 bin/py ../test-scripts/testpop.py -v
	timeout 300 bin/py ../test-scripts/python/compare.py
	#timeout 300 bin/py ../test-scripts/python/loadlmtp.py
	#timeout 300 bin/py ../test-scripts/python/loadimap.py
	#timeout 300 bin/py ../test-scripts/python/loadpop.py
	/usr/bin/phpunit ../contrib/testdbmailclient.php
	bin/supervisorctl shutdown || true



.PHONY: test run-test

