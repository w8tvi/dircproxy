/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * sprintf.h
 * --
 * @(#) $Id: sprintf.h,v 1.3 2000/10/13 12:53:10 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_SPRINTF_H
#define __DIRCPROXY_SPRINTF_H

/* required includes */
#include <stdarg.h>

/* functions */
#ifdef DEBUG_MEMORY
/* Sorry about this, its pretty disgusting.  Because x_sprintf() is a ...
   function we can't actually just make a #define wrapper for it tagging
   a couple of extra parameters in it.  Instead we have to call a function
   called xx_set to set the file and line number parameters, then have this
   return a pointer to the real x_sprintf() function - cast it - then use
   the following brackets to actually call it. */
#define x_sprintf ((char *(*)(const char *, ...))xx_set(__FILE__, __LINE__))
extern void *xx_set(char *, int);

/* These are just wrappers around their usual function */
#define x_vsprintf(FMT, LIST) xx_vsprintf(__FILE__, __LINE__, FMT, LIST)
#define x_strdup(STR) xx_strdup(__FILE__, __LINE__, STR)
extern char *xx_vsprintf(char *, int, const char *, va_list);
extern char *xx_strdup(char *, int, const char *);
#else /* DEBUG_MEMORY */
/* Not debugging memory, run normally */
extern char *x_sprintf(const char *, ...);
extern char *x_vsprintf(const char *, va_list);
extern char *x_strdup(const char *);
#endif /* DEBUG_MEMORY */

#endif /* __DIRCPROXY_SPRINTF_H */
