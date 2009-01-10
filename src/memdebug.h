/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * memdebug.h
 * --
 * @(#) $Id: memdebug.h,v 1.5 2002/12/29 21:30:12 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_MEMDEBUG_H
#define __DIRCPROXY_MEMDEBUG_H

/* required includes */
#include <stdlib.h>

/* replace standard functions with replacements wrapped around debug */
#ifdef DEBUG_MEMORY
#undef malloc
#undef calloc
#undef free
#undef realloc
#define malloc(SIZE) mem_malloc(SIZE, __FILE__, __LINE__)
#define calloc(NMEMB, SIZE) mem_malloc(NMEMB * SIZE, __FILE__, __LINE__)
#define free(PTR) mem_realloc(PTR, 0, __FILE__, __LINE__)
#define realloc(PTR, SIZE) mem_realloc(PTR, SIZE, __FILE__, __LINE__)
#endif /* DEBUG_MEMORY */

/* functions */
extern void *mem_malloc(size_t, char *, int);
extern void *mem_realloc(void *, size_t, char *, int);
extern void mem_report(const char *);

#endif /* __DIRCPROXY_MEMDEBUG_H */
