# $Id$

CREATE DATABASE dbmail;
USE dbmail;
CREATE TABLE aliases (
   alias_idnr bigint(21) DEFAULT '0' NOT NULL auto_increment,
   alias varchar(100) NOT NULL,
   deliver_to varchar(250) NOT NULL,
	owner_id int(11) DEFAULT '0' NOT NULL,
   PRIMARY KEY (alias_idnr),
   KEY alias_idnr (alias_idnr),
   UNIQUE alias_idnr_2 (alias_idnr)
);

CREATE TABLE user (
   useridnr int(11) DEFAULT '0' NOT NULL auto_increment,
   userid varchar(100) NOT NULL,
   passwd varchar(32) NOT NULL,
   clientid bigint(21) DEFAULT '0' NOT NULL,
   maxmail_size bigint(21) DEFAULT '0' NOT NULL,
   PRIMARY KEY (useridnr),
   KEY useridnr (useridnr, userid),
   UNIQUE useridnr_2 (useridnr, userid)
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
   no_inferiors tinyint(1) default '0' not null,
   no_select tinyint(1) default '0' not null,
   permission tinyint(1) default '2',
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
   internal_date datetime default '0' not null,
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

CREATE TABLE config (
	configid int(11) DEFAULT '0' NOT NULL,
	item varchar(128) NOT NULL,
	value varchar(128) NOT NULL
);


