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
 * $Id$
 * \file config.c
 * \brief read configuration values from a config file
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmail.h"
#include "list.h"
#include "debug.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#define LINESIZE 1024

/* list of all config lists. Every item in this list holds a list for
 * the specific service [SERVICE NAME] 
 */
static struct list config_list;

/**
 */
struct service_config_list {
	char *service_name;
	struct list *config_items;
};

/**
 * static local function which gets config items for one service.
 * \param[in] name name of field to look for
 * \param[in] config_items list of configuration items for a service
 * \param[out] value will hold value of configuration item
 */
static int GetConfigValueConfigList(const field_t name, struct list *cfg_items,
				    field_t value);
/*
 * ReadConfig()
 *
 * builds up a linked list of configuration item name/value pairs based upon 
 * the content of the file cfilename.
 * items are given in "name=value" pairs, separated by newlines.
 *
 * a single config file can contain values for multiple services, each service
 * has to be started by "[SERVICE NAME]" and ended by an empty line.
 *
 * empty lines other than ending ones are ignored as is everything after a '#'
 *
 * returns 0 on succes, -1 on error unless CONFIG_ERROR_LEVEL is set TRACE_FATAL/TRACE_STOP;
 * if so the function will not return upon error but call exit().
 */
int ReadConfig(const char *serviceName, const char *cfilename)
{
	struct service_config_list *service_config;
	item_t item;
	char line[LINESIZE], *tmp, *value, service[LINESIZE];
	FILE *cfile = NULL;
	int serviceFound = 0, isCommentline;

	trace(TRACE_DEBUG, "ReadConfig(): starting procedure");

	if (!(service_config = malloc(sizeof(struct service_config_list)))) {
		trace(CONFIG_ERROR_LEVEL, "%s,%s: error allocating memory "
		      "for config list", __FILE__, __FUNCTION__);
		return -1;
	}
	if (!(service_config->config_items = malloc(sizeof(struct list)))) {
		trace(TRACE_ERROR, "%s,%s: unable to allocate memory "
		      "for config items", __FILE__, __FUNCTION__);
		return -1;
	}
	service_config->service_name = strdup(serviceName);

	(void) snprintf(service, LINESIZE, "[%s]", serviceName);

	list_init(service_config->config_items);

	if (!(cfile = fopen(cfilename, "r"))) {
		trace(CONFIG_ERROR_LEVEL,
		      "ReadConfig(): could not open config file [%s]",
		      cfilename);
		return -1;
	}

	do {
		if (fgets(line, LINESIZE, cfile) == NULL)
			break;

		if (feof(cfile) || ferror(cfile))
			break;

		/* chop whitespace front */
		for (tmp = line; (*tmp != '\0') && isspace(*tmp); tmp++);
		memmove(line, tmp, strlen(tmp));

		if (strncasecmp(line, service, strlen(service)) == 0) {
			/* ok entering config block */
			serviceFound = 1;

			trace(TRACE_DEBUG, "ReadConfig(): found %s tag",
			      service);
			memset(&item, 0, sizeof(item));

			while (1) {
				isCommentline = 0;

				if (fgets(line, LINESIZE, cfile) == NULL)
					break;

				if (feof(cfile) || ferror(cfile)
				    || strlen(line) == 0)
					break;

				if ((tmp = strchr(line, '#'))) {	/* remove comments */
					isCommentline = 1;
					*tmp = '\0';
				}

				/* chop whitespace front */
				for (tmp = line; ((*tmp != '\0') && 
						  isspace(*tmp));
				     tmp++);
				memmove(line, tmp, strlen(tmp));

				/* chop whitespace at end */
				for (tmp = &line[strlen(line) - 1];
				     tmp >= line && isspace(*tmp); tmp--)
					*tmp = '\0';

				if (strlen(line) == 0 && !isCommentline)
					break;	/* empty line specifies ending */

				if (!(tmp = strchr(line, '='))) {
					trace(TRACE_INFO,
					      "ReadConfig(): no value specified for service item [%s].",
					      line);
					continue;
				}

				*tmp = '\0';
				value = tmp + 1;

				strncpy(item.name, line, FIELDSIZE);
				strncpy(item.value, value, FIELDSIZE);

				if (!list_nodeadd
				    (service_config->config_items, &item, 
				     sizeof(item))) {
					trace(CONFIG_ERROR_LEVEL,
					      "ReadConfig(): could not add node");
					return -1;
				}

				trace(TRACE_DEBUG,
				      "ReadConfig(): item [%s] value [%s] added",
				      item.name, item.value);
			}
			trace(TRACE_DEBUG,
			      "ReadConfig(): service %s added", service);
		}

		/* skip otherwise */
	} while (!serviceFound);

	trace(TRACE_DEBUG,
	      "ReadConfig(): config for %s read, found [%ld] config_items",
	      service, service_config->config_items->total_nodes);
	if (fclose(cfile) != 0) 
		trace(TRACE_ERROR, "%s,%s: error closing file: [%s]",
		      __FILE__, __FUNCTION__, strerror(errno));
	
	if (!list_nodeadd(&config_list, service_config, 
			  sizeof(struct service_config_list))) {
		trace(CONFIG_ERROR_LEVEL,
		      "%s,%s: could not add config list",
		      __FILE__, __FUNCTION__);
		return -1;
	}
	return 0;
}

