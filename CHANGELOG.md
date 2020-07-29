# Changelog
All notable changes to this project will be documented in this file.

## [3.2.5] - 2020-08-xx
### Added
- IMAP Daemon: added switch to control mailbox_update_strategy=2 (see mailbox_update_strategy_2_max_iterations) [#81]
- IMAP Daemon: added switch to control UNSEEN first message in SELECT commands [#83]

### Changed
 
### Optimizations 
- optimizing differential state [#81]


### Issues
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
