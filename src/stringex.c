/* dircproxy
 * Copyright (C) 2002 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * stringex.c
 *  - Case conversion functions
 * --
 * @(#) $Id: stringex.c,v 1.3 2001/12/21 20:15:55 keybuk Exp $
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

