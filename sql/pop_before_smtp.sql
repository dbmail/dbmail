# $Id$
# This will add a table which is used for
# pop-before-smtp

USE dbmail;

CREATE TABLE pbsp (
   idnr bigint(21) DEFAULT '0' NOT NULL auto_increment,
   since datetime default '0' not null,
   ipnumber varchar(40) NOT NULL,
   PRIMARY KEY (idnr),
   KEY idnr (idnr),
   UNIQUE idnr_2 (idnr),
   UNIQUE ipnumber_2 (ipnumber)
);
