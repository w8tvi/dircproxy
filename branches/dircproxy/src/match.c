/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * match.c
 *  - wildcard matching
 *
 * I actually figured this out and wrote it myself. Unforunately
 * its a lot smaller than anyone elses that I know of, which worries
 * me slightly :) - But it seems to work
 * --
 * @(#) $Id: match.c,v 1.1 2000/05/13 02:13:56 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdlib.h>
#include <string.h>

#include <dircproxy.h>
#include "stringex.h"
#include "match.h"

/* Checks whether a string matches a wildcard string.  1 = yes, 0 = no */
int strmatch(const char *str, const char *mask) {
  int instar;

  instar = 0;
  while (*str) {
    if (*mask == '*') {
      instar = 1;
      mask++;
    }
    if ((*mask == *str) || (*mask == '?')) {
      if (instar && strmatch(str, mask))
        return 1;
    } else {
      if (!instar)
        return 0;
    }
    str++;
    if (!instar) {
      mask++;
      if (!(*mask) && *str)
        return 0;
    }
  }

  return !(*mask && (*mask != '*'));
}

/* Case insentively matches against wildcards */
int strcasematch(const char *str, const char *mask) {
  char *newstr, *newmask;
  int ret;

  newstr = (char *)malloc(strlen(str) + 1);
  strcpy(newstr, str);
  strlwr(newstr);

  newmask = (char *)malloc(strlen(mask) + 1);
  strcpy(newmask, mask);
  strlwr(newmask);

  ret = strmatch(newstr, newmask);

  free(newstr);
  free(newmask);

  return ret;
}

