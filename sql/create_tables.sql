CREATE DATABASE dbmail;
USE dbmail;
CREATE TABLE aliases (
   alias_idnr int(11) DEFAULT '0' NOT NULL auto_increment,
   alias varchar(100) NOT NULL,
   useridnr int(11) NOT NULL,
   PRIMARY KEY (alias_idnr),
   KEY alias_idnr (alias_idnr),
   UNIQUE alias_idnr_2 (alias_idnr)
);
CREATE TABLE mailbox (
   useridnr int(11) DEFAULT '0' NOT NULL auto_increment,
   userid varchar(100) NOT NULL,
   passwd varchar(15) NOT NULL,
   clientid varchar(8) NOT NULL,
   maxmail int(11) DEFAULT '0' NOT NULL,
   PRIMARY KEY (useridnr),
   KEY useridnr (useridnr, userid),
   UNIQUE useridnr_2 (useridnr)
);
CREATE TABLE message (
   messageidnr bigint(21) DEFAULT '0' NOT NULL auto_increment,
   useridnr int(11) DEFAULT '0' NOT NULL,
   messagesize bigint(21) DEFAULT '0' NOT NULL,
   status tinyint(3) unsigned zerofill DEFAULT '000' NOT NULL,
   unique_id varchar(70) NOT NULL,
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
