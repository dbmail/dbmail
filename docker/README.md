# DBMail Docker

DBMail can be built and run in a container using Docker for virtualization
making managing DBMail instances easier. Both Jails on BSD and Docker on
Linux work well. This readme is for Linux containers using Docker.

DBMail aims to be platform agnostic though currently only amd64 is supported,
it is developed on and in production using FreeBSD and runs successfully in
FreeBSD Jails though they are outside the scope of this readme.

## Building DBMail with Docker

There are two uses for Docker containers, building an image for use in
production and for testing and development.

All the images are broadly the same except production images are for a stated
version and include tests to ensure a successful build, and development
versions where tests are not run automatically ensuring a successful build
even when tests might fail.

Although builds try to be similar there are differences and some dependencies
may be built from source.

### Dockerfile naming convention

The Dockerfile naming format is as follows:

Dockerfile-\<architecture\>-\<distro\>-\<version\>

Production: Dockerfile-\<architecture\>-\<distro\>-\<version\>

Example: Dockerfile-amd64-ubuntu-3.5

Development: Dockerfile-\<architecture\>-\<distro\>-devel

Example: Dockerfile-amd64-alpine-devel

Named versions are downloaded from GitHub, development versions use the current
source.

### Named version for production
To build an ubuntu production image for version 3.5, git checks out the latest
version of a version branch, tagging the image with v3.5. The image is checked
(make check) to ensure all tests pass before committing the image.

    docker image build --file ./Dockerfile-amd64-ubuntu-350 --tag dbmail:v3.5 .

### Development version from source
Source files are used to build the image. Tests should be run manually and are
omitted from the Dockerfile to ensure the image is built.

To make debugging a build easier there is no caching and progress is plain to
facilitate logging. Note the target is the repo source ..

    docker image build --file ./Dockerfile-amd64-ubuntu-devel --tag dbmail:devel --no-cache --progress=plain .. 2> build.txt

After the image has been built, run the image starting with with a bash shell
prompt. Change to the dbmail build directory /app/dbmail then run make check
to run the automatic tests. You can find the results in test/test-suite.log

    docker run --rm -it dbmail:devel bash

Then inside the container:

    cd /app/dbmail
    make check
    less test/test-suite.log

## Using a docker image
An experimental [docker image](https://hub.docker.com/r/alanhicks/dbmail)
is available, the following shows how to use it with docker-compose.

The aim is for this to become an official dbmail image.

Using the Docker philosophy, there is a single dbmail image that is used for
imap, lmtp and sieve. This image is intended to be configured and run using
compose.yaml.

All the files can be found on GitHub.

## DBMail applications

DBMail is a collection of apps that work together to deliver an IMAP service.
dbmail-deliver and dbmail-lmtp for delivery, dbmail-sieve for managing SIEVE
automation and dbmail-imapd for the IMAP service.

As good Docker practice is to build a container per service, there is a docker
compose.yaml for managing the three services.

These services are generated from the dbmail:latest image using compose.yaml.

## Configuration

Including any sensitive information in a docker image is inadvisable.
Docker Compose provides a way to use secrets without having to use
environment variables to store information, these files are copied
into /var/run/secrets/.

DBMail requires access to database credentials stored in dbmail.conf,
Transport Layer Security requires x509 certificates and private keys.
These are protected using docker compose secrets.

The complete dbmail configuration file should be included in the
secrets:dbmail.conf:file entry in compose.yaml, the following DBMAIL
entries are of particular note:

* dburi
* authdriver
* errorlog = /var/log/dbmail/dbmail.err
* syslog_logging_levels
* file_logging_levels = 31
* syslog_logging_levels = 0
* library_directory = /usr/local/lib/dbmail
* tls_cafile = /var/run/secrets/ca.crt
* tls_cert = /var/run/secrets/cert.pem
* tls_key = /var/run/secrets/key.pem

If you use Lightweight Directory Access Protocol for authentication
(authdriver=ldap) you also need to configure the LDAP section.

## Logging

DBMail doesn't yet output logs via docker log.

The following can be used to view the log and manage its size. DBMail is a
non-interactive process, workarounds from Apache and Nginx are unsuitable.

View the log file:

    docker exec -it [container] tail -f /var/log/dbmail/dbmail.err

To avoid disk-exhaustion, run the following to truncate the error log to
about 1k:

    docker exec -it [container] truncate -s '<1k' /var/log/dbmail/dbmail.err

## Security

DBMail requires various sensitive information that must be protected. The
mechanism used is docker compose secrets.

DBMail uses Transport Layer Security (TLS) with X.509 public-key
certificates to securely communicate with clients such as Microsoft
Outlook, Mozilla Thunderbird, K-9 Mail, RoundCube and SquirrelMail.

In order to establish secure communications dbmail needs access to the
certificate authority's public certificate (tls_cafile), your public
certificate (tls_cert) and your private key (tls_key). It's your private
key that needs protecting and your public certificate that needs to be
updated every time it's renewed. The authority's public certificate is
rarely changed but should be included in any updates.

Include passwords for database access and LDAP if you use it for
authentication.

The four sensitive files configured using docker compose secrets are:

* dbmail.conf - the dbmail configuration file;
* ca.crt - the certificate authority;
* cert.pem - your public certificate;
* key.pem - your private key.
