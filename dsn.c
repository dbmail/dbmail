/* Delivery User Functions
 * Aaron Stone, 9 Feb 2004 */
/*
  $Id: dsn.c 2003 2006-03-01 21:02:56Z paul $

 Copyright (C) 2004 Aaron Stone aaron at serendipity dot cx

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

/* Enhanced Status Codes from RFC 1893
 * Nota Bene: should be updated to include
 * its successor, RFC 3463, too.
 * */


/* Top level codes */
static const char * const DSN_STRINGS_CLASS[] = {
/* nop */	"", "",
/* 2.. */	"Success",
/* nop */	"",
/* 4.. */	"Persistent Transient Failure",
/* 5.. */	"Permanent Failure"
};

/* Second the Third level codes */
static const char * const DSN_STRINGS_SUBJECT[] = {
/* nop */	"",
/* .1. */	"Address Status",
/* .2. */	"Mailbox Status",
/* .3. */	"Mail System Status",
/* .4. */	"Network and Routing Status",
/* .5. */	"Mail Delivery Protocol Status",
/* .6. */	"Message Content or Message Media Status",
/* .7. */	"Security or Policy Status"
};

/* Address Status */
static const char * const DSN_STRINGS_DETAIL_ONE[] = {
/* .1.0 */	"Other address status",
/* .1.1 */	"Bad destination mailbox address",
/* .1.2 */	"Bad destination system address",
/* .1.3 */	"Bad destination mailbox address syntax",
/* .1.4 */	"Destination mailbox address ambiguous",
/* .1.5 */	"Destination mailbox address valid",
/* .1.6 */	"Mailbox has moved",
/* .1.7 */	"Bad sender's mailbox address syntax",
/* .1.8 */	"Bad sender's system address"
};

/* Mailbox Status */
static const char * const DSN_STRINGS_DETAIL_TWO[] = {
/* .2.0 */	"Other or undefined mailbox status",
/* .2.1 */	"Mailbox disabled, not accepting messages",
/* .2.2 */	"Mailbox full",
/* .2.3 */	"Message length exceeds administrative limit",
/* .2.4 */	"Mailing list expansion problem"
};

/* Mail System Status */
static const char * const DSN_STRINGS_DETAIL_THREE[] = {
/* .3.0 */	"Other or undefined mail system status",
/* .3.1 */	"Mail system full",
/* .3.2 */	"System not accepting network messages",
/* .3.3 */	"System not capable of selected features",
/* .3.4 */	"Message too big for system"
};

/* Network and Routing Status */
static const char * const DSN_STRINGS_DETAIL_FOUR[] = {
/* .4.0 */	"Other or undefined network or routing status",
/* .4.1 */	"No answer from host",
/* .4.2 */	"Bad connection",
/* .4.3 */	"Routing server failure",
/* .4.4 */	"Unable to route",
/* .4.5 */	"Network congestion",
/* .4.6 */	"Routing loop detected",
/* .4.7 */	"Delivery time expired"
};

/* Mail Delivery Protocol Status */
static const char * const DSN_STRINGS_DETAIL_FIVE[] = {
/* .5.0 */	"Other or undefined protocol status",
/* .5.1 */	"Invalid command",
/* .5.2 */	"Syntax error",
/* .5.3 */	"Too many recipients",
/* .5.4 */	"Invalid command arguments",
/* .5.5 */	"Wrong protocol version"
};

/* Message Content or Message Media Status */
static const char * const DSN_STRINGS_DETAIL_SIX[] = {
/* .6.0 */	"Other or undefined media error",
/* .6.1 */	"Media not supported",
/* .6.2 */	"Conversion required and prohibited",
/* .6.3 */	"Conversion required but not supported",
/* .6.4 */	"Conversion with loss performed",
/* .6.5 */	"Conversion failed"
};

