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

#include "dbmail.h"
#include "dm_quota.h"

#define THIS_MODULE "QUOTA"


#define T Quota_T

#define QUOTA_ROOT_SIZE 128

/* A resource type.
 * RT_STORAGE:  "STORAGE"
 */
typedef enum {
	RT_STORAGE
} resource_type_t;

/* A resource limit.
 * type:  the type of the resource
 * usage: the current usage of the resource
 * limit: the maximum allowed usage of the resource
 */
typedef struct {
	resource_type_t type;
	uint64_t usage;
	uint64_t limit;
} resource_limit_t;

/* A quota root and its resource limits.
 * root:        the quota root, e.g. ""
 * n_resources: the number of limted resources under this quota root
 * resource[]:  an array with `n_resources' elements, each entry
 *              describing a resource limit
 */
struct T {
	char root[QUOTA_ROOT_SIZE];
	int n_resources;
	resource_limit_t resource[0];
};


/* Allocate a quota structure for `n_resources' resources. 
 * Returns NULL on failure.
 */
T quota_alloc(int n_resources)
{
	T quota;

	quota = g_malloc0(sizeof(*quota) + n_resources * sizeof(resource_limit_t));
	quota->n_resources = n_resources;

	return quota;
}

/* Set a resource limit in a quota structure.
 * quota:        the quota object to modify.
 * resource_idx: the index of the resource to modify.
 * type:         the type of the resource, e.g. RT_STORAGE.
 * usage:        the current usage of the resource.
 * limit:        the usage limit for the resource.
 */
void quota_set_resource_limit(T quota, int resource_idx,
			      resource_type_t type,
			      uint64_t usage, uint64_t limit)
{
	resource_limit_t *rl = &(quota->resource[resource_idx]);
	rl->type = type;
	rl->usage = usage;
	rl->limit = limit;
}

/* Set the name of the quota root.
 * Returns 0 on success, 1 on failure.
 * quota: the quota object to modify.
 * root:  the (new) name of the quota root.
 */
const char * quota_get_root(T quota)
{
	return (const char *)quota->root;
}

int quota_set_root(T quota, const char *root)
{
	memset(quota->root, 0, sizeof(quota->root));
	g_strlcpy(quota->root, root, sizeof(quota->root)-1);
	return 0;
}

uint64_t quota_get_limit(T quota)
{
	return quota->resource[0].limit;
}

uint64_t quota_get_usage(T quota)
{
	return quota->resource[0].usage;
}

/* Free a quota structure. */
void quota_free(T *quota)
{
	T q = *quota;
	g_free(q);
	q = NULL;
}


/* Get the quota root for a given mailbox.
 * Currently, the only supported quota root is "".
 * Returns the name of the quota root, or NULL on failure
 * (e.g. quota root not found). When NULL is returned, a pointer to 
 * an appropriate error message is stored in *errormsg.
 *   useridnr: the useridnr of the mailbox owner.
 *   mailbox:  the name of the mailbox.
 *   errormsg: will point to an error message if NULL is returned.
 */
const char *quota_get_quotaroot(uint64_t useridnr, const char *mailbox,
			  char **errormsg)
{
	uint64_t mailbox_idnr;

	if (! db_findmailbox(mailbox, useridnr, &mailbox_idnr)) {
		*errormsg = "mailbox not found";
		return NULL;
	}

	return "";
}

/* Get the quota for a given quota root.
 * Currently, the only supported quota root is "".
 * Returns a quota structure describing the given quota root,
 * or NULL on failure. When NULL is returned, a pointer to 
 * an appropriate error message is stored in *errormsg.
 *   useridnr:  the useridnr of the mailbox owner.
 *   quotaroot: the quotaroot.
 *   errormsg:  will point to an error message if NULL is returned.
 */
T quota_get_quota(uint64_t useridnr, const char *quotaroot, char **errormsg)
{
	T quota;
	uint64_t maxmail_size, usage;

	/* Currently, there's only the quota root "". */
	if (strcmp(quotaroot, "") != 0) {
		TRACE(TRACE_ERR, "unknown quota root \"%s\"", quotaroot);
		*errormsg = "unknown quota root";
		return NULL;
	}

	if (auth_getmaxmailsize(useridnr, &maxmail_size) == -1) {
		TRACE(TRACE_ERR, "auth_getmaxmailsize() failed\n");
		*errormsg = "invalid user";
		return NULL;
	}

	if (dm_quota_user_get(useridnr, &usage) == -1) {
		TRACE(TRACE_ERR, "dm_quota_user_get() failed\n");
		*errormsg = "internal error";
		return NULL;
	}

	/* We support exactly one resource: RT_STORAGE */
	quota = quota_alloc(1);

	/* Set quota root */
	if (quota_set_root(quota, quotaroot)) {
		TRACE(TRACE_ERR, "quota_set_root() failed\n");
		*errormsg = "out of memory";
		return NULL;
	}

	/* Set usage and limit for RT_STORAGE */
	quota_set_resource_limit(quota, 0, RT_STORAGE, usage,
				 maxmail_size);

	return quota;
}
