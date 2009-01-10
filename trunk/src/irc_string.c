/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * irc_string.c
 *  - Case conversion functions for IRC protocol
 *  - Comparison and match functions for IRC protocol
 * --
 * @(#) $Id: irc_string.c,v 1.9 2002/12/29 21:30:12 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <dircproxy.h>
#include "match.h"
#include "sprintf.h"
#include "irc_string.h"

/* forward declarations */
static int _irc_tolower(int);
static int _irc_toupper(int);

/* IRC version of tolower */
static int _irc_tolower(int c) {
  switch (c) {
    case '\\':
      return '|';
    default:
      return tolower(c);
  }
}

/* IRC version of tolower */
static int _irc_toupper(int c) {
  switch (c) {
    case '|':
      return '\\';
    default:
      return toupper(c);
  }
}

/* Changes the case of a string to lowercase */
char *irc_strlwr(char *str) {
  char *c;

  c = str;
  while (*c) {
    *c = _irc_tolower(*c);
    c++;
  }

  return str;
}

/* Changes the case of a string to uppercase */
char *irc_strupr(char *str) {
  char *c;

  c = str;
  while (*c) {
    *c = _irc_toupper(*c);
    c++;
  }

  return str;
}

/* Compare two irc strings, ignoring case.  This is done so much, I've dropped
   a simple version of the strcmp algorithm here rather than doing two mallocs
   lowercasing etc. */
int irc_strcasecmp(const char *s1, const char *s2) {
  while (_irc_tolower(*s1) == _irc_tolower(*s2)) {
    if (!*s1)
      return 0;

    s1++;
    s2++;
  }

  return _irc_tolower(*s1) - _irc_tolower(*s2);
}

/* Match an irc string against wildcards, ignoring case */
int irc_strcasematch(const char *str, const char *mask) {
  char *newstr, *newmask;
  int ret;

  newstr = irc_strlwr(x_strdup(str));
  newmask = irc_strlwr(x_strdup(mask));

  ret = strmatch(newstr, newmask);

  free(newstr);
  free(newmask);

  return ret;
}