/* Security or Policy Status */
static const char * const DSN_STRINGS_DETAIL_SEVEN[] = {
/* .7.0 */	"Other or undefined security status",
/* .7.1 */	"Delivery not authorized, message refused",
/* .7.2 */	"Mailing list expansion prohibited",
/* .7.3 */	"Security conversion required but not possible",
/* .7.4 */	"Security features not supported",
/* .7.5 */	"Cryptographic failure",
/* .7.6 */	"Cryptographic algorithm not supported",
/* .7.7 */	"Message integrity failure"
};

/* Convert the DSN code into a descriptive message. 
 * Returns 0 on success, -1 on failure. */
int dsn_tostring(delivery_status_t dsn, const char ** const class,
                 const char ** const subject, const char ** const detail)
{
	if (dsn.class == 2 || dsn.class == 4 || dsn.class == 5)
		*class = DSN_STRINGS_CLASS[dsn.class];
	else
		return -1;

	switch (dsn.subject) {
		case 1:
			if (dsn.detail >= 0 && dsn.detail <= 8)
				*detail = DSN_STRINGS_DETAIL_ONE[dsn.detail];
			else
				return -1;
			break;
		case 2:
			if (dsn.detail >= 0 && dsn.detail <= 4)
				*detail = DSN_STRINGS_DETAIL_TWO[dsn.detail];
			else
				return -1;
			break;
		case 3:
			if (dsn.detail >= 0 && dsn.detail <= 4)
				*detail = DSN_STRINGS_DETAIL_THREE[dsn.detail];
			else
				return -1;
			break;
		case 4:
			if (dsn.detail >= 0 && dsn.detail <= 7)
				*detail = DSN_STRINGS_DETAIL_FOUR[dsn.detail];
			else
				return -1;
			break;
		case 5:
			if (dsn.detail >= 0 && dsn.detail <= 5)
				*detail = DSN_STRINGS_DETAIL_FIVE[dsn.detail];
			else
				return -1;
			break;
		case 6:
			if (dsn.detail >= 0 && dsn.detail <= 5)
				*detail = DSN_STRINGS_DETAIL_SIX[dsn.detail];
			else
				return -1;
			break;
		case 7:
			if (dsn.detail >= 0 && dsn.detail <= 7)
				*detail = DSN_STRINGS_DETAIL_SEVEN[dsn.detail];
			else
				return -1;
			break;
		default:
			return -1;
	}

	/* If we made it this far, then the subject was valid. */
	*subject = DSN_STRINGS_SUBJECT[dsn.subject];

	return 0;
}

int dsnuser_init(deliver_to_user_t * dsnuser)
{
	dsnuser->useridnr = 0;
	dsnuser->dsn.class = 0;
	dsnuser->dsn.subject = 0;
	dsnuser->dsn.detail = 0;

	dsnuser->address = NULL;
	dsnuser->mailbox = NULL;
	dsnuser->source = BOX_NONE;

	dsnuser->userids = (struct dm_list *) dm_malloc(sizeof(struct dm_list));
	if (dsnuser->userids == NULL)
		return -1;
	dsnuser->forwards = (struct dm_list *) dm_malloc(sizeof(struct dm_list));
	if (dsnuser->forwards == NULL) {
		dm_free(dsnuser->userids);
		return -1;
	}

	dm_list_init(dsnuser->userids);
	dm_list_init(dsnuser->forwards);

	trace(TRACE_DEBUG, "%s, %s: dsnuser initialized",
	      __FILE__, __func__);
	return 0;
}


void dsnuser_free(deliver_to_user_t * dsnuser)
{
	dsnuser->useridnr = 0;
	dsnuser->dsn.class = 0;
	dsnuser->dsn.subject = 0;
	dsnuser->dsn.detail = 0;

	dsnuser->address = NULL;
	dsnuser->mailbox = NULL;
	dsnuser->source = BOX_NONE;
	
	dm_list_free(&dsnuser->userids->start);
	dm_list_free(&dsnuser->forwards->start);

	dm_free(dsnuser->mailbox);
	dm_free(dsnuser->userids);
	dm_free(dsnuser->forwards);
	dm_free(dsnuser->address);

	trace(TRACE_DEBUG, "%s, %s: dsnuser freed",
	      __FILE__, __func__);
}

