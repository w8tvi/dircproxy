/* dircproxy
 * Copyright (C) 2000,2001,2002,2003 Scott James Remnant <scott@netsplit.com>.
 *
 * match.h
 * --
 * @(#) $Id: match.h,v 1.4 2002/12/29 21:30:12 scott Exp $
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
