/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * stringex.h
 * --
 * @(#) $Id: stringex.h,v 1.1 2000/05/13 02:13:57 keybuk Exp $
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