void set_dsn(delivery_status_t *dsn,
		int foo, int bar, int qux)
{
	dsn->class = foo;
	dsn->subject = bar;
	dsn->detail = qux;
}

static int address_has_alias(deliver_to_user_t *delivery)
{
	int alias_count;

	if (!delivery->address)
		return 0;

	alias_count = auth_check_user_ext(delivery->address,
				delivery->userids,
				delivery->forwards, 0);
	trace(TRACE_DEBUG, "%s, %s: user [%s] found total of [%d] aliases",
	      __FILE__, __func__, delivery->address, alias_count);

	if (alias_count > 0)
		return 1;

	return 0;
}

// address is in format username+mailbox@domain
// and we want to cut out the mailbox and produce
// an address in format username@domain, then check it.
static int address_has_alias_mailbox(deliver_to_user_t *delivery)
{
	int alias_count;
	char *newaddress;
	size_t newaddress_len, zapped_len;

	if (!delivery->address)
		return 0;

	if (zap_between(delivery->address, -'+', '@', &newaddress,
			&newaddress_len, &zapped_len) != 0)
		return 0;

	alias_count = auth_check_user_ext(newaddress,
				delivery->userids,
				delivery->forwards, 0);
	trace(TRACE_DEBUG, "%s, %s: user [%s] found total of [%d] aliases",
	      __FILE__, __func__, newaddress, alias_count);

	dm_free(newaddress);

	if (alias_count > 0)
		return 1;

	return 0;
}

static int address_is_username_mailbox(deliver_to_user_t *delivery)
{
	int user_exists;
	u64_t userid;
	char *newaddress;
	size_t newaddress_len, zapped_len;

	if (!delivery->address)
		return 0;

	if (zap_between(delivery->address, -'+', '@', &newaddress,
			&newaddress_len, &zapped_len) != 0)
		return 0;

	user_exists = auth_user_exists(newaddress, &userid);

	if (user_exists < 0) {
		/* An error occurred. */
		trace(TRACE_ERROR, "%s, %s: error checking user [%s]",
		      __FILE__, __func__, newaddress);
		dm_free(newaddress);
		return -1;
	}

	if (user_exists == 0) {
		/* User does not exist. */
		trace(TRACE_INFO, "%s, %s: username not found [%s]",
		      __FILE__, __func__, newaddress);
		dm_free(newaddress);
		return 0;
	}

	if (dm_list_nodeadd(delivery->userids, &userid, sizeof(u64_t)) == 0) {
		trace(TRACE_ERROR, "%s, %s: out of memory",
		      __FILE__, __func__);
		dm_free(newaddress);
		return -1;
	}

	trace(TRACE_DEBUG, "%s, %s: added user [%s] id [%llu] to delivery list",
		__FILE__, __func__, newaddress, userid);

	dm_free(newaddress);
	return 1;
}

static int address_is_username(deliver_to_user_t *delivery)
{
	int user_exists;
	u64_t userid;

	if (!delivery->address)
		return 0;

	user_exists = auth_user_exists(delivery->address, &userid);

	if (user_exists < 0) {
		/* An error occurred. */
		trace(TRACE_ERROR, "%s, %s: error checking user [%s]",
		      __FILE__, __func__, delivery->address);
		return -1;
	}

	if (user_exists == 0) {
		/* User does not exist. */
		trace(TRACE_INFO, "%s, %s: username not found [%s]",
		      __FILE__, __func__, delivery->address);
		return 0;
	}

	if (dm_list_nodeadd(delivery->userids, &userid, sizeof(u64_t)) == 0) {
		trace(TRACE_ERROR, "%s, %s: out of memory",
		      __FILE__, __func__);
		return -1;
	}

	trace(TRACE_DEBUG, "%s, %s: added user [%s] id [%llu] to delivery list",
		__FILE__, __func__, delivery->address, userid);

	return 1;
}

