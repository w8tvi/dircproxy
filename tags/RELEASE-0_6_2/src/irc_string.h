/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_string.h
 * --
 * @(#) $Id: irc_string.h,v 1.1 2000/05/13 02:14:01 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_IRC_STRING_H
#define __DIRCPROXY_IRC_STRING_H

/* required includes */
#include "match.h"

/* Non case sensitive versions are defined for completeness */
#define irc_strcmp(_s1, _s2) strcmp((_s1), (_s2))
#define irc_strmatch(_s, _m) strmatch((_s), (_m))

/* functions */
extern char *irc_strlwr(char *);
extern char *irc_strupr(char *);
extern int irc_strcasecmp(const char *, const char *);
extern int irc_strcasematch(const char *, const char *);

#endif /* __DIRCPROXY_STRINGEX_H */
