/* Dynamic loading of the database backend.
 * We use GLib's multiplatform dl() wrapper
 * to open up sort_sieve.so and
 * populate the global 'sort' structure.
 *
 * (c) 2005 Aaron Stone <aaron@serendipity.cx>
 */

#ifndef DM_SORTMODULE_H
#define DM_SORTMODULE_H

/* Prototypes must match with those in sort.h
 * and in the sorting drivers. */
typedef struct {
	SortResult_T *(* process)(uint64_t user_idnr, DbmailMessage *message, const char *mailbox);
	SortResult_T *(* validate)(uint64_t user_idnr, char *scriptname);
	void (* free_result)(SortResult_T *result);
	const char *(* listextensions)(void);
	int (* get_cancelkeep)(SortResult_T *result);
	int (* get_reject)(SortResult_T *result);
	const char *(* get_mailbox)(SortResult_T *result);
	const char *(* get_errormsg)(SortResult_T *result);
	int (* get_error)(SortResult_T *result);
} sort_func;


#endif
