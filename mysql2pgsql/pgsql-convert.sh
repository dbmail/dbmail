#!/bin/bash

/usr/local/pgsql/bin/dropdb dbmail
/usr/local/pgsql/bin/createdb dbmail
/usr/local/pgsql/bin/psql dbmail < ../sql/postgresql/no-constraint-tables.pgsql
/usr/local/pgsql/bin/psql dbmail < copytopgsql.sql
/usr/local/pgsql/bin/psql dbmail < ../sql/postgresql/add-constraints.pgsql


