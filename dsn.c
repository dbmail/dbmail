/* Delivery User Functions
 * Aaron Stone, 9 Feb 2004 */

#include <stdlib.h>

#include "dsn.h"
#include "list.h"

void dsnuser_init(deliver_to_user_t *dsnuser)
{
        dsnuser->useridnr = 0;
        dsnuser->dsn.class = 0;
        dsnuser->dsn.subject = 0;
        dsnuser->dsn.detail = 0;

        dsnuser->address = NULL;
        dsnuser->mailbox = NULL;

        dsnuser->userids = (struct list *)malloc(sizeof(struct list));
        dsnuser->forwards = (struct list *)malloc(sizeof(struct list));

        list_init(dsnuser->userids);
        list_init(dsnuser->forwards);
}

void dsnuser_free(deliver_to_user_t *dsnuser)
{
        dsnuser->useridnr = 0;
        dsnuser->dsn.class = 0;
        dsnuser->dsn.subject = 0;
        dsnuser->dsn.detail = 0;

	/* These are nominally const, but
	 * we really do want to free them. */
        free((char *)dsnuser->address);
        free((char *)dsnuser->mailbox);

        list_freelist(&dsnuser->userids->start);
        list_freelist(&dsnuser->forwards->start);

        free(dsnuser->userids);
        free(dsnuser->forwards);
}

