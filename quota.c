#include <stdlib.h>
#include <string.h>
#include "db.h"
#include "auth.h"
#include "quota.h"

/* Allocate a quota structure for `n_resources' resources. 
 * Returns NULL on failure.
 */
quota_t *quota_alloc(int n_resources) {
  quota_t *quota;

  quota = malloc(sizeof(quota_t) + n_resources * sizeof(resource_limit_t));
  if (quota != NULL) {
    quota->root = NULL;
    quota->n_resources = n_resources;	
  }

  return quota;
}

/* Set a resource limit in a quota structure.
 * quota:        the quota object to modify.
 * resource_idx: the index of the resource to modify.
 * type:         the type of the resource, e.g. RT_STORAGE.
 * usage:        the current usage of the resource.
 * limit:        the usage limit for the resource.
 */
void quota_set_resource_limit(quota_t *quota, int resource_idx,
			      resource_type_t type,
			      u64_t usage, u64_t limit) {
    resource_limit_t * rl = &(quota->resource[resource_idx]);
    rl->type  = type;
    rl->usage = usage;
    rl->limit = limit;
}

/* Set the name of the quota root.
 * Returns 0 on success, 1 on failure.
 * quota: the quota object to modify.
 * root:  the (new) name of the quota root.
 */
int quota_set_root(quota_t *quota, char *root) {
    free(quota->root);
    quota->root = strdup(root);
    return (quota->root == NULL);
}

/* Free a quota structure. */
void quota_free(quota_t *quota) {
    free(quota->root);
    free(quota);
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
char *quota_get_quotaroot(u64_t useridnr, const char *mailbox,
			  char **errormsg) {
    if (db_get_mailboxid(useridnr, mailbox) == 0) {
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
quota_t *quota_get_quota(u64_t useridnr, char *quotaroot, char **errormsg) {
    quota_t *quota;
    u64_t maxmail_size, usage;
    
    /* Currently, there's only the quota root "". */
    if (strcmp(quotaroot, "") != 0) {
	trace(TRACE_ERROR, "quota_get_quota(): unknown quota root \"%s\"\n",
	      quotaroot);
	*errormsg = "unknown quota root";
	return NULL;
    }
    
    maxmail_size = auth_getmaxmailsize(useridnr);
    if (maxmail_size < 0) {
	trace(TRACE_ERROR, "quota_get_quota(): auth_getmaxmailsize() failed\n");
	*errormsg = "invalid user";
	return NULL;
    }
    
    usage = db_get_quotum_used(useridnr);
    if (usage == -1) {
	trace(TRACE_ERROR, "quota_get_quota(): db_get_quotum_used() failed\n");
	*errormsg = "internal error";
	return NULL;	
    }
    
    /* We support exactly one resource: RT_STORAGE */
    quota = quota_alloc(1);
    if (quota == NULL) {
	trace(TRACE_ERROR, "quota_get_quota(): out of memory\n");
	*errormsg = "out of memory";
	return NULL;
    }
    
    /* Set quota root */
    if (quota_set_root(quota, quotaroot)) {
	trace(TRACE_ERROR, "quota_get_quota(): quota_set_root() failed\n");
	*errormsg = "out of memory";
	return NULL;
    }
    
    /* Set usage and limit for RT_STORAGE */
    quota_set_resource_limit(quota, 0, RT_STORAGE, usage, maxmail_size);
    
    return quota;
}
