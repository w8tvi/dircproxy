/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * sprintf.c
 *  - various ways of doing allocating sprintf() functions to void b/o
 * --
 * @(#) $Id: sprintf.c,v 1.2 2000/05/13 05:25:04 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <dircproxy.h>
#include "sprintf.h"

/* With memory debugging, we want to see our own mallocs take place */
#ifdef MEM_DEBUG
#undef HAVE_VASPRINTF
#undef HAVE_STRDUP
#endif

/* The sprintf() version is just a wrapper around whatever vsprintf() we
   decide to implement. */
char *x_sprintf(const char *format, ...) {
  va_list ap;
  char *ret;

  va_start(ap, format);
  ret = x_vsprintf(format, ap);
  va_end(ap);

  return ret;  
}

#ifdef HAVE_VASPRINTF

/* Wrap around vasprintf() which exists in BSD */
char *x_vsprintf(const char *format, va_list ap) {
  char *str;

  str = 0;
  vasprintf(&str, format, ap);

  return str;
}

#else /* HAVE_VASPRINTF */

# ifdef HAVE_VSNPRINTF

/* Does vsnprintf()s until its not truncated and returns the string */
char *x_vsprintf(const char *format, va_list ap) {
  int buffsz, ret;
  char *buff;

  buff = 0;
  buffsz = 0;

  do {
    buffsz += 64;
    buff = (char *)realloc(buff, buffsz + 1);

    ret = vsnprintf(buff, buffsz, format, ap);
  } while ((ret == -1) || (ret >= buffsz));

  return buff;
}

# else /* HAVE_VSNPRINTF */
#   error "Your system must have vasprintf() or vsnprintf()"
# endif /* JAVE_VSNPRINTF */
#endif /* HAVE_VASPRINTF */

#ifdef HAVE_STRDUP

/* Wrap around strdup() */
char *x_strdup(const char *s) {
  return strdup(s);
}

#else /* HAVE_STRDUP */

/* Do the malloc and strcpy() ourselves so we don't annoy memdebug.c */
char *x_strdup(const char *s) {
  char *ret;

  ret = (char *)malloc(strlen(s) + 1);
  strcpy(ret, s);

  return ret;
}

#endif /* HAVE_STRDUP */
