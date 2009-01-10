/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * sprintf.c
 *  - various ways of doing allocating sprintf() functions to void b/o
 *  - wrapper around strdup()
 * --
 * @(#) $Id: sprintf.c,v 1.12 2002/12/29 21:30:12 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include <dircproxy.h>
#include "stringex.h"
#include "sprintf.h"

/* The sprintf() version is just a wrapper around whatever vsprintf() we
   decide to implement. */
#ifdef DEBUG_MEMORY
char *xx_sprintf(char *file, int line, const char *format, ...) {
#else /* DEBUG_MEMORY */
char *x_sprintf(const char *format, ...) {
#endif /* DEBUG_MEMORY */
  va_list ap;
  char *ret;

  va_start(ap, format);
#ifdef DEBUG_MEMORY
  ret = xx_vsprintf(file, line, format, ap);
#else /* DEBUG_MEMORY */
  ret = x_vsprintf(format, ap);
#endif /* DEBUG_MEMORY */
  va_end(ap);

  return ret;  
}

#ifdef HAVE_VASPRINTF

/* Wrap around vasprintf() which exists in BSD.  We can't memdebug() this,
   so its disabled by configure.in if DEBUG_MEMORY is enabled. */
char *x_vsprintf(const char *format, va_list ap) {
  char *str;

  str = 0;
  vasprintf(&str, format, ap);

  return str;
}

#else /* HAVE_VASPRINTF */

# ifdef HAVE_VSNPRINTF

/* Does vsnprintf()s until its not truncated and returns the string.  We
   can't memdebug this, so its disabled by configure.in if DEBUG_MEMORY is
   enabled. */
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

/* These routines are based on those found in the Linux Kernel.  I've
 * rewritten them quite a bit (read completely) since then.
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 */

/* Makes a string representation of a number which can have different
   lengths, bases and precisions etc. */
#ifdef DEBUG_MEMORY
static char *_x_makenum(char *file, int line,
                        long num, int base, int minsize, int mindigits,
                        char padchar, char signchar) {
#else /* DEBUG_MEMORY */
static char *_x_makenum(long num, int base, int minsize, int mindigits,
                        char padchar, char signchar) {
#endif /* DEBUG_MEMORY */
  static char *digits = "0123456789abcdefghijklmnopqrstuvwxyz";
  unsigned long newstrlen;
  char *newstr;
  int i, j;

  if ((base < 2) || (base > 36) || (minsize < 0) || (mindigits < 0)) {
    return NULL;
  }

#ifdef DEBUG_MEMORY
  newstr = xx_strdup(file, line, "");
#else /* DEBUG_MEMORY */
  newstr = x_strdup("");
#endif /* DEBUG_MEMORY */
  newstrlen = 1;

  if (signchar) {
    if (num < 0) {
      signchar = '-';
      num = -num;
    }
  }
  if (signchar == ' ') {
    signchar = 0;
  }

  if (!num) {
#ifdef DEBUG_MEMORY
    newstr = (char *)mem_realloc(newstr, newstrlen + 1, file, line);
#else /* DEBUG_MEMORY */
    newstr = (char *)realloc(newstr, newstrlen + 1);
#endif /* DEBUG_MEMORY */
    strcpy(newstr + newstrlen - 1, "0");
    newstrlen++;
  } else {
    while (num) {
#ifdef DEBUG_MEMORY
      newstr = (char *)mem_realloc(newstr, newstrlen + 1, file, line);
#else /* DEBUG_MEMORY */
      newstr = (char *)realloc(newstr, newstrlen + 1);
#endif /* DEBUG_MEMORY */
      newstr[newstrlen - 1] = digits[((unsigned long) num) % base];
      newstr[newstrlen] = 0;
      newstrlen++;
      num = ((unsigned long) num) / base;
    }
  }

  if (strlen(newstr) < mindigits) {
    mindigits -= strlen(newstr);
    while (mindigits--) {
#ifdef DEBUG_MEMORY
      newstr = (char *)mem_realloc(newstr, newstrlen + 1, file, line);
#else /* DEBUG_MEMORY */
      newstr = (char *)realloc(newstr, newstrlen + 1);
#endif /* DEBUG_MEMORY */
      strcpy(newstr + newstrlen - 1, "0");
      newstrlen++;
    }
  }

  if (signchar && (padchar != '0')) {
#ifdef DEBUG_MEMORY
    newstr = (char *)mem_realloc(newstr, newstrlen + 1, file, line);
#else /* DEBUG_MEMORY */
    newstr = (char *)realloc(newstr, newstrlen + 1);
#endif /* DEBUG_MEMORY */
    newstr[newstrlen - 1] = signchar;
    newstr[newstrlen] = 0;
    newstrlen++;
    signchar = 0;
  }

  if ((strlen(newstr) < minsize) && padchar) {
    minsize -= strlen(newstr);
    while (minsize--) {
#ifdef DEBUG_MEMORY
      newstr = (char *)mem_realloc(newstr, newstrlen + 1, file, line);
#else /* DEBUG_MEMORY */
      newstr = (char *)realloc(newstr, newstrlen + 1);
#endif /* DEBUG_MEMORY */
      newstr[newstrlen - 1] = padchar;
      newstr[newstrlen] = 0;
      newstrlen++;
    }
    if (signchar) {
      newstr[strlen(newstr)-1] = signchar;
      signchar = 0;
    }
  }

  if (signchar) {
#ifdef DEBUG_MEMORY
    newstr = (char *)mem_realloc(newstr, newstrlen + 1, file, line);
#else /* DEBUG_MEMORY */
    newstr = (char *)realloc(newstr, newstrlen + 1);
#endif /* DEBUG_MEMORY */
    newstr[newstrlen - 1] = signchar;
    newstr[newstrlen] = 0;
    newstrlen++;
  }

  i = strlen(newstr)-1;
  j = 0;
  while (i > j) {
    char tmpchar;

    tmpchar = newstr[i];
    newstr[i] = newstr[j];
    newstr[j] = tmpchar;
    i--;
    j++;
  }

  if ((strlen(newstr) < minsize) && !padchar) {
    char *tmpstr;

    i = minsize - strlen(newstr);
#ifdef DEBUG_MEMORY
    tmpstr = (char *)mem_malloc(i + 1, file, line);
#else /* DEBUG_MEMORY */
    tmpstr = (char *)malloc(i + 1);
#endif /* DEBUG_MEMORY */
    tmpstr[i--] = 0;
    while (i >= 0) {
      tmpstr[i--] = ' ';
    }

#ifdef DEBUG_MEMORY
    tmpstr = (char *)mem_realloc(tmpstr, strlen(tmpstr) + strlen(newstr) + 1,
                                 file, line);
#else /* DEBUG_MEMORY */
    tmpstr = (char *)realloc(tmpstr, strlen(tmpstr) + strlen(newstr) + 1);
#endif /* DEBUG_MEMORY */
    strcpy(tmpstr + strlen(tmpstr), newstr);
#ifdef DEBUG_MEMORY
    mem_realloc(newstr, 0, file, line);
#else /* DEBUG_MEMORY */
    free(newstr);
#endif /* DEBUG_MEMORY */
    newstr = tmpstr;
  }

  return newstr;
}

/* Basically vasprintf, except it doesn't do floating point or pointers */
#ifdef DEBUG_MEMORY
char *xx_vsprintf(char *file, int line, const char *format, va_list ap) {
#else /* DEBUG_MEMORY */
char *x_vsprintf(const char *format, va_list ap) {
#endif /* DEBUG_MEMORY */
  char *newdest, *formatcpy, *formatpos, padding, signchar, qualifier;
  int width, prec, base, special, len, caps;
  unsigned long newdestlen;
  long num;

#ifdef DEBUG_MEMORY
  newdest = xx_strdup(file, line, "");
#else /* DEBUG_MEMORY */
  newdest = x_strdup("");
#endif /* DEBUG_MEMORY */
  newdestlen = 1;
#ifdef DEBUG_MEMORY
  formatpos = formatcpy = xx_strdup(file, line, format);
#else /* DEBUG_MEMORY */
  formatpos = formatcpy = x_strdup(format);
#endif /* DEBUG_MEMORY */

  while (*formatpos) {
    if (*formatpos == '%') {
      padding = signchar = ' ';
      qualifier = 0;
      width = prec = special = caps = 0;
      base = 10;
      formatpos++;

      while (1) {
        if (*formatpos == '-') {
          padding = 0;
        } else if (*formatpos == '+') {
          signchar = '+';
        } else if (*formatpos == ' ') {
          signchar = (signchar == '+') ? '+' : ' ';
        } else if (*formatpos == '#') {
          special = 1;
        } else {
          break;
        }
        formatpos++;
      }

      if (*formatpos == '*') {
        width = va_arg(ap, int);
        if (width < 0) {
          width =- width;
          padding = 0;
        }
        formatpos++;
      } else {
        if (*formatpos == '0') {
          padding = '0';
          formatpos++;
        }

        while (isdigit(*formatpos)) {
          width *= 10;
          width += (*formatpos - '0');
          formatpos++;
        }
      }

      if (*formatpos == '.') {
        formatpos++;
        if (*formatpos == '*') {
          prec = abs(va_arg(ap, int));
          formatpos++;
        } else {
          while (isdigit(*formatpos)) {
            prec *= 10;
            prec += (*formatpos - '0');
            formatpos++;
          }
        }
      }

      if ((*formatpos == 'h') || (*formatpos == 'l')) {
        qualifier = *formatpos;
        formatpos++;
      }

      if (*formatpos == 'c') {
        if (padding) {
          while (--width > 0) {
#ifdef DEBUG_MEMORY
            newdest = (char *)mem_realloc(newdest, newdestlen + 1, file, line);
#else /* DEBUG_MEMORY */
            newdest = (char *)realloc(newdest, newdestlen + 1);
#endif /* DEBUG_MEMORY */
            newdest[newdestlen - 1] = padding;
            newdest[newdestlen] = 0;
            newdestlen++;
          }
        }

#ifdef DEBUG_MEMORY
        newdest = (char *)mem_realloc(newdest, newdestlen + 1, file, line);
#else /* DEBUG_MEMORY */
        newdest = (char *)realloc(newdest, newdestlen + 1);
#endif /* DEBUG_MEMORY */
        newdest[newdestlen - 1] = va_arg(ap, unsigned char);
        newdest[newdestlen] = 0;
        newdestlen++;

        if (!padding) {
          while (--width > 0) {
#ifdef DEBUG_MEMORY
            newdest = (char *)mem_realloc(newdest, newdestlen + 1, file, line);
#else /* DEBUG_MEMORY */
            newdest = (char *)realloc(newdest, newdestlen + 1);
#endif /* DEBUG_MEMORY */
            newdest[newdestlen - 1] = padding;
            newdest[newdestlen] = 0;
            newdestlen++;
          }
        }
      } else if (*formatpos == 's') {
        char *tmpstr;

#ifdef DEBUG_MEMORY
        tmpstr = xx_strdup(file, line, va_arg(ap, char *));
#else /* DEBUG_MEMORY */
        tmpstr = x_strdup(va_arg(ap, char *));
#endif /* DEBUG_MEMORY */
        len = (prec && (prec < strlen(tmpstr))) ? prec : strlen(tmpstr);
        if (padding) {
          while (--width > len) {
#ifdef DEBUG_MEMORY
            newdest = (char *)mem_realloc(newdest, newdestlen + 1, file, line);
#else /* DEBUG_MEMORY */
            newdest = (char *)realloc(newdest, newdestlen + 1);
#endif /* DEBUG_MEMORY */
            newdest[newdestlen - 1] = padding;
            newdest[newdestlen] = 0;
            newdestlen++;
          }
        }

#ifdef DEBUG_MEMORY
        newdest = (char *)mem_realloc(newdest, newdestlen + len, file, line);
#else /* DEBUG_MEMORY */
        newdest = (char *)realloc(newdest, newdestlen + len);
#endif /* DEBUG_MEMORY */
        strncpy(newdest + newdestlen - 1, tmpstr, len);
        newdestlen += len;
        newdest[newdestlen - 1] = 0;
#ifdef DEBUG_MEMORY
        mem_realloc(tmpstr, 0, file, line);
#else /* DEBUG_MEMORY */
        free(tmpstr);
#endif /* DEBUG_MEMORY */

        if (!padding) {
          while (--width > len) {
#ifdef DEBUG_MEMORY
            newdest = (char *)mem_realloc(newdest, newdestlen + 1, file, line);
#else /* DEBUG_MEMORY */
            newdest = (char *)realloc(newdest, newdestlen + 1);
#endif /* DEBUG_MEMORY */
            newdest[newdestlen - 1] = padding;
            newdest[newdestlen] = 0;
            newdestlen++;
          }
        }
      } else if (strchr("oXxdiu", *formatpos) != NULL) {
        char *tmpstr;

        if (*formatpos == 'o') {
          base = 8;
          if (signchar) {
            signchar = 0;
          }
          if (special) {
#ifdef DEBUG_MEMORY
            newdest = (char *)mem_realloc(newdest, newdestlen + 1, file, line);
#else /* DEBUG_MEMORY */
            newdest = (char *)realloc(newdest, newdestlen + 1);
#endif /* DEBUG_MEMORY */
            strcpy(newdest + newdestlen - 1, "0");
            newdestlen++;
          }
        } else if (*formatpos == 'X') {
          base = 16;
          caps = 1;
          if (signchar) {
            signchar = 0;
          }
          if (special) {
#ifdef DEBUG_MEMORY
            newdest = (char *)mem_realloc(newdest, newdestlen + 1, file, line);
#else /* DEBUG_MEMORY */
            newdest = (char *)realloc(newdest, newdestlen + 1);
#endif /* DEBUG_MEMORY */
            strcpy(newdest + newdestlen - 1, "0");
            newdestlen++;
          }
        } else if (*formatpos == 'x') {
          base = 16;
          if (signchar) {
            signchar = 0;
          }
          if (special) {
#ifdef DEBUG_MEMORY
            newdest = (char *)mem_realloc(newdest, newdestlen + 2, file, line);
#else /* DEBUG_MEMORY */
            newdest = (char *)realloc(newdest, newdestlen + 2);
#endif /* DEBUG_MEMORY */
            strcpy(newdest + newdestlen - 1, "0x");
            newdestlen += 2;
          }
        } else if (*formatpos == 'i') {
          if (!signchar) {
            signchar = ' ';
          }
        } else if (*formatpos == 'u') {
          if (signchar) {
            signchar = 0;
          }
        }
        
        if (qualifier == 'l') {
          num = va_arg(ap, unsigned long);
        } else if (qualifier == 'h') {
          if (signchar) {
            num = va_arg(ap, signed short int);
          } else {
            num = va_arg(ap, unsigned short int);
          }
        } else if (signchar) {
          num = va_arg(ap, signed int);
        } else {
          num = va_arg(ap, unsigned int);
        }

#ifdef DEBUG_MEMORY
        tmpstr = _x_makenum(file, line,
                            num, base, width, prec, padding, signchar);
#else /* DEBUG_MEMORY */
        tmpstr = _x_makenum(num, base, width, prec, padding, signchar);
#endif /* DEBUG_MEMORY */
        if (caps)
          strupr(tmpstr);

#ifdef DEBUG_MEMORY
        newdest = (char *)mem_realloc(newdest, newdestlen + strlen(tmpstr),
                                      file, line);
#else /* DEBUG_MEMORY */
        newdest = (char *)realloc(newdest, newdestlen + strlen(tmpstr));
#endif /* DEBUG_MEMORY */
        strcpy(newdest + newdestlen - 1, tmpstr);
        newdestlen += strlen(tmpstr);
#ifdef DEBUG_MEMORY
        mem_realloc(tmpstr, 0, file, line);
#else /* DEBUG_MEMORY */
        free(tmpstr);
#endif /* DEBUG_MEMORY */
      } else if (*formatpos == '%') {
#ifdef DEBUG_MEMORY
        newdest = (char *)mem_realloc(newdest, newdestlen + 1, file, line);
#else /* DEBUG_MEMORY */
        newdest = (char *)realloc(newdest, newdestlen + 1);
#endif /* DEBUG_MEMORY */
        newdest[newdestlen - 1] = *formatpos;
        newdest[newdestlen] = 0;
        newdestlen++;
      }
    } else {
#ifdef DEBUG_MEMORY
      newdest = (char *)mem_realloc(newdest, newdestlen + 1, file, line);
#else /* DEBUG_MEMORY */
      newdest = (char *)realloc(newdest, newdestlen + 1);
#endif /* DEBUG_MEMORY */
      newdest[newdestlen - 1] = *formatpos;
      newdest[newdestlen] = 0;
      newdestlen++;
    }

    formatpos++;
  }

#ifdef DEBUG_MEMORY
  mem_realloc(formatcpy, 0, file, line);
#else /* DEBUG_MEMORY */
  free(formatcpy);
#endif /* DEBUG_MEMORY */
  return newdest;
}

# endif /* HAVE_VSNPRINTF */
#endif /* HAVE_VASPRINTF */

#ifdef HAVE_STRDUP

/* Wrap around strdup().  We can't memdebug() this, so its disabled by
   configure.in if DEBUG_MEMORY is enabled. */
char *x_strdup(const char *s) {
  return strdup(s);
}

#else /* HAVE_STRDUP */

/* Do the malloc and strcpy() ourselves so we don't annoy memdebug.c */
#ifdef DEBUG_MEMORY
char *xx_strdup(char *file, int line, const char *s) {
#else /* DEBUG_MEMORY */
char *x_strdup(const char *s) {
#endif /* DEBUG_MEMORY */
  char *ret;

#ifdef DEBUG_MEMORY
  ret = (char *)mem_malloc(strlen(s) + 1, file, line);
#else /* DEBUG_MEMORY */
  ret = (char *)malloc(strlen(s) + 1);
#endif /* DEBUG_MEMORY */
  strcpy(ret, s);

  return ret;
}

#endif /* HAVE_STRDUP */
