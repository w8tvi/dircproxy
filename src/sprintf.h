/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * sprintf.h
 * --
 * @(#) $Id: sprintf.h,v 1.4 2000/10/13 17:02:26 keybuk Exp $
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
#define x_sprintf(ARGS...) xx_sprintf(__FILE__, __LINE__, ARGS)
#define x_vsprintf(FMT, LIST) xx_vsprintf(__FILE__, __LINE__, FMT, LIST)
#define x_strdup(STR) xx_strdup(__FILE__, __LINE__, STR)
extern char *xx_sprintf(char *, int, const char *, ...);
extern char *xx_vsprintf(char *, int, const char *, va_list);
extern char *xx_strdup(char *, int, const char *);
#else /* DEBUG_MEMORY */
/* Not debugging memory, run normally */
extern char *x_sprintf(const char *, ...);
extern char *x_vsprintf(const char *, va_list);
extern char *x_strdup(const char *);
#endif /* DEBUG_MEMORY */

#endif /* __DIRCPROXY_SPRINTF_H */
