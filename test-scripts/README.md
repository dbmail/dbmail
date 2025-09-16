# DBMail integration tests

These integration tests should be run before every release to ensure all
DBMail functions and services work together. They should be supplemented with
as many tests as necessary to ensure all parts of DBMail work as expected.

DBMail is a collection of services running under a variety systems including
Linux and the BSDs. With many configuration options, these integration tests
go some way towards checking they work as expected.

The main script is check.sh a Bourne shell script that runs the DBMail daemons
under valgrind and checks output for known errors.

There are utility functions to show an example IMAP conversation, create
plain passwords, create a date suitable for the debian changelog, also
release info and guidance notes to help ensure a a successful release.

Other scripts test various aspects of DBMail and should be used as required.
