 /*

 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2012 NFG Net Facilities Group BV support@nfg.nl

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

/*
 * dm_stream.h
 *
 */

#ifndef DM_STREAM_H
#define DM_STREAM_H

#define T Stream_T
typedef struct T *T;

extern T    Stream_new(void);
extern T    Stream_open(void);
extern void Stream_ref    (T M, T N); 
extern void Stream_close  (T *M);
extern int  Stream_write  (T M, const void *data, int size);
extern int  Stream_read   (T M, void *data, int size);
extern int  Stream_seek   (T M, long offset, int whence);

#define Stream_rewind(M) Stream_seek(M, 0, SEEK_SET)

#undef T

#endif