static int address_is_domain_catchall(deliver_to_user_t *delivery)
{
	char *domain;
	int domain_count;

	if (!delivery->address)
		return 0;

	trace(TRACE_INFO, "%s, %s: user [%s] checking for domain forwards.",
		__FILE__, __func__, delivery->address);

	domain = strchr(delivery->address, '@');

	if (domain == NULL) {
		return 0;
	}

	trace(TRACE_DEBUG, "%s, %s: domain [%s] checking for domain forwards",
		__FILE__, __func__, domain);

	/* Checking for domain aliases */
	domain_count = auth_check_user_ext(domain, delivery->userids,
			delivery->forwards, 0);
	trace(TRACE_DEBUG, "%s, %s: domain [%s] found total of [%d] aliases",
		__FILE__, __func__, domain, domain_count);

	if (domain_count == 0) {
		return 0;
	}

	return 1;
}

static int address_is_userpart_catchall(deliver_to_user_t *delivery)
{
	char *userpart = dm_strdup(delivery->address);
	char *userpartcut;
	int userpart_count;

	if (!delivery->address)
		return 0;

	trace(TRACE_INFO, "%s, %s: user [%s] checking for userpart forwards.",
		__FILE__, __func__, userpart);

	userpartcut = strchr(userpart, '@');

	if (userpartcut == NULL) {
		return 0;
	}

	/* Stomp _after_ the @-sign. */
	*(userpartcut + 1) = '\0';

	trace(TRACE_DEBUG, "%s, %s: userpart [%s] checking for userpart forwards",
		__FILE__, __func__, userpart);

	/* Checking for userpart aliases */
	userpart_count = auth_check_user_ext(userpart, delivery->userids,
			delivery->forwards, 0);
	trace(TRACE_DEBUG, "%s, %s: userpart [%s] found total of [%d] aliases",
		__FILE__, __func__, userpart, userpart_count);

	if (userpart_count == 0) {
		return 0;
	}

	return 1;
}

void dsnuser_free_list(struct dm_list *deliveries)
{
	struct element *tmp;

	for (tmp = dm_list_getstart(deliveries); tmp != NULL;
	     tmp = tmp->nextnode)
		dsnuser_free((deliver_to_user_t *) tmp->data);

	dm_list_free(&deliveries->start);
}

delivery_status_t dsnuser_worstcase_int(int ok, int temp, int fail, int fail_quota)
{
	delivery_status_t dsn;
	
	dsn.class = DSN_CLASS_NONE;
	dsn.subject = 0;
	dsn.detail = 0;

	if (ok)
		dsn.class = DSN_CLASS_OK;
	if (fail_quota)
		dsn.class = DSN_CLASS_QUOTA;
	if (fail)
		dsn.class = DSN_CLASS_FAIL;
	if (temp)
		dsn.class = DSN_CLASS_TEMP;
	
	return dsn;
}

delivery_status_t dsnuser_worstcase_list(struct dm_list * deliveries)
{
	delivery_status_t dsn;
	struct element *tmp;
	int ok = 0, temp = 0, fail = 0, fail_quota = 0;
	

	/* Get one reasonable error code for everyone. */
	for (tmp = dm_list_getstart(deliveries); tmp != NULL;
	     tmp = tmp->nextnode) {
		dsn = ((deliver_to_user_t *) tmp->data)->dsn;
		switch (dsn.class) {
		case DSN_CLASS_OK:
			/* Success. */
			ok = 1;
			break;
		case DSN_CLASS_TEMP:
			/* Temporary transient failure. */
			temp = 1;
			break;
		case DSN_CLASS_FAIL:
		case DSN_CLASS_QUOTA:
			/* Permanent failure. */
			if (dsn.subject == 2)
				fail_quota = 1;
			else
				fail = 1;
			break;
		case DSN_CLASS_NONE:
			/* Nothing doing. */
			break;
		}
	}

	/* If we never made it into the list, all zeroes will
	 * yield a temporary failure, which is pretty reasonable. */
	return dsnuser_worstcase_int(ok, temp, fail, fail_quota);
}

