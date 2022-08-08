# Docker images

DBMail Docker support is experimental and is focused on dbmail-3.3.

This docker image is only for dbmail 3.3+

## Configuration

Configuration is currently only available in dbmail.conf, so copy dbmail.conf to docker/dbmail.conf and edit with your configuration. TLS is not included in this version but is easy to add, simply add your certificates in the DBMail container (copy your ca, cert and key into docker/tls/, amend the tls_cafile, tls_cert and tls_key entries in dbmail.conf, then add the following to the Dockerfile.

    COPY docker/tls/* /usr/local/etc/"

You may wish to review logging

    file_logging_levels   = 31
    syslog_logging_levels = 0
    logfile               = /var/log/dbmail-imapd.log
    errorlog              = /var/log/dbmail-imapd.err

## Build

To create the dbmail image use the docker cli as follows:

    docker image build --file docker/Dockerfile --tag dbmail:latest .

The image is based on Deban Jammy and is currently about 180Mb.

## Run

Create and run the container with the docker cli:

    docker run -p 143:143 -p 993:993 dbmail:latest
