# DBMail

[![Build Status](https://travis-ci.org/dbmail/dbmail.svg?branch=master)](https://travis-ci.org/dbmail/dbmail)

> Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk
>
> Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
>
> Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
>
> Copyright (c) 2000-2006 IC&S, The Netherlands

## What is it?

DBMail is a collection of programs that enables email to be stored in and
retrieved from a database. 

Read the INSTALL file for installation of DBMail.

## Why is it useful?

- it enables you to create mailboxes without the need of systemusers.

- Email is more effeciently stored and therefore it can be inserted an retrieved much faster than any
  regular system (DBMail is currently able to retrieve aprox. 250 mail messages per second)

- It's scalable. You can run the dbmail programs on different servers talking to the same
  database(cluster). LDAP integration allows you to integrate with existing authentication
  backends, such as ActiveDirectory.

- There's no need to maintain system users or write access to the filesystem. All this
  is done through the database.


## Who created it?

DBMail was originally created by IC&S in the Netherlands.

Around 2003, Paul Stevens at NFG also joined the development team, initially to provide debian
packaging, later to take over development of the 2.1 release. Aaron Stone also deserves special
thanks for initiating the LDAP driver, maintaining the delivery chain and of course sieve support.

DBMail is now a community effort to create a fast, effecient and scalable database driven
mailingsystem. Both IC&S and NFG are fully behind opensource and the GPL. Therefore DBMail has the
GPL licence.

Support is available by raising an issue at https://github.com/dbmail/dbmail

## How do i install it?

Check the different README files and on https://dbmail.org for detailed
information and howtos.

## Future

Check the website for further DBMail plans.

## What kind of licence is DBMail?

DBMail uses the GPL version 2 licence. 

It's included in the COPYING file.


## Installation Procedure

### Dependencies

* Database: Current versions of MySQL, PostgreSQL, Sqlite3 and Oracle
* Glib: (>= 2.16)
* GMime: (>= 3 (3.2))
* OpenSSL
* libmhash
* libzdb (http://www.tildeslash.com/libzdb/)
* libevent: (>= 2.1)
* Optional: libsieve (>= 2.2.1) (https://github.com/sodabrew/libsieve)
* Optional: jemalloc

### Installing

* Download DBMail package
* Install dependencies (some provided from your linux distribution and some (libzdb and/or libsieve) need to be compiled
* ./configure
* make 
* make install