int dsnuser_resolve_list(struct dm_list *deliveries)
{
	int ret;
	struct element *element;

	/* Loop through the users list */
	for (element = dm_list_getstart(deliveries); element != NULL;
	     element = element->nextnode) {
		if ((ret = dsnuser_resolve((deliver_to_user_t *) element->data)) != 0) {
			return ret;
		}
	}

	return 0;
}

int dsnuser_resolve(deliver_to_user_t *delivery)
{
	/* If the userid is already set, then we're doing direct-to-userid.
	 * We just want to make sure that the userid actually exists... */
	if (delivery->useridnr != 0) {

		trace(TRACE_INFO, "%s, %s: checking if [%llu] is a valid useridnr.",
				__FILE__, __func__, delivery->useridnr);

		switch (auth_check_userid(delivery->useridnr)) {
		case -1:
			/* Temp fail. Address related. D.N.E. */
			set_dsn(&delivery->dsn, DSN_CLASS_TEMP, 1, 1);
			trace(TRACE_INFO, "%s, %s: useridnr [%llu] temporary lookup failure.",
					__FILE__, __func__, delivery->useridnr);
			break;
		case 1:
			/* Failure. Address related. D.N.E. */
			set_dsn(&delivery->dsn, DSN_CLASS_FAIL, 1, 1);
			trace(TRACE_INFO, "%s, %s: useridnr [%llu] does not exist.",
					__FILE__, __func__, delivery->useridnr);
			break;
		case 0:
			/* Copy the delivery useridnr into the userids list. */
			if (dm_list_nodeadd(delivery->userids, &delivery->useridnr,
			     sizeof(delivery->useridnr)) == 0) {
				trace(TRACE_ERROR, "%s, %s: out of memory",
				      __FILE__, __func__);
				return -1;
			}

			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			trace(TRACE_INFO, "%s, %s: delivery [%llu] directly to a useridnr.",
					__FILE__, __func__, delivery->useridnr);
			break;
		}
	/* Ok, we don't have a useridnr, maybe we have an address? */
	} else if (strlen(delivery->address) > 0) {

		trace(TRACE_INFO, "%s, %s: checking if [%s] is a valid username, alias, or catchall.",
				__FILE__, __func__, delivery->address);

		if (address_has_alias(delivery))  {
			/* The address had aliases and they've
			 * been resolved into the delivery struct. */
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			trace(TRACE_INFO, "%s, %s: delivering [%s] as an alias.",
					__FILE__, __func__, delivery->address);

		} else if (address_has_alias_mailbox(delivery)) {
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			trace(TRACE_INFO, "%s, %s: delivering [%s] as an alias with mailbox.",
					__FILE__, __func__, delivery->address);

		} else if (address_is_username(delivery)) {
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			trace(TRACE_INFO, "%s, %s: delivering [%s] as a username.",
					__FILE__, __func__, delivery->address);

		} else if (address_is_username_mailbox(delivery)) {
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			trace(TRACE_INFO, "%s, %s: delivering [%s] as a username with mailbox.",
					__FILE__, __func__, delivery->address);
			
		} else if (address_is_domain_catchall(delivery)) {
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			trace(TRACE_INFO, "%s, %s: delivering [%s] as a domain catchall.",
					__FILE__, __func__, delivery->address);

		} else if (address_is_userpart_catchall(delivery)) {
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			trace(TRACE_INFO, "%s, %s: delivering [%s] as a userpart catchall.",
					__FILE__, __func__, delivery->address);

		} else {
			/* Failure. Address related. D.N.E. */
			set_dsn(&delivery->dsn, DSN_CLASS_FAIL, 1, 1);
			trace(TRACE_INFO, "%s, %s: could not find [%s] at all.",
					__FILE__, __func__, delivery->address);
		}

	/* Neither useridnr nor address.
	 * Something is wrong upstream. */
	} else {

		trace(TRACE_ERROR, "%s, %s: this delivery had neither useridnr nor address.",
				__FILE__, __func__);

		return -1;
	}

	return 0;
}

