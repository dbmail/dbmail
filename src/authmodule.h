/* Dynamic loading of the authentication backend.
 * We use GLib's multiplatform dl() wrapper
 * to open up auth_sql.so or auth_ldap.so and
 * populate the global 'auth' structure.
 *
 * (c) 2005 Aaron Stone <aaron@serendipity.cx>
 */

#ifndef DM_AUTHMODULE_H
#define DM_AUTHMODULE_H

/* Prototypes must match with those in auth.h
 * and in the authentication drivers. */
typedef struct {
	int (* connect)(void);
	int (* disconnect)(void);
	int (* user_exists)(const char *username, uint64_t * user_idnr);
	char * (* get_userid)(uint64_t user_idnr);
	int (* check_userid)(uint64_t user_idnr);
	GList * (* get_known_users)(void);
	GList * (* get_known_aliases)(void);
	int (* getclientid)(uint64_t user_idnr, uint64_t * client_idnr);
	int (* getmaxmailsize)(uint64_t user_idnr, uint64_t * maxmail_size);
	char * (* getencryption)(uint64_t user_idnr);
	int (* check_user_ext)(const char *username, GList **userids, GList **fwds, int checks);
	int (* adduser)(const char *username, const char *password, const char *enctype,
			uint64_t clientid, uint64_t maxmail, uint64_t * user_idnr);
	int (* delete_user)(const char *username);
	int (* change_username)(uint64_t user_idnr, const char *new_name);
	int (* change_password)(uint64_t user_idnr,
			const char *new_pass, const char *enctype);
	int (* change_clientid)(uint64_t user_idnr, uint64_t new_cid);
	int (* change_mailboxsize)(uint64_t user_idnr, uint64_t new_size);
	int (* validate)(ClientBase_T *ci, const char *username, const char *password, uint64_t * user_idnr);
	uint64_t (* md5_validate)(ClientBase_T *ci, char *username,
			unsigned char *md5_apop_he, char *apop_stamp);
	int (* get_users_from_clientid)(uint64_t client_id,
			uint64_t ** user_ids, unsigned *num_users);
	char * (* get_deliver_from_alias)(const char *alias);
	GList * (* get_user_aliases)(uint64_t user_idnr);
	GList * (* get_aliases_ext)(const char *alias);
	int (* addalias)(uint64_t user_idnr, const char *alias, uint64_t clientid);
	int (* addalias_ext)(const char *alias, const char *deliver_to,
			uint64_t clientid);
	int (* removealias)(uint64_t user_idnr, const char *alias);
	int (* removealias_ext)(const char *alias, const char *deliver_to);
	gboolean (*requires_shadow_user)(void);
} auth_func_t;

#endif
