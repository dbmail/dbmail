/* Dynamic loading of the database backend.
 * We use GLib's multiplatform dl() wrapper
 * to open up sort_sieve.so and
 * populate the global 'sort' structure.
 *
 * (c) 2005 Aaron Stone <aaron@serendipity.cx>
 */

#ifndef SORTMODULE_H
#define SORTMODULE_H

/* Prototypes must match with those in sort.h
 * and in the sorting drivers. */
typedef struct {
	sort_result_t *(* process)(u64_t user_idnr, DbmailMessage *message);
	sort_result_t *(* validate)(u64_t user_idnr, char *scriptname);
	void (* free_result)(sort_result_t *result);
	const char *(* listextensions)(void);
	int (* get_cancelkeep)(sort_result_t *result);
	int (* get_reject)(sort_result_t *result);
	const char *(* get_mailbox)(sort_result_t *result);
	const char *(* get_errormsg)(sort_result_t *result);
	int (* get_error)(sort_result_t *result);
} sort_func_t;


#endif
