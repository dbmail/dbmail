/*
 Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/**
 * \file dbmail.h
 * header file for a general configuration
 */

#ifndef _DBMAIL_H
#define _DBMAIL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#define VERSION "2.0"
#define PACKAGE "dbmail"
#endif

#include "list.h"
#include "debug.h"

/* define UNUSED for parameters that are not used in functions */
#ifdef __GNUC__
#define UNUSED __attribute__((__unused__))
#else
#define UNUSED
#endif

#define CONFIG_ERROR_LEVEL TRACE_WARNING

/** string length of configuration values */
#define FIELDSIZE 1024
#define COPYRIGHT "(c) 1999-2003 IC&S, The Netherlands"
/** default configuration file */
#define DEFAULT_CONFIG_FILE "/etc/dbmail.conf"

/* uncomment this if you want informative process titles */
//#define PROC_TITLES

/** field_t is used for storing configuration values */
typedef char field_t[FIELDSIZE];

/** parameters for the database connection */
typedef struct {
     field_t host; /**< hostname or ip address of database server */
     field_t user; /**< username to connect with */
     field_t pass; /**< password of user */
     field_t db;   /**< name of database to connect with */
     unsigned int port; /**< port number of database server */
     field_t sock; /**< path to local unix socket (local connection) */
} db_param_t;
     
/** configuration items */
typedef struct
{
  field_t name;    /**< name of configuration item */ 
  field_t value;   /**< value of configuration item */
} item_t;

/**
 * \brief read configuration items for a service
 * \param serviceName name of service to get paramaters for. determines from which
 *        section in the configuration file values are taken.
 * \param cfilename name of configuration file 
 * \param cfg_items list of configuration values (should already be filled in using
          ReadConfig()
 * \return
 *     - -1 on error
 *     -  0 on success
 */
int ReadConfig(const char *serviceName, const char *cfilename, struct list *cfg_items);
/**
 * \brief get configuration value for an item
 * \param name name of configuration item
 * \param cfg_items list of configuration values (should already be filled in using
          ReadConfig()
 * \param value value of configuration item name
 * \return 0
 * \attention value is set to a string beginning with a '\\0' if no configuration
              item with name is found in items.
 */
int GetConfigValue(const field_t name, struct list *cfg_items, field_t value);

/* some common used functions reading config options */
/**
 \brief get parameters for database connection
 \param db_params list of database parameters (db_param_t)
 \param cfg_items list of configuration items
*/
void GetDBParams(db_param_t *db_params, struct list *cfg_items);
/** 
 \brief set the overall trace level, using the value in cfg_items
 \param cfg_items list of configuration items
 \attention trace level is set to TRACE_ERROR if no trace level value is found
            in cfg_items
 */
void SetTraceLevel(struct list *cfg_items);

#endif
