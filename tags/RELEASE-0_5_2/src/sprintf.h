/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * sprintf.h
 * --
 * @(#) $Id: sprintf.h,v 1.1 2000/05/13 02:13:56 keybuk Exp $
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
extern char *x_sprintf(const char *, ...);
extern char *x_vsprintf(const char *, va_list);

#endif /* __DIRCPROXY_SPRINTF_H */
