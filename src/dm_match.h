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
#ifndef DM_MATCH_H
#define DM_MATCH_H

// See if the needle matches the haystack, given
// than '?' means zero or one character and '*'
// means zero or any number of characters.
//
// Returns the candidate argument if it matches, or NULL.
// Does not make a copy of candidate. This allows nesting.
char *match_glob(char *pattern, char *candidate);

// Returns a list of elements from the argument list that
// match the pattern. DOES make a copy of matching elements.
// The resulting GList should be freed with
//   g_list_foreach(glob_list, (GFunc)g_free, NULL)
//   g_list_free(glob_list);
GList *match_glob_list(char *pattern, GList *list);

#endif
