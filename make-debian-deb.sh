#!/bin/sh
# 
# File: make-deb.sh
# Date: 2022-Oct-26
# By  : Kevin L. Esteb
#
# A script to build debian .deb files.
#
cd debian
./create-defaults.sh
./create-dbmail-conf.sh
./create-sqlite-db.sh
cd ..
#
dpkg-buildpackage -rfakeroot -us -uc
#
