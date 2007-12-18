

DBMail-2 as of 2.1.4 supports the Sieve mail sorting language. The libSieve
library is used to provide the core Sieve interpreter: http://libsieve.sf.net/

Users are allowed to store their own Sieve scripts in the DBMail database,
and to set the active script that will run as each message is received for
a given user. Scripts are managed using "Tim's Sieve Daemon" protocol, which
was specified in the MANAGESIEVE draft standard, but has long since expired.
DBMail's timsieved is compatible with the widely deployed Cyrus timsieved,
and client programs compatible with Cyrus timsieved should work well.

Scripts can also be managed using the command line tool dbmail-sievecmd, but
this is recommended only to be used for administrative maintenance; such as
clearing out old scripts from deleted users or testing new configurations.

Many thanks to Kevin Baker for his generous development bounty which spurred
completion of this feature. Thanks to Aaron Stone for finishing the code.