void config_free()
{
	struct element *el;
	struct service_config_list *scl;
	
	/* first free all "sublists" */
	el = list_getstart(&config_list);
	while(el) {
		scl = (struct service_config_list *) el->data;
		list_freelist(scl->config_items);
		el = el->nextnode;
	}
	
	/* free the complete list */
	list_freelist(&config_list);
}

int GetConfigValue(const field_t field_name, const char *service_name, 
		   field_t value) {
	struct element *el;
	struct service_config_list *scl;

	el = list_getstart(&config_list);
	while(el) {
		scl = (struct service_config_list *) el->data;
		if (!scl || strlen(scl->service_name) == 0) 
			trace(TRACE_INFO, "%s,%s: NULL config_list on "
			      "config list", __FILE__, __FUNCTION__);
		else {
			if (strcmp(scl->service_name, service_name) == 0) {
				/* found correct list */
				GetConfigValueConfigList(field_name,
							 scl->config_items,
							 value);
				return 0;
			}
		}
		el = el->nextnode;
	}
	
	trace(TRACE_DEBUG, "%s,%s config for service not found",
	      __FILE__, __FUNCTION__);
	return 0;
}
			
int GetConfigValueConfigList(const field_t name, struct list *config_items,
			     field_t value)
{
	item_t *item;
	struct element *el;
	int i = 0;

	assert(config_items);

	value[0] = '\0';
	
	trace(TRACE_DEBUG,
	      "GetConfigValue(): searching value for config item [%s]",
	      name);

	el = list_getstart(config_items);
	while (el) {
		item = (item_t *) el->data;
		trace(TRACE_DEBUG, "%s,%s i = %d",
		      __FILE__, __FUNCTION__, i++);


		if (!item || strlen(item->name) == 0 || 
		    strlen(item->value) == 0) {
			trace(TRACE_INFO,
			      "GetConfigValue(): NULL item %s in item-list",
			      item ? (strlen(item->name) > 0?
				      " value" : " name") : "");

			el = el->nextnode;
			continue;
		}

		if (strcasecmp(item->name, name) == 0) {
			trace(TRACE_DEBUG,
			      "GetConfigValue(): found value [%s]",
			      item->value);
			strncpy(value, item->value, FIELDSIZE);
			return 0;
		}

		el = el->nextnode;
	}

	trace(TRACE_DEBUG, "GetConfigValue(): item not found");
	return 0;
}

void SetTraceLevel(const char *service_name)
{
	field_t val;

	if (GetConfigValue("trace_level", service_name, val) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __FUNCTION__);
	if (strlen(val) == 0)
		configure_debug(TRACE_ERROR, 1, 0);
	else
		configure_debug(atoi(val), 1, 0);
}

void GetDBParams(db_param_t * db_params)
{
	field_t port_string;
	field_t sock_string;
	if (GetConfigValue("host", "DBMAIL", db_params->host) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __FUNCTION__);
	if (GetConfigValue("db", "DBMAIL", db_params->db) < 0) 
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __FUNCTION__);
	if (GetConfigValue("user", "DBMAIL", db_params->user) < 0) 
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __FUNCTION__);
	if (GetConfigValue("pass", "DBMAIL", db_params->pass) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __FUNCTION__);
	if (GetConfigValue("sqlport", "DBMAIL", port_string) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __FUNCTION__);
	if (GetConfigValue("sqlsocket", "DBMAIL", sock_string) < 0)
		trace(TRACE_FATAL, "%s,%s: error getting config!",
		      __FILE__, __FUNCTION__);

	/* check if port_string holds a value */
	if (strlen(port_string) != 0) {
		db_params->port =
		    (unsigned int) strtoul(port_string, NULL, 10);
		if (errno == EINVAL || errno == ERANGE)
			trace(TRACE_FATAL,
			      "%s,%s: wrong value for sqlport in "
			      "config file", __FILE__, __FUNCTION__);
	} else
		db_params->port = 0;

	/* same for sock_string */
	if (strlen(sock_string) != 0)
		strncpy(db_params->sock, sock_string, FIELDSIZE);
	else
		db_params->sock[0] = '\0';
}
