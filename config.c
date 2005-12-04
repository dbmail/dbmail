/*
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

/*
 * $Id: config.c 1929 2005-11-29 10:44:16Z aaron $
 * \file config.c
 * \brief read configuration values from a config file
 */

#include "dbmail.h"

/** dictionary which holds the configuration */
static GKeyFile *config_dict = NULL;

static int configured = 0;
/**
 * read the configuration file and stores the configuration
 * directives in an internal structure.
 */
int config_read(const char *config_filename)
{
	if (configured++) 
		return 0;
	
	assert(config_filename != NULL);
        
        config_dict = g_key_file_new();
	
	if (! g_key_file_load_from_file(config_dict, config_filename, G_KEY_FILE_NONE, NULL)) {
		g_key_file_free(config_dict);
                trace(CONFIG_ERROR_LEVEL, "%s,%s: error reading "
                      "config file %s", __FILE__, __func__,
                      config_filename);
		return -1;
	}
        return 0;
}

/**
 * free all memory related to config 
 */
void config_free(void) 
{
	if (--configured) 
		return;
	
	g_key_file_free(config_dict);
}

/* FIXME: Always returns 0, which is dandy for debugging. */
int config_get_value(const field_t field_name,
                     const char * const service_name,
                     field_t value) {
        char *dict_value;
	char *key;
	gssize len;

        assert(service_name);
        assert(config_dict);
 
	/* as is */
	dict_value = g_key_file_get_value(config_dict, service_name, field_name, NULL);
        if (dict_value) {
                g_strlcpy(value, dict_value, FIELDSIZE);
		g_free(dict_value);
		return 0;
	}
	       
	len = strlen(field_name);
	
	/* uppercase */
	key = g_ascii_strup(field_name,len);
        dict_value = g_key_file_get_value(config_dict, service_name, key, NULL);
        if (dict_value) {
                g_strlcpy(value, dict_value, FIELDSIZE);
		g_free(key);
		g_free(dict_value);
		return 0;
	}
	
	/* lowercase */
	key = g_ascii_strdown(field_name,len);
	dict_value = g_key_file_get_value(config_dict, service_name, key, NULL);
        if (dict_value) {
                g_strlcpy(value, dict_value, FIELDSIZE);
		g_free(key);
		g_free(dict_value);
		return 0;
	}

	/* give up */
        value[0] = '\0';
	g_free(key);
	g_free(dict_value);

        return 0;
}

void SetTraceLevel(const char *service_name)
{
	field_t trace_level;
	field_t trace_stderr;
	field_t trace_syslog;

	if (config_get_value("trace_level", service_name, trace_level) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);

	if (config_get_value("trace_stderr", service_name, trace_stderr) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);

	if (config_get_value("trace_syslog", service_name, trace_syslog) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);

	if (strlen(trace_level) == 0)
		configure_debug(TRACE_ERROR,
				atoi(trace_syslog) ? 1 : 0,
				atoi(trace_stderr) ? 1 : 0);
	else 
		configure_debug(atoi(trace_level),
				atoi(trace_syslog) ? 1 : 0,
				atoi(trace_stderr) ? 1 : 0);
}

void GetDBParams(db_param_t * db_params)
{
	field_t port_string, sock_string, serverid_string;
	
	if (config_get_value("driver", "DBMAIL", db_params->driver) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);
	if (config_get_value("authdriver", "DBMAIL", db_params->authdriver) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);
	if (config_get_value("sortdriver", "DBMAIL", db_params->sortdriver) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);
	if (config_get_value("host", "DBMAIL", db_params->host) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);
	if (config_get_value("db", "DBMAIL", db_params->db) < 0) 
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);
	if (config_get_value("user", "DBMAIL", db_params->user) < 0) 
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);
	if (config_get_value("pass", "DBMAIL", db_params->pass) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);
	if (config_get_value("sqlport", "DBMAIL", port_string) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);
	if (config_get_value("sqlsocket", "DBMAIL", sock_string) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);
	if (config_get_value("serverid", "DBMAIL", serverid_string) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);

	

	if (config_get_value("table_prefix", "DBMAIL", db_params->pfx) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __func__);
	if (strcmp(db_params->pfx, "\"\"") == 0) {
		/* FIXME: It appears that when the empty string is quoted
		 * that the quotes themselves are returned as the value. */
		g_strlcpy(db_params->pfx, "", FIELDSIZE);
	} else if (strlen(db_params->pfx) == 0) {
		/* If it's not "" but is zero length, set the default. */
		g_strlcpy(db_params->pfx, DEFAULT_DBPFX, FIELDSIZE);
	}

	/* check if port_string holds a value */
	if (strlen(port_string) != 0) {
		db_params->port =
		    (unsigned int) strtoul(port_string, NULL, 10);
		if (errno == EINVAL || errno == ERANGE)
			trace(TRACE_FATAL,
			      "%s,%s: wrong value for sqlport in "
			      "config file", __FILE__, __func__);
	} else
		db_params->port = 0;

	/* same for sock_string */
	if (strlen(sock_string) != 0)
		g_strlcpy(db_params->sock, sock_string, FIELDSIZE);
	else
		db_params->sock[0] = '\0';

	/* and serverid */
	if (strlen(serverid_string) != 0) {
		db_params->serverid = (unsigned int) strtol(serverid_string, NULL, 10);
		if (errno == EINVAL || errno == ERANGE)
			trace(TRACE_FATAL, "%s,%s: serverid invalid in config file",
					__FILE__, __func__);
	} else {
		db_params->serverid = 1;
	}
}

