#ifndef _DBMAIL_QUOTA_H
#define _DBMAIL_QUOTA_H

#include "dbmailtypes.h"


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
    u64_t usage;
    u64_t limit;
} resource_limit_t;

/* A quota root and its resource limits.
 * root:        the quota root, e.g. ""
 * n_resources: the number of limted resources under this quota root
 * resource[]:  an array with `n_resources' elements, each entry
 *              describing a resource limit
 */
typedef struct {
    char *root;
    int n_resources;
    resource_limit_t resource[0];
} quota_t;


/* Functions for manipulating quota_t objects */
quota_t *quota_alloc(int n_resources);
void quota_free(quota_t *quota);
void quota_set_resource_limit(quota_t *quota, int resource_idx,
			      resource_type_t type,
			      u64_t usage, u64_t limit);
int quota_set_root(quota_t *quota, char *root);

/* Functions for querying quota and quota root */
char *quota_get_quotaroot(u64_t useridnr, const char *mailbox,
			  char **errormsg);
quota_t *quota_get_quota(u64_t useridnr, char *quotaroot, char **errormsg);


#endif
