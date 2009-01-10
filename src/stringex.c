/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * stringex.c
 *  - Case conversion functions
 * --
 * @(#) $Id: stringex.c,v 1.4 2002/12/29 21:30:12 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <ctype.h>

#include <dircproxy.h>
#include "stringex.h"

/* Changes the case of a string to lowercase */
char *strlwr(char *str) {
  char *c;

  c = str;
  while (*c) {
    *c = tolower(*c);
    c++;
  }

  return str;
}

/* Changes the case of a string to uppercase */
char *strupr(char *str) {
  char *c;

  c = str;
  while (*c) {
    *c = toupper(*c);
    c++;
  }

  return str;
}

