# DBMail integration tests

These integration tests should be run before every release to ensure all
DBMail functions and services work together. They should be supplemented with
as many tests as necessary to ensure all parts of DBMail work as expected.

Functional checks only ensure a function when provided with an input returns a
pre-defined output.

These integration tests check typical workflow of DBMail services. The
dbmail-* apps are run under valgrind to check for dynamic analysis including
detection of many memory management and threading bugs.

DBMail is a collection of services running under a variety systems including
Linux, Docker and the BSDs. With many configuration options, these integration
tests go some way towards checking they work as expected on supported systems.

The main script is check.sh a Bourne shell script for compatibility across
Linux, the BSDs and other Unix type systems. It runs the DBMail daemons under
valgrind and reports known errors. A manual inspection of the logs is strongly
advised.

There are utility functions to show an example IMAP conversation, create
plain passwords, create a date suitable for the Debian changelog, also
release info and guidance notes to help ensure a successful release.

Other scripts test various aspects of DBMail and should be used as required.
