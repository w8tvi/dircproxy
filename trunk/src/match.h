/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * match.h
 * --
 * @(#) $Id: match.h,v 1.1 2000/05/13 02:13:56 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_MATCH_H
#define __DIRCPROXY_MATCH_H

/* functions */
extern int strmatch(const char *, const char *);
extern int strcasematch(const char *, const char *);

#endif /* __DIRCPROXY_MATCH_H */
