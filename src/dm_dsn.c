/* Delivery User Functions
 * Aaron Stone, 9 Feb 2004 */
/*
 Copyright (C) 2004 Aaron Stone aaron at serendipity dot cx
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
#define THIS_MODULE "dsn"

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

/* Empty subject */
static const char * const DSN_STRINGS_DETAIL_ZERO[] = {
/* .0.0 */	""
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

/* MAGIC: max detail indexes for each subject detail array */
static int DSN_STRINGS_DETAIL_VALID[] = { 0, 8, 4, 4, 7, 5 ,5, 7 };

static const char * const * const DSN_STRINGS_DETAIL[] = {
	DSN_STRINGS_DETAIL_ZERO,
	DSN_STRINGS_DETAIL_ONE,
	DSN_STRINGS_DETAIL_TWO,
	DSN_STRINGS_DETAIL_THREE,
	DSN_STRINGS_DETAIL_FOUR,
	DSN_STRINGS_DETAIL_FIVE,
	DSN_STRINGS_DETAIL_SIX,
	DSN_STRINGS_DETAIL_SEVEN,
};

/* Convert the DSN code into a descriptive message. 
 * Returns 0 on success, -1 on failure. */
int dsn_tostring(delivery_status_t dsn, const char ** const class,
                 const char ** const subject, const char ** const detail)
{
	assert(class); assert(subject); assert(detail);
	*class = *subject = *detail = NULL;
	
	if (dsn.class == 2 || dsn.class == 4 || dsn.class == 5)
		*class = DSN_STRINGS_CLASS[dsn.class];
	
	if (dsn.subject >= 0 && dsn.subject <= 7) /* MAGIC: max index of DSN_STRINGS_SUBJECT */
		*subject = DSN_STRINGS_SUBJECT[dsn.subject];

	if (dsn.subject >= 0 && dsn.subject <= 7 /* MAGIC: max index of DSN_STRINGS_SUBJECT */
		 && dsn.detail >= 0 && dsn.detail <= DSN_STRINGS_DETAIL_VALID[dsn.subject])
		*detail = DSN_STRINGS_DETAIL[dsn.subject][dsn.detail];

	if (!*class || !*subject || !*detail) {
		TRACE(TRACE_INFO, "Invalid dsn code received [%d][%d][%d]",
			dsn.class, dsn.subject, dsn.detail);
		/* Allow downstream code to assume the return pointers
		 * are valid strings, though not particularly useful. */
		*class = *subject = *detail = "";
		return -1;
	}

	return 0;
}


int dsnuser_init(Delivery_T * dsnuser)
{
	dsnuser->useridnr = 0;
	dsnuser->dsn.class = 0;
	dsnuser->dsn.subject = 0;
	dsnuser->dsn.detail = 0;

	dsnuser->address = NULL;
	dsnuser->mailbox = NULL;
	dsnuser->source = BOX_NONE;

	dsnuser->userids = NULL;
	dsnuser->forwards = NULL;

	TRACE(TRACE_DEBUG, "dsnuser initialized");
	return 0;
}


void dsnuser_free(Delivery_T * dsnuser)
{
	dsnuser->useridnr = 0;
	dsnuser->dsn.class = 0;
	dsnuser->dsn.subject = 0;
	dsnuser->dsn.detail = 0;
	dsnuser->source = BOX_NONE;

	g_list_destroy(dsnuser->userids);
	g_list_destroy(dsnuser->forwards);

	if (dsnuser->address) {
		g_free(dsnuser->address);
		dsnuser->address = NULL;
	}
	if (dsnuser->mailbox) {
		g_free(dsnuser->mailbox);
		dsnuser->mailbox = NULL;
	}
	
	TRACE(TRACE_DEBUG, "dsnuser freed");
}

void set_dsn(delivery_status_t *dsn,
		int foo, int bar, int qux)
{
	dsn->class = foo;
	dsn->subject = bar;
	dsn->detail = qux;
}

static int address_has_alias(Delivery_T *delivery)
{
	int alias_count;

	if (!delivery->address)
		return 0;

	alias_count = auth_check_user_ext(delivery->address, &delivery->userids, &delivery->forwards, 0);
	TRACE(TRACE_DEBUG, "user [%s] found total of [%d] aliases", delivery->address, alias_count);

	if (alias_count > 0)
		return 1;

	return 0;
}

// address is in format username+mailbox@domain
// and we want to cut out the mailbox and produce
// an address in format username@domain, then check it.
static int address_has_alias_mailbox(Delivery_T *delivery)
{
	int alias_count;
	char *newaddress;
	size_t newaddress_len, zapped_len;

	if (!delivery->address)
		return 0;

	if (zap_between(delivery->address, -'+', '@', &newaddress,
			&newaddress_len, &zapped_len) != 0)
		return 0;

	alias_count = auth_check_user_ext(newaddress, &delivery->userids, &delivery->forwards, 0);
	TRACE(TRACE_DEBUG, "user [%s] found total of [%d] aliases", newaddress, alias_count);

	g_free(newaddress);

	if (alias_count > 0)
		return 1;

	return 0;
}

static int address_is_username_mailbox(Delivery_T *delivery)
{
	uint64_t userid, *uid;
	char *newaddress;
	size_t newaddress_len, zapped_len;

	if (!delivery->address)
		return 0;

	if (zap_between(delivery->address, -'+', '@', &newaddress,
			&newaddress_len, &zapped_len) != 0)
		return 0;

	if (! auth_user_exists(newaddress, &userid)) {
		/* User does not exist. */
		TRACE(TRACE_INFO, "username not found [%s]", newaddress);
		g_free(newaddress);
		return 0;
	}

	uid = g_new0(uint64_t,1);
	*uid = userid;
	delivery->userids = g_list_prepend(delivery->userids, uid);

	TRACE(TRACE_DEBUG, "added user [%s] id [%" PRIu64 "] to delivery list", newaddress, userid);

	g_free(newaddress);
	return 1;
}

static int address_is_username(Delivery_T *delivery)
{
	uint64_t userid, *uid;

	if (!delivery->address)
		return 0;

	if (! auth_user_exists(delivery->address, &userid)) {
		/* User does not exist. */
		TRACE(TRACE_INFO, "username not found [%s]", delivery->address);
		return 0;
	}

	uid = g_new0(uint64_t,1);
	*uid = userid;
	delivery->userids = g_list_prepend(delivery->userids, uid);

	TRACE(TRACE_DEBUG, "added user [%s] id [%" PRIu64 "] to delivery list", delivery->address, userid);

	return 1;
}

static int address_is_domain_catchall(Delivery_T *delivery)
{
	char *domain;
	int domain_count;

	if (!delivery->address)
		return 0;

	TRACE(TRACE_INFO, "user [%s] checking for domain forwards.", delivery->address);

	domain = strchr(delivery->address, '@');

	if (domain == NULL) {
		return 0;
	}

	char *my_domain = g_strdup(domain);
	char *my_domain_dot = my_domain;

	while (1) {
		TRACE(TRACE_DEBUG, "domain [%s] checking for domain forwards", my_domain);
        
		/* Checking for domain aliases */
		domain_count = auth_check_user_ext(my_domain, &delivery->userids, &delivery->forwards, 0);
        
		if (domain_count > 0) {
			/* This is the way to succeed out. */
			break;
		}

		/* On each loop, lop off a chunk between @ and . */
		my_domain_dot = strchr(my_domain, '.');

		if (!my_domain_dot || my_domain_dot == my_domain) {
			/* This is one way to fail out, it means we have
			 * somethign like @foo or a missing at-sign. */
			break;
		}

		if (my_domain_dot == my_domain + 1) {
			/* We're looking at something like @.foo.bar.qux,
			 * and my_domain_dot is pointed at .foo.bar.qux,
			 * so we have to look one more character ahead. */
			my_domain_dot = strchr(my_domain_dot + 1, '.');
			if (!my_domain_dot) {
				/* We're looking at @. so we're done. */
				break;
			}
		}

		/* Copy everything from the next dot to one after the at-sign,
		 * including the trailing nul byte. */
		int move_len = strlen(my_domain_dot);
		memmove(my_domain + 1, my_domain_dot, move_len + 1);
	}

	TRACE(TRACE_DEBUG, "domain [%s] found total of [%d] aliases", my_domain, domain_count);
	g_free(my_domain);
	if (domain_count > 0)
		return 1;

	return 0;
}

static int address_is_userpart_catchall(Delivery_T *delivery)
{
	char *userpart;
	char *userpartcut;
	int userpart_count;

	if (!delivery->address)
		return 0;

	userpart = g_strdup(delivery->address);
	TRACE(TRACE_INFO, "user [%s] checking for userpart forwards.", userpart);

	userpartcut = strchr(userpart, '@');

	if (userpartcut == NULL) {
		g_free(userpart);
		return 0;
	}

	/* Stomp _after_ the @-sign. */
	*(userpartcut + 1) = '\0';

	TRACE(TRACE_DEBUG, "userpart [%s] checking for userpart forwards", userpart);

	/* Checking for userpart aliases */
	userpart_count = auth_check_user_ext(userpart, &delivery->userids, &delivery->forwards, 0);
	TRACE(TRACE_DEBUG, "userpart [%s] found total of [%d] aliases", userpart, userpart_count);

	g_free(userpart);

	if (userpart_count == 0)
		return 0;

	return 1;
}

void dsnuser_free_list(List_T deliveries)
{
	if (! deliveries)
		return;
	deliveries = p_list_first(deliveries);
	while (deliveries) {
		Delivery_T * dsnuser = (Delivery_T *)p_list_data(deliveries);
		if (dsnuser) {
			dsnuser_free(dsnuser);
			g_free(dsnuser);
		}
		if (! p_list_next(deliveries))
			break;
		deliveries = p_list_next(deliveries);
	}
	deliveries = p_list_first(deliveries);
	p_list_free(&deliveries);
}

dsn_class_t dsnuser_worstcase_int(int ok, int temp, int fail, int fail_quota)
{
	if (temp)
		return DSN_CLASS_TEMP;
	if (fail)
		return DSN_CLASS_FAIL;
	if (fail_quota)
		return DSN_CLASS_QUOTA;
	if (ok)
		return DSN_CLASS_OK;
	return DSN_CLASS_NONE;
}

dsn_class_t dsnuser_worstcase_list(List_T deliveries)
{
	delivery_status_t dsn;
	int ok = 0, temp = 0, fail = 0, fail_quota = 0;

	deliveries = p_list_first(deliveries);
	
	/* Get one reasonable error code for everyone. */
	while (deliveries) {
		dsn = ((Delivery_T *)p_list_data(deliveries))->dsn;
		switch (dsn.class) {
		case DSN_CLASS_OK:	/* Success. */
			ok = 1;
			break;
		case DSN_CLASS_TEMP:	/* Temporary transient failure. */
			temp = 1;
			break;
		case DSN_CLASS_FAIL:
		case DSN_CLASS_QUOTA:	/* Permanent failure. */
			if (dsn.subject == 2)
				fail_quota = 1;
			else
				fail = 1;
			break;
		case DSN_CLASS_NONE:	/* Nothing doing. */
			break;
		}
		if (! p_list_next(deliveries))
			break;
		deliveries = p_list_next(deliveries);
	}

	/* If we never made it into the list, all zeroes will
	 * yield a temporary failure, which is pretty reasonable. */
	return dsnuser_worstcase_int(ok, temp, fail, fail_quota);
}

int dsnuser_resolve_list(List_T deliveries)
{
	int ret;

	deliveries = p_list_first(deliveries);

	/* Loop through the users list */
	while (deliveries) {
		if ((ret = dsnuser_resolve((Delivery_T *)p_list_data(deliveries))) != 0)
			return ret;
		if (! p_list_next(deliveries))
			break;
		deliveries = p_list_next(deliveries);
	}

	return 0;
}

int dsnuser_resolve(Delivery_T *delivery)
{
	uint64_t *uid;
	/* If the userid is already set, then we're doing direct-to-userid.
	 * We just want to make sure that the userid actually exists... */
	if (delivery->useridnr != 0) {

		TRACE(TRACE_INFO, "checking if [%" PRIu64 "] is a valid useridnr.", delivery->useridnr);

		switch (auth_check_userid(delivery->useridnr)) {
		case -1:
			/* Temp fail. Address related. D.N.E. */
			set_dsn(&delivery->dsn, DSN_CLASS_TEMP, 1, 1);
			TRACE(TRACE_INFO, "useridnr [%" PRIu64 "] temporary lookup failure.", delivery->useridnr);
			break;
		case 1:
			/* Failure. Address related. D.N.E. */
			set_dsn(&delivery->dsn, DSN_CLASS_FAIL, 1, 1);
			TRACE(TRACE_INFO, "useridnr [%" PRIu64 "] does not exist.", delivery->useridnr);
			break;
		case 0:
			/* Copy the delivery useridnr into the userids list. */
			uid = g_new0(uint64_t,1);
			*uid = delivery->useridnr;
			delivery->userids = g_list_prepend(delivery->userids, uid);

			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			TRACE(TRACE_INFO, "delivery [%" PRIu64 "] directly to a useridnr.", delivery->useridnr);
			break;
		}
	/* Ok, we don't have a useridnr, maybe we have an address? */
	} else if (strlen(delivery->address) > 0) {

		TRACE(TRACE_INFO, "checking if [%s] is a valid username, alias, or catchall.", delivery->address);

		if (address_has_alias(delivery))  {
			/* The address had aliases and they've
			 * been resolved into the delivery struct. */
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			TRACE(TRACE_INFO, "delivering [%s] as an alias.", delivery->address);

		} else if (address_has_alias_mailbox(delivery)) {
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			TRACE(TRACE_INFO, "delivering [%s] as an alias with mailbox.", delivery->address);

		} else if (address_is_username(delivery)) {
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			TRACE(TRACE_INFO, "delivering [%s] as a username.", delivery->address);

		} else if (address_is_username_mailbox(delivery)) {
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			TRACE(TRACE_INFO, "delivering [%s] as a username with mailbox.", delivery->address);
			
		} else if (address_is_domain_catchall(delivery)) {
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			TRACE(TRACE_INFO, "delivering [%s] as a domain catchall.", delivery->address);

		} else if (address_is_userpart_catchall(delivery)) {
			/* Success. Address related. Valid. */
			set_dsn(&delivery->dsn, DSN_CLASS_OK, 1, 5);
			TRACE(TRACE_INFO, "delivering [%s] as a userpart catchall.", delivery->address);

		} else {
			/* Failure. Address related. D.N.E. */
			set_dsn(&delivery->dsn, DSN_CLASS_FAIL, 1, 1);
			TRACE(TRACE_INFO, "could not find [%s] at all.", delivery->address);
		}

	/* Neither useridnr nor address.
	 * Something is wrong upstream. */
	} else {

		TRACE(TRACE_ERR, "this delivery had neither useridnr nor address.");

		return -1;
	}

	return 0;
}

