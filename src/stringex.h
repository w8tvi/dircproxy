/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
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
