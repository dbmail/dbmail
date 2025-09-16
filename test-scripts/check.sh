#!/bin/sh

# Pre release integration test script
# Checks daemons for valgrind and functions

SERVICES="imap lmtp"

HOST="localhost"
PORT=""

BASEDIR=$(dirname "$0")
BASENAME=$(basename "$0")
BINDIR="${BASEDIR}/../src"
CONFIG_DEFAULT="$BASEDIR/../dbmail.conf"
CONFIG_TEST="dbmail-test.conf"
PWD=$(pwd)
ERRORS=0

help () {
  cat << EOF
  Usage is
  $BASENAME run-tests
  $BASENAME clean
  $BASENAME help
  $BASENAME help-imap
    Show example imap session

  Misc:
  $BASENAME plain username password
  $BASENAME debian-date yyyymmdd
  $BASENAME release-info

  Notes:
  This script runs DBMail integration tests and should be
  checked before a release.

  These integration tests are only a starting point and should be supplemented
  with local tests, for example to test ldap.

  Default config = $CONFIG_DEFAULT
EOF
}

do_help_imap () {
  cat << EOF
  a01 login [user] [password]
  a02 select inbox
  a03 logout

  C: <open connection>
  S:   * OK IMAP4rev1 Service Ready
  C:   a001 login mrc secret
  S:   a001 OK LOGIN completed
  C:   a002 select inbox
  S:   * 18 EXISTS
  S:   * FLAGS (\Answered \Flagged \Deleted \Seen \Draft)
  S:   * 2 RECENT
  S:   * OK [UNSEEN 17] Message 17 is the first unseen message
  S:   * OK [UIDVALIDITY 3857529045] UIDs valid
  S:   a002 OK [READ-WRITE] SELECT completed
  C:   a003 fetch 12 full
  S:   * 12 FETCH (FLAGS (\Seen) INTERNALDATE "17-Jul-1996 02:44:25 -0700"
       RFC822.SIZE 4286 ENVELOPE ("Wed, 17 Jul 1996 02:23:25 -0700 (PDT)"
       "IMAP4rev1 WG mtg summary and minutes"
       (("Terry Gray" NIL "gray" "cac.washington.edu"))
       (("Terry Gray" NIL "gray" "cac.washington.edu"))
       (("Terry Gray" NIL "gray" "cac.washington.edu"))
       ((NIL NIL "imap" "cac.washington.edu"))
       ((NIL NIL "minutes" "CNRI.Reston.VA.US")
       ("John Klensin" NIL "KLENSIN" "MIT.EDU")) NIL NIL
       "<B27397-0100000@cac.washington.edu>")
       BODY ("TEXT" "PLAIN" ("CHARSET" "US-ASCII") NIL NIL "7BIT" 3028
       92))
  S:   a003 OK FETCH completed
  C:   a004 fetch 12 body[header]
  S:   * 12 FETCH (BODY[HEADER] {342}
  S:   Date: Wed, 17 Jul 1996 02:23:25 -0700 (PDT)
  S:   From: Terry Gray <gray@cac.washington.edu>
  S:   Subject: IMAP4rev1 WG mtg summary and minutes
  S:   To: imap@cac.washington.edu
  S:   Cc: minutes@CNRI.Reston.VA.US, John Klensin <KLENSIN@MIT.EDU>
  S:   Message-Id: <B27397-0100000@cac.washington.edu>
  S:   MIME-Version: 1.0
  S:   Content-Type: TEXT/PLAIN; CHARSET=US-ASCII
  S:
  S:   )
  S:   a004 OK FETCH completed
  C    a005 store 12 +flags \deleted
  S:   * 12 FETCH (FLAGS (\Seen \Deleted))
  S:   a005 OK +FLAGS completed
  C:   a006 logout
  S:   * BYE IMAP4rev1 server terminating connection
  S:   a006 OK LOGOUT completed
EOF
}

do_clean () {
  for SERVICE in $SERVICES
    do
      echo "Cleaning service $SERVICE"
      # Delete previous logs
      if [ -f "dbmail-${SERVICE}d.core" ]; then
        rm "dbmail-${SERVICE}d.core"
      fi
      if [ -f "dbmail-${SERVICE}d.valgrind" ]; then
        rm "dbmail-${SERVICE}d.valgrind"
      fi
      if [ -f "dbmail-${SERVICE}d.log" ]; then
        rm "dbmail-${SERVICE}d.log"
      fi
      if [ -f "dbmail-${SERVICE}d.pid" ]; then
        rm "dbmail-${SERVICE}d.pid"
      fi
    done
}

do_plain () {
  USERNAME=$2
  PASSWORD=$3
  PLAIN2=$(printf "\0%s\0%s" "${USERNAME}" "${PASSWORD}" | openssl enc -base64)
  PLAIN3=$(printf "%s\0%s\0%s" "${USERNAME}" "${USERNAME}" "${PASSWORD}" | openssl enc -base64)
  echo "authzid, authcid and passwd"
  echo AUTHENTICATE '"'"PLAIN"'"'  '"'"$PLAIN2"'"'
  echo "authzid, authcid and passwd"
  echo AUTHENTICATE '"'"PLAIN"'"'  '"'"$PLAIN3"'"'
}
do_release_info () {
  echo "Releasing a new version"
  echo "Update version in configure.ac"
  echo "Run autoreconf"
  echo "Update debian/changelog use debian-date for dates"
  echo "Update contrib/build-deb-with-docker.sh"
  echo ""
  echo ""
}

