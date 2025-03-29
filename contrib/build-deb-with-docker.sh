#!/bin/sh

docker rm -f dbmail-build-deb 2>/dev/null
docker run --name dbmail-build-deb debian:stable /bin/sh -c '
  apt-get update &&\
  apt-get -y dist-upgrade &&\
  apt-get -y install git automake automake1.11 autoconf debhelper\
          libncurses5-dev libldap2-dev libtool asciidoc xmlto po-debconf\
          default-libmysqlclient-dev libpq-dev libsqlite3-dev libsieve2-dev\
          libglib2.0-dev libzdb-dev libmhash-dev libevent-dev\
          pkg-config libssl-dev cdbs libgmime-3.0-dev libjemalloc-dev libcurl4-openssl-dev &&\
  mkdir /build-dir &&\
  cd /build-dir &&\
  git clone -b 3.5.x https://github.com/dbmail/dbmail.git &&\
  cd dbmail &&\
  dpkg-buildpackage -us -uc -d &&\
  cd /build-dir &&
  tar -cvf /dbmail-deb-build-result.tar dbmail_*.deb' &&
docker cp dbmail-build-deb:/dbmail-deb-build-result.tar ./ &&
tar -xvf dbmail-deb-build-result.tar &&
rm dbmail-deb-build-result.tar &&
docker rm dbmail-build-deb
