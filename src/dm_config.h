/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

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
#ifndef DM_CONFIG_H
#define DM_CONFIG_H



/* 
 * determine the app version name 
 * it is retrieved from the internal configuration and it cannot be overwritten
 */
long config_get_app_version(void);


/* 
 * determine the default configuration filename
 * may be overridden by a -f switch
 */
void config_get_file(void);

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
int config_get_value(const Field_T name, const char *service_name,
                     /*@out@*/ Field_T value);


int config_get_value_default_int(const Field_T field_name,
                     const char * const service_name,
                     int default_value);

Field_T* config_get_value_default_string(const Field_T field_name,
                     const char * const service_name,
                     Field_T value, Field_T default_value);
					 
/* some common used functions reading config options */
/**
 \brief get parameters for database connection
*/
void GetDBParams(void);
/** 
 \brief set the overall trace level, using the value in cfg_items
 \param service_name name of service to get trace level for.
 \attention trace level is set to TRACE_ERR if no trace level value is found
 *                for service.
 */
void SetTraceLevel(const char *service_name);


void pidfile_create(const char *pidFile, pid_t pid);

void config_get_timeout(ServerConfig_T *config, const char * const service);
void config_get_logfiles(ServerConfig_T *config, const char * const service);
void config_get_security_actions(ServerConfig_T *config);

char * config_get_pidfile(ServerConfig_T *config, const char *name);
char * config_get_statefile(ServerConfig_T *config, const char *name);




#endif