do_debian_date () {
  date -j -R ${2}0000
}

service_configure () {
  SERVICE="$1"
  cp "$CONFIG_DEFAULT" ./"$CONFIG_TEST"
  case $SERVICE in
    imap)
      PORT="10143"
      sed -i '' 's/^port.*/port='$PORT'/g' $CONFIG_TEST
    ;;
    lmtp)
      PORT="10024"
      sed -i '' 's/^port.*/port='$PORT'/g' $CONFIG_TEST
    ;;
    *)
      echo "Unable to configure service for ${SERVICE}"
    ;;
  esac

  # Set common options for all daemons
  sed -i '' "s#^bindip.*#bindip=127.0.0.1,::1#" $CONFIG_TEST
  sed -i '' "s#^logfile.*#logfile=${PWD}/dbmail-${SERVICE}d.log#" $CONFIG_TEST
  sed -i '' "s#^file_logging_levels.*#file_logging_levels=debug#" $CONFIG_TEST
  sed -i '' "s#^pid_directory.*#pid_directory=$PWD#" $CONFIG_TEST
  sed -i '' "s#^effective_user.*#effective_user=$(id -u -n)#" $CONFIG_TEST
  sed -i '' "s#^effective_group.*#effective_group=$(id -g -n)#" $CONFIG_TEST
}

service_start () {
  SERVICE="$1"
  # Delete previous logs
  if [ -f "dbmail-${SERVICE}d.core" ]; then
    rm "dbmail-${SERVICE}d.core"
  fi
  if [ -f "dbmail-${SERVICE}d.valgrind" ]; then
    rm "dbmail-${SERVICE}d.valgrind"
  fi
  if [ -f "dbmail-${SERVICE}d.log" ]; then
    rm "dbmail-${SERVICE}d.log"
  fi
  valgrind  --run-libc-freeres=no -s --read-var-info=yes \
    --leak-check=full \
    --log-file="$PWD/dbmail-${SERVICE}d.valgrind" \
    "${BINDIR}/dbmail-${SERVICE}d" --config "$CONFIG_TEST"
  sleep 3
}

service_test () {
  SERVICE="$1"
  echo "Checking ${BASEDIR}"
  for SERVICE_TEST in "${BASEDIR}/"*".${SERVICE}"
  do
    echo "Testing $SERVICE_TEST"
    nc -w 10 "${HOST}" "${PORT}" < "$SERVICE_TEST"
  done
}

service_report () {
  SERVICE="$1"
  echo "Reporting service $SERVICE"
  INVALID=$(grep 'Invalid' "$PWD/dbmail-${SERVICE}d.valgrind")
  if [ -n "$INVALID" ]
  then
    echo "Valgrind found an Invalid read or write for $SERVICE"
    echo "See $PWD/dbmail-${SERVICE}d.valgrind for details"
    echo "There are known Invalid reads documented in issue #474"
    echo "https://github.com/dbmail/dbmail/issues/474"
    ERRORS="$(("$ERRORS" + 1))"
  else
    echo "No known Valgrind errors found for $SERVICE"
  fi
  INVALID=$(grep 'definitely lost' "$PWD/dbmail-${SERVICE}d.valgrind" | grep -v 'definitely lost: 0 bytes in 0 blocks')
  if [ -n "$INVALID" ]
    then
    echo "Valgrind found memory that is definitely lost for $SERVICE"
    echo "There are known memory leaks so check to ensure no new leaks have"
    echo "been introduced."
    echo "$INVALID"
  fi
}
service_stop () {
  SERVICE="$1"
  echo "Stopping service $SERVICE"
  if [ -f "$PWD/dbmail-${SERVICE}d.pid" ]
  then
    ls -l "$PWD/dbmail-${SERVICE}d.pid"
    kill -9 "$(cat "$PWD/dbmail-${SERVICE}d.pid")"
    rm "$PWD/dbmail-${SERVICE}d.pid"
  else
    echo "Service $SERVICE is not running!"
  fi
}

run_tests () {
  for SERVICE in $SERVICES
  do
    echo "Checking service $SERVICE"
    service_configure "$SERVICE"
    service_start "$SERVICE"
    service_test "$SERVICE"
    service_stop "$SERVICE"
    service_report "$SERVICE"
  done

  if [ "$ERRORS" -ge 0 ]
  then
    echo "Errors were found"
    exit 1
  else
    exit 0
  fi
}

case $1 in
  run-tests)
    run_tests
  ;;
  help)
    help
  ;;
  help-imap)
    do_help_imap
  ;;
  clean)
    do_clean
  ;;
  plain)
    do_plain "$@"
  ;;
  release-info)
    do_release_info
  ;;
  debian-date)
    do_debian_date "$@"
  ;;
  *)
    echo 'Unknown option ' "$*"
    help
esac

