FROM alpine:edge AS build-image

ADD . /app
COPY docker/etc/ /etc/
WORKDIR /app
RUN apk add --no-cache musl=1.1.22-r0 libc-dev libzdb-dev gcc curl make gmime-dev libmhash-dev libsieve-dev libevent-dev bsd-compat-headers check-dev pkgconf
RUN ./configure --prefix=/usr --with-sieve=/usr --sysconfdir=/etc/dbmail --enable-static=no \
	--enable-shared=yes --with-check=/usr  && make && CK_FORK=no make check && make install

FROM alpine:edge
COPY docker/etc/ /etc/
WORKDIR /app
RUN apk add --no-cache musl=1.1.22-r0 libzdb gmime libmhash libsieve libevent
COPY --from=build-image /usr/sbin/dbmail* /usr/sbin/
COPY --from=build-image /usr/lib/dbmail/ /usr/lib/dbmail/

EXPOSE 24
EXPOSE 143
EXPOSE 110
EXPOSE 2000


CMD ["sh"]
