-- DESCRIPTION
--  Simple example script to create  tablespaces
-- usage:
--   sqlplus "/ as sysdba" @create_ts.sql DATA_TOP

SPOOL "create_ts.log"

create tablespace DBMAIL_TS_DATA
datafile '&1/dbmail_ts_data01.dbf'
size 400M
autoextend on
next 100M maxsize 4000M
extent management local
uniform size 1M;

create tablespace DBMAIL_TS_IDX
datafile '&1/dbmail_ts_idx0i1.dbf'
size 400M
autoextend on
next 100M maxsize 4000M
extent management local
uniform size 1M;

SPOOL OFF
EXIT;

