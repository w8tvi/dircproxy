/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_string.c
 *  - Case conversion functions for IRC protocol
 * --
 * @(#) $Id: irc_string.c,v 1.3 2000/05/13 05:25:04 keybuk Exp $
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

/* Changes the case of a string to lowercase */
char *irc_strlwr(char *str) {
  char *c;

  c = str;
  while (*c) {
    switch (*c) {
      case '[':
        *c = '{';
        break;
      case ']':
        *c = '}';
        break;
      case '\\':
        *c = '|';
        break;
      default:
        *c = tolower(*c);
        break;
    }
    c++;
  }

  return str;
}

/* Changes the case of a string to uppercase */
char *irc_strupr(char *str) {
  char *c;

  c = str;
  while (*c) {
    switch (*c) {
      case '{':
        *c = '[';
        break;
      case '}':
        *c = ']';
        break;
      case '|':
        *c = '\\';
        break;
      default:
        *c = tolower(*c);
        break;
    }
    c++;
  }

  return str;
}

/* Compare two irc strings, ignoring case */
int irc_strcasecmp(const char *s1, const char *s2) {
  char *news1, *news2;
  int ret;

  news1 = irc_strlwr(x_strdup(s1));
  news2 = irc_strlwr(x_strdup(s2));

  ret = strcmp(news1, news2);

  free(news1);
  free(news2);

  return ret;
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
