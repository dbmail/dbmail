/* Dynamic loading of the authentication backend.
 * We use GLib's multiplatform dl() wrapper
 * to open up auth_sql.so or auth_ldap.so and
 * populate the global 'auth' structure.
 *
 * (c) 2005 Aaron Stone <aaron@serendipity.cx>
 */

#ifndef AUTHMODULE_H
#define AUTHMODULE_H

/* Prototypes must match with those in auth.h
 * and in the authentication drivers. */
typedef struct {
	int (* connect)(void);
	int (* disconnect)(void);
	int (* user_exists)(const char *username, u64_t * user_idnr);
	char * (* get_userid)(u64_t user_idnr);
	int (* check_userid)(u64_t user_idnr);
	GList * (* get_known_users)(void);
	GList * (* get_known_aliases)(void);
	int (* getclientid)(u64_t user_idnr, u64_t * client_idnr);
	int (* getmaxmailsize)(u64_t user_idnr, u64_t * maxmail_size);
	char * (* getencryption)(u64_t user_idnr);
	int (* check_user_ext)(const char *username, struct dm_list *userids,
			struct dm_list *fwds, int checks);
	int (* adduser)(const char *username, const char *password, const char *enctype,
			u64_t clientid, u64_t maxmail, u64_t * user_idnr);
	int (* delete_user)(const char *username);
	int (* change_username)(u64_t user_idnr, const char *new_name);
	int (* change_password)(u64_t user_idnr,
			const char *new_pass, const char *enctype);
	int (* change_clientid)(u64_t user_idnr, u64_t new_cid);
	int (* change_mailboxsize)(u64_t user_idnr, u64_t new_size);
	int (* validate)(clientinfo_t *ci, char *username, char *password, u64_t * user_idnr);
	u64_t (* md5_validate)(clientinfo_t *ci, char *username,
			unsigned char *md5_apop_he, char *apop_stamp);
	int (* get_users_from_clientid)(u64_t client_id,
			u64_t ** user_ids, unsigned *num_users);
	char * (* get_deliver_from_alias)(const char *alias);
	GList * (* get_user_aliases)(u64_t user_idnr);
	GList * (* get_aliases_ext)(const char *alias);
	int (* addalias)(u64_t user_idnr, const char *alias, u64_t clientid);
	int (* addalias_ext)(const char *alias, const char *deliver_to,
			u64_t clientid);
	int (* removealias)(u64_t user_idnr, const char *alias);
	int (* removealias_ext)(const char *alias, const char *deliver_to);
	gboolean (*requires_shadow_user)(void);
} auth_func_t;

#endif
