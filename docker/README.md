# DBMail Docker

An experimental [docker image](https://hub.docker.com/r/alanhicks/dbmail)
is available, this article documents how to use it with docker-compose.

The aim is for this to become an official dbmail image.

Using the Docker philosophy, there is a single dbmail image that is used for
imap, lmtp and sieve. This image is intended to be configured and run using
compose.yaml.

Default values for auto_notify and auto_reply are no, and are not currently supported in this docker image.

SIEVE notify, redirect and vacation are not currently supported in this docker image.

All the files can be found on GitHub.

## DBMail applications

DBMail is a collection of apps that work together to deliver an IMAP service.
dbmail-deliver and dbmail-lmtp for delivery, dbmail-sieve for managing SIEVE
automation and dbmail-imapd for the IMAP service.

As good Docker practice is to build a container per service, there is a docker compose.yaml for managing the three services.

These services are generated from the dbmail:latest image using compose.yaml.

## Build

The dbmail image is created with the Dockerfile:

    docker image build --file docker/Dockerfile --tag dbmail:latest .

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

DBMail doesn't output logs via docker log.

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

