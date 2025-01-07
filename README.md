# DBMail

> Copyright (c) 2020-2025 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk
>
> Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
>
> Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
>
> Copyright (c) 2000-2006 IC&S, The Netherlands

## What is it?

DBMail is a collection of programs that enables email to be managed, stored in
and retrieved from a database using industry standard IMAP.

## Why is it useful?

- Securely and scaleably manages user emails with industry standard IMAP;

- Integrates with existing authentication backends including ActiveDirectory
  and OpenLDAP;

- DBMail is scalable including multiple terrabyte installations;

- DBMail is flexible. You can run dbmail programs on different servers talking
  to the same database;

- Email filtering is integrated into DBMail and managed using SIEVE;

- Schema upgrades are automatic with SQL provided for DBAs who wish to upgrade
  manually;

- Easy to use and flexible logging for each service;

- An experimental Docker image is available;

- High Availability thanks to database replication and Docker images;

- Data safety thanks to database replication;

- Secure connections thanks to TLS;

- DBMail is database driven so no need of systemusers;

- No need to maintain system users or write access to the filesystem;

- DBMail is Free with a recognised GPL2 Open Source licence.

## Who created it?

DBMail was originally created by IC&S in the Netherlands.

Around 2003, Paul Stevens at NFG also joined the development team, initially to
provide debian packaging, later to take over development of the 2.1 release.
Aaron Stone also deserves special thanks for initiating the LDAP driver,
maintaining the delivery chain and of course sieve support.

DBMail is now a community effort to create a fast, effecient and scalable
database driven mailingsystem. Both IC&S and NFG are fully behind opensource
and the GPL. Therefore DBMail has the GPL licence.

## Support

Support is available by raising an issue at https://github.com/dbmail/dbmail

For the latest announcements, please subscribe to the following
https://dbmail.org/wws/info/dbmail-announce

General questions may be answered on the mailing list
https://dbmail.org/wws/info/dbmail-general

### Architecture

For an architectural overview of DBMail and its components please visit:
https://dbmail.org/architecture/

## Future

Check the website for further DBMail plans.

## What kind of licence is DBMail?

DBMail uses the GPL version 2 licence. 

It's included in the COPYING file.

## How do I install it?

DBMail is available on many Linux and BSD distributions.

There is also an experimental
[Docker image](https://hub.docker.com/r/alanhicks/dbmail)
with instructions to configure compose.yaml at
https://dbmail.org/docker/

## Installation Procedure

Check README and INSTALL files and on https://dbmail.org for detailed
information and HOWTOs including how to use the Docker image.

See also contrib, debian and docker on https://github.com/dbmail/dbmail

### Configuration

There are various settings including database access, authentication and
logging. Please see dbmail.conf or https://dbmail.org/man-pages/dbmailconf/

Exim is a modern MTA and there is an example configure at
https://github.com/dbmail/dbmail/blob/main/contrib/exim-dbmail-configure

### Integration

DBMail integrates with most MTAs. There are a number of examples on
https://dbmail.org/docs/

Web frontends including [SquirrelMail](https://squirrelmail.org/) and
[Roundcube](https://roundcube.net/) work well with IMAP using dbmail-imapd
plus Roundcube and most desktop software has integration with email filtering
via dbmail-sieved.

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
* Install dependencies (some provided from your linux / BSD distribution and
  some (libzdb and/or libsieve) need to be compiled
* ./configure
* make 
* make install
