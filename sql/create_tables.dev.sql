# $Id$

CREATE DATABASE dbmail_dev;
USE dbmail_dev;
CREATE TABLE aliases (
   alias_idnr int(11) DEFAULT '0' NOT NULL auto_increment,
   alias varchar(100) NOT NULL,
   useridnr int(11) NOT NULL,
   PRIMARY KEY (alias_idnr),
   KEY alias_idnr (alias_idnr),
   UNIQUE alias_idnr_2 (alias_idnr)
);

CREATE TABLE user (
   useridnr int(11) DEFAULT '0' NOT NULL auto_increment,
   userid varchar(100) NOT NULL,
   passwd varchar(15) NOT NULL,
   clientid varchar(8) NOT NULL,
   maxmail int(11) DEFAULT '0' NOT NULL,
   PRIMARY KEY (useridnr),
   KEY useridnr (useridnr, userid),
   UNIQUE useridnr_2 (useridnr)
);

CREATE TABLE mailbox (
   mailboxidnr bigint(21) DEFAULT '0' NOT NULL auto_increment,
   owneridnr int(11) DEFAULT '0' NOT NULL,
   name varchar(100) NOT NULL,
   seen_flag tinyint(1) default '0' not null,
   answered_flag tinyint(1) default '0' not null,
   deleted_flag tinyint(1) default '0' not null,
   flagged_flag tinyint(1) default '0' not null,
   recent_flag tinyint(1) default '0' not null,
   draft_flag tinyint(1) default '0' not null,
   permission tinyint(1) default '3',
   PRIMARY KEY (mailboxidnr),
   UNIQUE mailboxidnr_2 (mailboxidnr)
);
  
CREATE TABLE message (
   messageidnr bigint(21) DEFAULT '0' NOT NULL auto_increment,
   mailboxidnr int(21) DEFAULT '0' NOT NULL,
   messagesize bigint(21) DEFAULT '0' NOT NULL,
   seen_flag tinyint(1) default '0' not null,
   answered_flag tinyint(1) default '0' not null,
   deleted_flag tinyint(1) default '0' not null,
   flagged_flag tinyint(1) default '0' not null,
   recent_flag tinyint(1) default '0' not null,
   draft_flag tinyint(1) default '0' not null,
   unique_id varchar(70) NOT NULL,
   status tinyint(3) unsigned zerofill default '000' not null,
   PRIMARY KEY (messageidnr),
   KEY messageidnr (messageidnr),
   UNIQUE messageidnr_2 (messageidnr)
);

CREATE TABLE messageblk (
   messageblknr bigint(21) DEFAULT '0' NOT NULL auto_increment,
   messageidnr bigint(21) DEFAULT '0' NOT NULL,
   messageblk longtext NOT NULL,
   blocksize bigint(21) DEFAULT '0' NOT NULL,
   PRIMARY KEY (messageblknr),
   KEY messageblknr (messageblknr),
	KEY msg_index (messageidnr),
   UNIQUE messageblknr_2 (messageblknr)
) TYPE=MyISAM;


