/*
 $Id$

 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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

#include <glib.h>
#include "list.h"

/* Define several macros for GCC specific attributes.
 * Although the __attribute__ macro can be easily defined
 * to nothing, these macros make them a little prettier.
 * */
#ifdef __GNUC__
#define UNUSED __attribute__((__unused__))
#define PRINTF_ARGS(X, Y) __attribute__((format(printf, X, Y)))
#else
#define UNUSED
#define PRINTF_ARGS(X, Y)
#endif


#define CONFIG_ERROR_LEVEL TRACE_WARNING

/** string length of configuration values */
#define FIELDSIZE 1024
#define COPYRIGHT "(c) 1999-2004 IC&S, The Netherlands"
/** default directory and extension for pidfiles */
#define DEFAULT_PID_DIR "/var/run/"
#define DEFAULT_PID_EXT ".pid"
/** default configuration file */
#define DEFAULT_CONFIG_FILE "/etc/dbmail.conf"

/** username of user that is owner of all mailboxes */
#define SHARED_MAILBOX_USERNAME "__public__"

/** prefix for #Users namespace */
#define NAMESPACE_USER "#Users"
/** prefix for public namespace */
#define NAMESPACE_PUBLIC "#Public"
/** seperator for namespaces and mailboxes and submailboxes */
#define MAILBOX_SEPERATOR "/"
/** username for owner of public folders */
#define PUBLIC_FOLDER_USER "__public__"

/** default table prefix */
#define DEFAULT_DBPFX "dbmail_"

#define MATCH(x,y) strcasecmp(x,y)==0

/* uncomment this if you want informative process titles */
//#define PROC_TITLES

/** status fields for messages */
typedef enum {
	MESSAGE_STATUS_NEW     = 0,
	MESSAGE_STATUS_SEEN    = 1,
	MESSAGE_STATUS_DELETE  = 2,
	MESSAGE_STATUS_PURGE   = 3,
	MESSAGE_STATUS_UNUSED  = 4,
	MESSAGE_STATUS_INSERT  = 5,
	MESSAGE_STATUS_ERROR   = 6
} MessageStatus_t;

/** field_t is used for storing configuration values */
typedef char field_t[FIELDSIZE];

/** size of a timestring_t field */
#define TIMESTRING_SIZE 30
/** timestring_t is used for holding timestring */
typedef char timestring_t[TIMESTRING_SIZE];

/** parameters for the database connection */
typedef struct {
	field_t host;
		   /**< hostname or ip address of database server */
	field_t user;
		   /**< username to connect with */
	field_t pass;
		   /**< password of user */
	field_t db;/**< name of database to connect with */
	unsigned int port;
			/**< port number of database server */
	field_t sock;
		   /**< path to local unix socket (local connection) */
	field_t pfx;
			/**< prefix for tables e.g. dbmail_ */
} db_param_t;

/** configuration items */
typedef struct {
	field_t name;
		   /**< name of configuration item */
	field_t value;
		   /**< value of configuration item */
} item_t;

/**
 * \brief read configuration from filename
 * \param cfilename name of configuration file 
 * \return
 *     - -1 on error
 *     -  0 on success
 */
int config_read(const char *config_filename);

/**
 * free all memory taken up by config.
 */
void config_free(void);
	       
/**
 * \brief get configuration value for an item
 * \param name name of configuration item
 * \param service_name name of service
 * \param value value of configuration item name
 * \return 0
 * \attention value is set to a string beginning with a '\\0' 
 * if no configuration item with name is found in items.
 */
int config_get_value(const field_t name, const char *service_name,
                     /*@out@*/ field_t value);

/* some common used functions reading config options */
/**
 \brief get parameters for database connection
 \param db_params list of database parameters (db_param_t)
*/
void GetDBParams(db_param_t * db_params);
/** 
 \brief set the overall trace level, using the value in cfg_items
 \param service_name name of service to get trace level for.
 \attention trace level is set to TRACE_ERROR if no trace level value is found
 *                for service.
 */
void SetTraceLevel(const char *service_name);


#endif
