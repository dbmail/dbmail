# Changelog
All notable changes to this project will be documented in this file.

## [3.5.0] - 2025-03-05
- Successfully builds on ubuntu 24.4
- Docerfiles for ubuntu development, 22.4 and 24.4
- Tests refactored and now pass with gmime 213

## [3.5.0] - 2025-01-09
- Fresh installs automatically create database
- Required headername automatically added
- curl added to enable forwarding emails via SMTP
- Logging config simplified
- Deprecated dbmail-timsieved removed in favour of dbmail-sieved
- Upgrade to gmime 3 reflected in checks
- Checks refactored
- Check added before install to Dockerfile
- Fix a SQL syntax error when no header fields specified
- Reverted a memory leak
- Created DBMail announce newsletter
- mack-erel updated ‎contrib/build-centos-rpm
- Re-organised dbmail-util man page
- messageblks upgrade info added
- Docker alpine updates from BobRam
- Debian build updated
- Duplicate envelope inserts treated as warnings instead of errors
- Redundant code removed
- Unused variables removed
- Fix for sig11 null reference
- Inserting null data avoid if invalid parts are received
- Upgrading V2 schema to V3 invalid entries no longer error
- Message part insertion refactored
- flags.silent refactored for outlook clients
- Missing final mime boundary added
- Default config file listed when calling dbmail-* -h
- Experimental Sender Rewriting Scheme removed

## [3.5.0] - 2024-10-22
- cURL library added for SMTP
- Logging simplified to use keywords

## [3.5.0] - 2024-08-16
- Experimental Sender Rewriting Scheme added

## [3.5.0] - 2024-05-20
- Version bump to 3.5.0
- Create tables automated
- dbmail-timsieve without tls removed
- MySQL utf8mb4 moved to upgrade 35001

## [3.4.1] - 2023-10-21
- Missing flags fixed
- dbmail-sieved.txt added
- Experimental Docker image created
- Version bump to 3.4.1

## [3.4.0] - 2023-08-26
- dbmail-sieved with tls support added
- dbmail-timsieved deprecated and will be removed in 3.5
- configure scripts updated with autoconf 2.71 automake 1.6.5

## [3.3.1] - 2022-10-15
- Fixed issue #175 with mysql 8.0.31 and libzdb

## [3.3.0] - 2022-05-26
- gmime updated to 3

## [3.2.6] - 2022-01-09
### WARNING
- the maintenance(dbmail-util) process should be issues in a maintenance window do to massive changes on database, see [#108]
- compatibility compiler standard was raised to C17

### Added
- IMAP Daemon: added switch to control the sequence update strategy
- IMAP Daemon: added switch to control the sync between \DELETE flag and the actual state of the message (deleted), see also mailbox_sync_batch_size.
- Build: automatic installation script (v 0.5), #104
- Maintenance(dbmail-util): added option to upgrade to utf8mb4(MariaDB/MySQL), --upgrade, beaware this is an intensive process (depending on you database), a backup is advised [#108]

### Optimizations
- IMAP: optimizing differential state

### Issues
- fixing Outlook issue found on making a message read [#139]
- fixing duplicate key by adding necessary handlers [#134]
- fixing deleting forwards and aliases when deleting a user [#129]
- fixing LMTP delivery in case of disabled users [#122]
- fixing compilation issue on alpine and SmartOS [#123] [#124]
- fixing sql issues related to MODSEQ [#111]
- fixing LMTP segmentation fault on sieve error [#106]
- fixing invalid utf-8 character adding sql conversion [#108]
- fixing other sql issues (upgrade process) [#103] [#102] [#99] [#97] [#105] [#93]
- fixing SIEVE crash on error [#106]
- fixing debian build [#91]

## [3.2.5] - 2020-08-03
### Added
- IMAP Daemon: added switch to control the diffential state reload (mailbox_update_strategy=2), more information in dbmail.conf, mailbox_update_strategy_2_max_iterations [#81]
- IMAP Daemon: added switch to control UNSEEN first message in SELECT commands [#83]

### Changed
- IMAP Daemon: allow reporting UID COPY success in case of various failures (except quota), reporting issues are sent to error log as warnings [#87]
 
### Optimizations 
- optimizing differential state [#81]
- optimizing fetch message headers [#85]

### Issues
- fixing issue related to copy message in regard to RFC 3501, section 6.4.8 [#87]
- fixing issues related group_concat for PostgreSql [#75], [#78]
- fixing issue related to lastRowId [#71]
- fixing issues related with differential update [#70], [#73]
- fixing proc not being used in BSD systems [#74]
- IMAP Daemon: segmentation fault [#68]

## [3.2.4] - 2020-06-08
### Added
- IMAP Daemon: mailbox-update-strategy switch (see dbmail.conf)
- support for application_name in database connection uri
- IMAP Daemon: mailbox_search_strategy switch (see dbmail.conf)

### Changed
- systemd unit changed to type notify
- mailbox state is build using only valid messages [#39]


### Optimizations 
- IMAP Daemon: optimization of sql queries in relation to message headers
- libevent increased priority on accepting new connections
- libevent optimization on reading and writing to sockets
- simplify libzdb configuration (AC_CHECK_HEADERS)

### Issues
- fix segmentation fault in imap_append_hash_as_string [#12]
- dbmail-users: sql issue on deleting alias user [#18]
- IMAP Daemon: generation of invalid BODYSTRUCTURE in Content-Type field [#23]
- fix support for jemalloc latest version [#35]
- IMAP Deamon: BYE Command now offers optional message even on normal operations [#46]
- IMAP Deamon: idle message now offers optional message (* OK Still Here)
- IMAP Daemon: random hangs when single user is connected [#37]
- fix fd leaks
- IMAP Daemon: fix MODIFIED keyword, too many '[' and ']'
- fix segmentation fault in find_end_of_header
- fix gcc 10 compilation issue, duplicated definition


## [3.2.3] 

The changelog original location was located on on http://git.dbmail.eu/paul/dbmail/log/
