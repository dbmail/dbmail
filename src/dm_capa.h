/*
  
 Copyright (c) 2009-2012 NFG Net Facilities Group BV support@nfg.nl

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

#ifndef DM_CAPA_H
#define DM_CAPA_H

#include <glib.h>

#define T Capa_T

typedef struct T *T;

extern T               Capa_new(void);
extern const gchar *   Capa_as_string(T);
extern gboolean        Capa_match(T, const char *); 
extern void            Capa_add(T, const char *);
extern void            Capa_remove(T, const char *);
extern void            Capa_free(T *);

#undef T

#endif
