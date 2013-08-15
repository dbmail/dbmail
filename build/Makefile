
default: buildout

buildout: bin/buildout
	bin/buildout

bin/buildout: bin/python
	test -d ./bootstrap.py || wget http://downloads.buildout.org/2/bootstrap.py
	bin/python ./bootstrap.py
	rm -f ./bootstrap.py

bin/python:
	virtualenv .
