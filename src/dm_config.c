/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl

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
 * 
 * \file config.c
 * \brief read configuration values from a config file
 */

#include "dbmail.h"

#define THIS_MODULE "config"

/** dictionary which holds the configuration */
static GKeyFile *config_dict = NULL;

static int configured = 0;
/**
 * read the configuration file and stores the configuration
 * directives in an internal structure.
 */
int config_read(const char *config_filename)
{
	if (configured) 
		return 0;
	
	assert(config_filename != NULL);
        
        config_dict = g_key_file_new();
	
	if (! g_key_file_load_from_file(config_dict, config_filename, G_KEY_FILE_NONE, NULL)) {
		g_key_file_free(config_dict);
                TRACE(TRACE_FATAL, "error reading config file %s", config_filename);
		_exit(1);
		return -1;
	}

	// silence the glib logger
	g_log_set_default_handler((GLogFunc)null_logger, NULL);

	configured = 1;
        return 0;
}

/**
 * free all memory related to config 
 */
void config_free(void) 
{
	if (!configured) 
		return;
	
	g_key_file_free(config_dict);

	configured = 0;
}

/* Return 1 if found, 0 if not. */
/* This function also strips any... # Trailing comments. */
/* value is not modified unless something is found. */
static int config_get_value_once(const field_t field_name,
		const char * const service_name,
		field_t value)
{
	char *dict_value;
	int retval = 0;

	assert(service_name);
	assert(config_dict);

	dict_value = g_key_file_get_value(config_dict, service_name, field_name, NULL);
        if (dict_value) {
		char *end;
		end = g_strstr_len(dict_value, FIELDSIZE, "#");
		if (end) *end = '\0';
		g_strstrip(dict_value);
                g_strlcpy(value, dict_value, FIELDSIZE);
		g_free(dict_value);
		retval = 1;
	}

	return retval;
}

/* FIXME: Always returns 0, which is dandy for debugging. */
int config_get_value(const field_t field_name,
                     const char * const service_name,
                     field_t value)
{
	char *key;
	gssize field_len;

	field_len = strlen(field_name);
	
	// First look in the SERVICE section.
	// For each attempt, try as-is, upper, lower.
	       
	key = NULL;
	if (config_get_value_once(field_name, service_name, value))
		goto config_get_value_done;
	
	key = g_ascii_strup(field_name, field_len);
	if (config_get_value_once(key, service_name, value))
		goto config_get_value_done;
	g_free(key);

	key = g_ascii_strdown(field_name, field_len);
	if (config_get_value_once(key, service_name, value))
		goto config_get_value_done;
	g_free(key);

	// if not found, get the DBMAIL section.
	// For each attempt, try as-is, upper, lower.
	       
	key = NULL;
	if (config_get_value_once(field_name, "DBMAIL", value))
		goto config_get_value_done;
	
	key = g_ascii_strup(field_name, field_len);
	if (config_get_value_once(key, "DBMAIL", value))
		goto config_get_value_done;
	g_free(key);

	key = g_ascii_strdown(field_name, field_len);
	if (config_get_value_once(key, "DBMAIL", value))
		goto config_get_value_done;
	g_free(key);
	
	/* give up */
        value[0] = '\0';
	return 0;

config_get_value_done:
	g_free(key);
	return 0;
}

void SetTraceLevel(const char *service_name)
{
	trace_t trace_stderr_int, trace_syslog_int;
	field_t trace_level, trace_syslog, trace_stderr;

	/* Warn about the deprecated "trace_level" config item,
	 * but we will use this value for trace_syslog if needed. */
	config_get_value("trace_level", service_name, trace_level);
	if (strlen(trace_level)) {
		TRACE(TRACE_MESSAGE,
			"Config item TRACE_LEVEL is deprecated. "
			"Please use TRACE_SYSLOG and TRACE_STDERR instead.");
	}

	/* Then we override globals with per-service settings. */
	config_get_value("trace_syslog", service_name, trace_syslog);
	config_get_value("trace_stderr", service_name, trace_stderr);

	if (strlen(trace_syslog))
		trace_syslog_int = atoi(trace_syslog);
	else if (strlen(trace_level))
		trace_syslog_int = atoi(trace_level);
	else
		trace_syslog_int = TRACE_ERROR;

	if (strlen(trace_stderr))
		trace_stderr_int = atoi(trace_stderr);
	else
		trace_stderr_int = TRACE_FATAL;

	configure_debug(trace_syslog_int, trace_stderr_int);
}

