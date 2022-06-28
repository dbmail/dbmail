# ISSUE: https://gitlab.alpinelinux.org/alpine/aports/-/issues/12519
FROM alpine:latest AS build-base
RUN df
#users
RUN addgroup root abuild

RUN apk add --no-cache alpine-sdk sudo su-exec

#RUN set -xe \
#    ; mkdir -p /var/cache/distfiles \
#    ; chmod a+w /var/cache/distfiles \
#    ; chgrp abuild /var/cache/distfiles \
#    ; abuild-keygen -a -i

	
RUN set -xe
RUN mkdir -p /var/cache/distfiles
RUN chmod a+w /var/cache/distfiles
RUN chgrp abuild /var/cache/distfiles
RUN adduser abuild -G abuild; \
    abuild-keygen -ai

	
env \
    PKGDEST=/root/packages/ \
    REPODEST=/root/packages/
RUN mkdir -pv ${PKGDEST}

####
FROM build-base AS build-libsieve
ARG libsieve_APKBUILD=https://gitlab.alpinelinux.org/alpine/aports/-/raw/3.6-stable/unmaintained/libsieve/APKBUILD
ADD ${libsieve_APKBUILD} /build/APKBUILD
WORKDIR /build
RUN abuild -F deps
RUN abuild -F fetch
RUN abuild -F verify
RUN abuild -F unpack
RUN abuild -F prepare
RUN abuild -F build
RUN abuild -F rootpkg
RUN abuild -F rootbld
RUN abuild -F package

####
FROM build-base AS build-gmime
ARG gmime_APKBUILD=https://gitlab.alpinelinux.org/alpine/aports/-/raw/3.6-stable/main/gmime/APKBUILD
ADD ${gmime_APKBUILD} /build/APKBUILD
WORKDIR /build
RUN abuild -F deps
RUN abuild -F fetch
RUN abuild -F verify
RUN abuild -F unpack
RUN abuild -F prepare
RUN abuild -F build
RUN abuild -F rootpkg
RUN abuild -F rootbld
RUN abuild -F package

####
FROM build-base AS build-libzdb
ARG libzdb_APKBUILD=https://gitlab.alpinelinux.org/alpine/aports/-/raw/3.6-stable/main/libzdb/APKBUILD
ARG libzdb_PATCH=https://gitlab.alpinelinux.org/alpine/aports/-/raw/3.6-stable/main/libzdb/test-makefile.patch
ADD ${libzdb_APKBUILD} /build/APKBUILD
ADD ${libzdb_PATCH} /build/test-makefile.patch
WORKDIR /build
RUN abuild -F deps
RUN abuild -F fetch
RUN abuild -F verify
RUN abuild -F unpack
RUN abuild -F prepare
RUN abuild -F build
RUN abuild -F rootpkg
RUN abuild -F rootbld
RUN abuild -F package

####
FROM alpine:latest AS base-image

ADD . /app
COPY docker/etc/ /etc/
WORKDIR /app
RUN apk add --no-cache libc-dev libmhash libevent file
# libzdb
# libsieve
# gmime=3.2


COPY --from=build-base /root/.abuild/ /root/.abuild/

ARG LIBSIEVE_VERSION=2.2.7-r1
COPY --from=build-libsieve /root/packages/x86_64/libsieve-${LIBSIEVE_VERSION}.apk /root/packages/x86_64/libsieve-${LIBSIEVE_VERSION}.apk
RUN apk add --allow-untrusted --no-cache /root/packages/x86_64/libsieve-${LIBSIEVE_VERSION}.apk

ARG GMIME_VERSION=3.2.7-r2
COPY --from=build-gmime /root/packages/x86_64/gmime-${GMIME_VERSION}.apk /root/packages/x86_64/gmime-${GMIME_VERSION}.apk
RUN apk add --allow-untrusted --no-cache /root/packages/x86_64/gmime-${GMIME_VERSION}.apk

ARG LIBZDB_VERSION=3.1-r1
COPY --from=build-libzdb /root/packages/x86_64/libzdb-${LIBZDB_VERSION}.apk /root/packages/x86_64/libzdb-${LIBZDB_VERSION}.apk
RUN apk add --allow-untrusted --no-cache /root/packages/x86_64/libzdb-${LIBZDB_VERSION}.apk

####
FROM base-image AS build-image
WORKDIR /app
RUN apk add --no-cache libc-dev gcc curl make libmhash-dev libevent-dev bsd-compat-headers check-dev pkgconf libtool m4 automake autoconf build-base

ARG LIBSIEVE_VERSION=2.2.7-r1
COPY --from=build-libsieve /root/packages/x86_64/libsieve-dev-${LIBSIEVE_VERSION}.apk /root/packages/x86_64/libsieve-dev-${LIBSIEVE_VERSION}.apk
RUN apk add --allow-untrusted --no-cache /root/packages/x86_64/libsieve-dev-${LIBSIEVE_VERSION}.apk

ARG GMIME_VERSION=3.2.7-r2
COPY --from=build-gmime /root/packages/x86_64/gmime-dev-${GMIME_VERSION}.apk /root/packages/x86_64/gmime-dev-${GMIME_VERSION}.apk
RUN apk add --allow-untrusted --no-cache /root/packages/x86_64/gmime-dev-${GMIME_VERSION}.apk

ARG LIBZDB_VERSION=3.1-r1
COPY --from=build-libzdb /root/packages/x86_64/libzdb-dev-${LIBZDB_VERSION}.apk /root/packages/x86_64/libzdb-dev-${LIBZDB_VERSION}.apk
RUN apk add --allow-untrusted --no-cache /root/packages/x86_64/libzdb-dev-${LIBZDB_VERSION}.apk

RUN mkdir -p /etc/dbmail
RUN chmod a+w -R /app 
RUN chgrp root /app 

RUN ./configure \
        --prefix=/root \
        --with-sieve=/usr \
        --sysconfdir=/etc/dbmail \
        --enable-static=no \
		--enable-shared=yes \
		--disable-libtool-lock \
		--disable-dependency-tracking \
		--disable-systemd \
        --with-check=/usr
	
RUN make all
ARG CK_FORK=no
RUN make check
RUN make install

####
FROM base-image
COPY --from=build-image /root/sbin/dbmail* /usr/sbin/
COPY --from=build-image /root/lib/dbmail/ /usr/lib/dbmail/

EXPOSE 24
EXPOSE 143
EXPOSE 110
EXPOSE 2000


CMD ["sh"]
