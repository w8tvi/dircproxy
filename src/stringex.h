/* dircproxy
 * Copyright (C) 2000,2001,2002,2003 Scott James Remnant <scott@netsplit.com>.
 *
 * stringex.h
 * --
 * @(#) $Id: stringex.h,v 1.4 2002/12/29 21:30:12 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_STRINGEX_H
#define __DIRCPROXY_STRINGEX_H

/* structure to hold a list of strings */
struct strlist {
  char *str;

  struct strlist *next;
};

/* functions */
extern char *strlwr(char *);
extern char *strupr(char *);

#endif /* __DIRCPROXY_STRINGEX_H */
