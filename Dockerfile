FROM alpine:edge

ADD . /app
COPY docker/etc/ /etc/
WORKDIR /app
RUN apk add --no-cache libc-dev libzdb-dev gcc curl make gmime-dev=2.6.20-r0 libmhash-dev openldap-dev libsieve-dev libevent-dev bsd-compat-headers
RUN ./configure --prefix=/usr --with-ldap=/usr --with-sieve=/usr --mandir=/usr/share/man --sysconfdir=/etc/dbmail \
	--localstatedir=/var/run/dbmail --with-logdir=/var/log/dbmail --infodir=/usr/share/info \
	--with-jemalloc=no
	
RUN make && make install
CMD ["sh"]