db_param_t * GetDBParams(void)
{
	field_t port_string, sock_string, serverid_string;
	field_t query_time;
	
	db_param_t * db_params = g_new0(db_param_t,1);
	
	if (config_get_value("driver", "DBMAIL", db_params->driver) < 0)
		TRACE(TRACE_FATAL, "error getting config! [driver]");
	if (config_get_value("authdriver", "DBMAIL", db_params->authdriver) < 0)
		TRACE(TRACE_FATAL, "error getting config! [authdriver]");
	if (config_get_value("sortdriver", "DBMAIL", db_params->sortdriver) < 0)
		TRACE(TRACE_FATAL, "error getting config! [sortdriver]");
	if (config_get_value("host", "DBMAIL", db_params->host) < 0)
		TRACE(TRACE_FATAL, "error getting config! [host]");
	if (config_get_value("db", "DBMAIL", db_params->db) < 0) 
		TRACE(TRACE_FATAL, "error getting config! [db]");
	if (config_get_value("user", "DBMAIL", db_params->user) < 0) 
		TRACE(TRACE_FATAL, "error getting config! [user]");
	if (config_get_value("pass", "DBMAIL", db_params->pass) < 0)
		TRACE(TRACE_FATAL, "error getting config! [pass]");
	if (config_get_value("sqlport", "DBMAIL", port_string) < 0)
		TRACE(TRACE_FATAL, "error getting config! [sqlpost]");
	if (config_get_value("sqlsocket", "DBMAIL", sock_string) < 0)
		TRACE(TRACE_FATAL, "error getting config! [sqlsocket]");
	if (config_get_value("serverid", "DBMAIL", serverid_string) < 0)
		TRACE(TRACE_FATAL, "error getting config! [serverid]");
	if (config_get_value("encoding", "DBMAIL", db_params->encoding) < 0)
		TRACE(TRACE_FATAL, "error getting config! [encoding]");
	if (config_get_value("table_prefix", "DBMAIL", db_params->pfx) < 0)
		TRACE(TRACE_FATAL, "error getting config! [table_prefix]");

	if (config_get_value("query_time_info", "DBMAIL", query_time) < 0)
		TRACE(TRACE_FATAL, "error getting config! [query_time_info]");
		if (strlen(query_time) != 0)
			db_params->query_time_info = (unsigned int) strtoul(query_time, NULL, 10);
		else
			db_params->query_time_info = 10;

	if (config_get_value("query_time_message", "DBMAIL", query_time) < 0)
		TRACE(TRACE_FATAL, "error getting config! [query_time_message]");
		if (strlen(query_time) != 0)
			db_params->query_time_message = (unsigned int) strtoul(query_time, NULL, 10);
		else
			db_params->query_time_message = 20;

	if (config_get_value("query_time_warning", "DBMAIL", query_time) < 0)
		TRACE(TRACE_FATAL, "error getting config! [query_time_warning]");
		if (strlen(query_time) != 0)
			db_params->query_time_warning = (unsigned int) strtoul(query_time, NULL, 10);
		else
			db_params->query_time_warning = 30;

	if (strcmp(db_params->pfx, "\"\"") == 0) {
		/* FIXME: It appears that when the empty string is quoted
		 * that the quotes themselves are returned as the value. */
		g_strlcpy(db_params->pfx, "", FIELDSIZE);
	} else if (strlen(db_params->pfx) == 0) {
		/* If it's not "" but is zero length, set the default. */
		g_strlcpy(db_params->pfx, DEFAULT_DBPFX, FIELDSIZE);
	}

	/* expand ~ in db name to HOME env variable */
	if ((strlen(db_params->db) > 0 ) && (db_params->db[0] == '~')) {
		char *homedir;
		field_t db;
		if ((homedir = getenv ("HOME")) == NULL)
			TRACE(TRACE_FATAL, "can't expand ~ in db name");
		g_snprintf(db, FIELDSIZE, "%s%s", homedir, &(db_params->db[1]));
		g_strlcpy(db_params->db, db, FIELDSIZE);
	}

	/* check if port_string holds a value */
	if (strlen(port_string) != 0) {
		db_params->port =
		    (unsigned int) strtoul(port_string, NULL, 10);
		if (errno == EINVAL || errno == ERANGE)
			TRACE(TRACE_FATAL, "wrong value for sqlport in config file");
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
			TRACE(TRACE_FATAL, "serverid invalid in config file");
	} else {
		db_params->serverid = 1;
	}

	return db_params;
}

void config_get_logfiles(serverConfig_t *config)
{
	field_t val;

	/* logfile */
	config_get_value("logfile", "DBMAIL", val);
	if (! strlen(val))
		g_strlcpy(config->log,DEFAULT_LOG_FILE, FIELDSIZE);
	else
		g_strlcpy(config->log, val, FIELDSIZE);
	assert(config->log);

	/* errorlog */
	config_get_value("errorlog", "DBMAIL", val);
	if (! strlen(val))
		g_strlcpy(config->error_log,DEFAULT_ERROR_LOG, FIELDSIZE);
	else
		g_strlcpy(config->error_log, val, FIELDSIZE);
	assert(config->error_log);

	/* pid directory */
	config_get_value("pid_directory", "DBMAIL", val);
	if (! strlen(val))
		g_strlcpy(config->pid_dir, DEFAULT_PID_DIR, FIELDSIZE);
	else
		g_strlcpy(config->pid_dir, val, FIELDSIZE);
	assert(config->pid_dir);
}

char * config_get_pidfile(serverConfig_t *config, const char *name)
{
	char *res;
	GString *s;
	res = g_build_filename(config->pid_dir, name, NULL);
	s = g_string_new("");
	g_string_printf(s, "%s%s", res, DEFAULT_PID_EXT);
	g_free(res);
	res = s->str;
	g_string_free(s,FALSE);
	return res;
}

