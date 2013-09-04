/*
 Copyright (c) 2006 Aaron Stone <aaron@serendipity.cx>

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

static char **weird_tokenize(char *pat, char *sep)
{
	char **parts;
	char *pat_begin, *pat_end;
	int t = 0, tokens = 1;

	// Count the number of separators in pat
	for (pat_end = pat; pat_end[0] != '\0'; pat_end++) {
		if (strchr(sep, pat_end[0])) {
			tokens+=2;
		}
	}

	// Mask the original pat.
	pat = g_strdup(pat);
	parts = g_new0(char *, tokens + 1);

	// Catch the degenerate case.
	if (tokens == 1) {
		parts[0] = pat;
		return parts;
	}

	// Break pat by sep into parts.
	for (pat_begin = pat_end = pat; pat_end[0] != '\0'; pat_end++) {
		if (strchr(sep, pat_end[0])) {
			if (pat_begin == pat_end) {
				parts[t] = g_strdup(" ");
				parts[t][0] = pat_end[0];
				pat_end[0] = '\0';
				pat_begin++;
				t++;
			} else {
				parts[t+1] = g_strdup(" ");
				parts[t+1][0] = pat_end[0];
				pat_end[0] = '\0';
				parts[t] = g_strdup(pat_begin);
				pat_begin = pat_end + 1;
				t+=2;
			}
		}
	}

	// Don't overwrite existing values
	// Copy the part if we haven't found a sep char.
	if (!parts[t] && pat_begin < pat_end) {
		parts[t] = g_strdup(pat_begin);
	}

	g_free(pat);

	return parts;
}


// See if the needle matches the haystack, given
// than '?' means zero or one character and '*'
// means zero or any number of characters.
//
// Returns the candidate argument if it matches, or NULL.
// Does not make a copy of candidate. This allows nesting.
char *match_glob(char *pattern, char *candidate)
{
	char *can = candidate;
	char **parts;
	int i, has_star = 0, has_question = 0;

	parts = weird_tokenize(pattern, "?*");
	if (!parts)
		return NULL;

	// Each of the parts much be placed on candidate.
	for (i = 0; parts[i] != NULL; i++) {
		int len, plen;
		
		plen = strlen(parts[i]);

		if (parts[i][0] == '\0') {
			continue;
		}

		if (parts[i][0] == '*') {
			// Signals that we should do an anywhere-ahead match.
			has_star = 1;
			continue;
		}

		if (parts[i][0] == '?') {
			// Signals that we should do a here or next match.
			// Count up the number of spaces we're allowed to skip.
			has_question++;
			continue;
		}

		len = strlen(can);

		if (has_star) {
			int j;

			for (j = 0; j < len; j++) {
				if (strncmp(parts[i], can + j, MIN(plen, len - j)) == 0) {
					can += CLAMP(plen + j, plen, len);
					has_star = 0;
					break;
				}
			}

			// If we get here, then we never matched.
			if (has_star)
				goto nomatch;

			continue;
		}

		// If we have a question mark, we're allowed to match one-ahead.
		if (has_question) {
			int j;

			for (j = 0; j <= has_question; j++) {
				if (strncmp(parts[i], can + j, MIN(plen, len-j)) == 0) {
					can += CLAMP(plen + j, plen, len);
					has_question = 0;
					break;
				}
			}

			// If we get here, then we never matched.
			if (has_question)
				goto nomatch;

			continue;
		}

		// This is for normal matching.
		if (strncmp(parts[i], can, MIN(plen, len)) == 0) {
			can += MIN(plen, len);
			continue;
		}

		// If we get here, then we never matched.
		goto nomatch;
	}

	// Did we advance all the way to the end?
	// Or, did we have a star at the end?
	if (can[0] == '\0' || has_star || (has_question && can[1] == '\0'))
		goto match;

nomatch:
	g_strfreev(parts);
	return NULL;

match:
	g_strfreev(parts);
	return candidate;

}

// Returns a list of elements from the argument list that
// match the pattern. DOES make a copy of matching elements.
// The resulting GList should be freed with
//   g_list_destroy(glob_list);
GList *match_glob_list(char *pattern, GList *list)
{
	GList *match_list = NULL;

	if (!list || !pattern)
		return NULL;

	list = g_list_first(list);
	if (!list)
		return NULL;

	while (list) {
		if (match_glob(pattern, (char *)list->data)) {
			match_list = g_list_prepend(match_list,
					g_strdup((char *)list->data));
		}
		if (! g_list_next(list)) break;
		list = g_list_next(list);
	} 

	// Return elements in the same relative order.
	if (match_list)
		match_list = g_list_reverse(match_list);

	return match_list;
}

