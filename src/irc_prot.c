/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * irc_prot.c
 *  - IRC protocol message parsing
 *  - IRC x!y@z parsing
 *  - CTCP stripping and dequoting
 *  - CTCP message parsing
 *  - Username sanitisation
 * --
 * @(#) $Id: irc_prot.c,v 1.15 2002/12/29 21:30:12 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdlib.h>
#include <string.h>

#include <dircproxy.h>
#include "sprintf.h"
#include "stringex.h"
#include "irc_prot.h"
#include "irc_string.h"

/* forward declarations */
static int _ircprot_parse_prefix(char *, struct ircsource *);
static int _ircprot_count_params(char *);
static int _ircprot_get_params(char *, char ***, char ***);
static char *_ircprot_skip_spaces(char *);
static char *_ircprot_ctcpdequote(const char *);

/* Parse an IRC message. num of params or -1 if no command */
int ircprot_parsemsg(const char *message, struct ircmessage *msg) {
  char *start, *ptr;

  /* Copy the original message as well */
  ptr = start = msg->orig = x_strdup(message);

  /* Begins with a prefix? */
  if (*ptr == ':') {
    while (*ptr && (*ptr != ' ')) ptr++;

    msg->src.orig = (char *)malloc(ptr - start);
    strncpy(msg->src.orig, start + 1, ptr - start - 1);
    msg->src.orig[ptr - start - 1] = 0;

    _ircprot_parse_prefix(msg->src.orig, &(msg->src));

    ptr = _ircprot_skip_spaces(ptr);
  } else {
    /* It just came from our peer */
    msg->src.name = msg->src.username = msg->src.hostname = msg->src.orig = 0;
    msg->src.fullname = 0;
    msg->src.type = IRC_PEER;
  }

  /* No command? */
  if (!*ptr) {
    free(msg->src.name);
    free(msg->src.username);
    free(msg->src.hostname);
    free(msg->src.fullname);
    free(msg->src.orig);
    free(msg->orig);
    return -1;
  }

  /* Take the command off the front */
  start = ptr;
  while (*ptr && (*ptr != ' ')) ptr++;

  msg->cmd = (char *)malloc(ptr - start + 1);
  strncpy(msg->cmd, start, ptr - start);
  msg->cmd[ptr - start] = 0;

  ptr = _ircprot_skip_spaces(ptr);

  /* Now do the parameters */
  msg->numparams = _ircprot_count_params(ptr);
  if (msg->numparams) {
    msg->params = (char **)malloc(sizeof(char *) * msg->numparams);
    msg->paramstarts = (char **)malloc(sizeof(char *) * msg->numparams);
    _ircprot_get_params(ptr, &(msg->params), &(msg->paramstarts));
  } else {
    msg->params = 0;
    msg->paramstarts = 0;
  }

  return msg->numparams;
}

/* Free an IRC message */
void ircprot_freemsg(struct ircmessage *msg) {
  int i;

  for (i = 0; i < msg->numparams; i++)
    free(msg->params[i]);

  free(msg->src.name);
  free(msg->src.username);
  free(msg->src.hostname);
  free(msg->src.fullname);
  free(msg->src.orig);
  free(msg->cmd);
  free(msg->params);
  free(msg->paramstarts);
  free(msg->orig);
}

/* Parse a prefix from an irc message */
static int _ircprot_parse_prefix(char *prefix, struct ircsource *source) {
  char *str, *ptr;

  str = prefix;
  ptr = strchr(str, '!');
  if (ptr) {
    source->type = IRC_USER;
    source->username = source->hostname = 0;

    source->name = (char *)malloc(ptr - str + 1);
    strncpy(source->name, str, ptr - str);
    source->name[ptr - str] = 0;
    str = ptr + 1;

    ptr = strchr(str, '@');
    if (ptr) {
      source->username = (char *)malloc(ptr - str + 1);
      strncpy(source->username, str, ptr - str);
      source->username[ptr - str] = 0;
      str = ptr + 1;

      source->hostname = x_strdup(str);
    } else {
      source->type = IRC_EITHER;
    }
  } else {
    source->type = IRC_EITHER;
    source->username = source->hostname = 0;
    source->name = x_strdup(str);
  }

  if (source->name && source->username && source->hostname) {
    source->fullname = x_sprintf("%s (%s@%s)", source->name,
                                 source->username, source->hostname);
  } else {
    source->fullname = x_strdup(source->name);
  }

  return source->type;
}

/* Count the number of parameters in an irc message */
static int _ircprot_count_params(char *message) {
  char *ptr;
  int count;

  count = 0;
  ptr = message;

  while (*ptr) {
    count++;

    if (*ptr == ':') break;
    while (*ptr && (*ptr != ' ')) ptr++;
    ptr = _ircprot_skip_spaces(ptr);
  }

  return count;
}

/* Split an irc message into an array */
static int _ircprot_get_params(char *message, char ***params,
                               char ***paramstarts) {
  char *ptr, *start;
  int param;

  param = 0;
  ptr = start = message;

  while (*ptr) {
    if (*ptr == ':') {
      (*params)[param] = x_strdup(ptr + 1);
      (*paramstarts)[param] = ptr + 1;
      break;
    } else {
      (*paramstarts)[param] = start;

      while (*ptr && (*ptr != ' ')) ptr++;

      (*params)[param] = (char *)malloc(ptr - start + 1);
      strncpy((*params)[param], start, ptr - start);
      (*params)[param][ptr - start] = 0;

      ptr = _ircprot_skip_spaces(ptr);

      param++;
      start = ptr;
    }
  }

  return param + 1;
}

/* Skip spaces */
static char *_ircprot_skip_spaces(char *ptr) {
#ifdef OLD_RFC1459_PARAM_SPACE
    while (*ptr && (*ptr == ' ')) ptr++;
#else /* OLD_RFC1459_PARAM_SPACE */
    if (*ptr && (*ptr == ' ')) ptr++;
#endif /* OLD_RFC1459_PARAM_SPACE */

    return ptr;
}

/* Returns a new string that's the dequoted version of the old one */
static char *_ircprot_ctcpdequote(const char *msg) {
  char *new, *out, *in, *ret;
  int quote = 0;

  in = out = new = x_strdup(msg);
  while (*in) {
    if (quote) {
      if (*in == 'a') {
        *(out++) = '\\';
      } else {
        *(out++) = *in;
      }

      quote = 0;
    } else {
      if (*in == '\\') {
        quote = 1;
      } else {
        *(out++) = *in;
      }
    }

    in++;
  }
  *out = 0;

  ret = x_strdup(new);
  free(new);
  return ret;
}

/* Strip embedded CTCP messages from a string, placing the new string in the
 * newmsg pointer and a strlist of the ctcp's that were found in the list
 * pointer */
void ircprot_stripctcp(const char *msg, char **newmsg, struct strlist **list) {
  char *copy, *in, *out, *start = 0;
  int ctcp = 0;

  if (list)
    *list = 0;

  in = out = copy = x_strdup(msg);
  while (*in) {
    if (*in == 0x01) {
      if (ctcp) {
        *(in++) = 0;
        out -= strlen(start);
        ctcp = 0;

        if (strlen(start + 1) && list) {
          struct strlist *s;

          s = (struct strlist *)malloc(sizeof(struct strlist));
          s->str = x_strdup(start + 1);
          s->next = 0;
                                          
          if (*list) {
            struct strlist *ss;

            ss = *list;
            while (ss->next)
              ss = ss->next;

            ss->next = s;
          } else {
            *list = s;
          }
        }
      } else {
        /* Found start of a CTCP */
        start = in;
        ctcp = 1;
        *(out++) = *(in++);
      }
    } else {
      *(out++) = *(in++);
    }
  }
  *out = 0;

  if (newmsg)
    *newmsg = x_strdup(copy);
  free(copy);
}

/* Parse an CTCP message. num of params or -1 if no command */
int ircprot_parsectcp(const char *message, struct ctcpmessage *cmsg) {
  char *start, *ptr;

  /* Copy the original message as well */
  ptr = start = cmsg->orig = _ircprot_ctcpdequote(message);

  /* No command? */
  if (!*ptr) {
    free(cmsg->orig);
    return -1;
  }

  /* Take the command off the front */
  ptr += strcspn(ptr, " ");
  cmsg->cmd = (char *)malloc(ptr - start + 1);
  strncpy(cmsg->cmd, start, ptr - start);
  cmsg->cmd[ptr - start] = 0;
  irc_strupr(cmsg->cmd);

  /* Get the parameters */
  ptr += strspn(ptr, " ");
  if (*ptr) {
    int p;

    start = ptr;
    cmsg->numparams = 0;
    while (*ptr) {
      ptr += strcspn(ptr, " ");
      ptr += strspn(ptr, " ");
      cmsg->numparams++;
    }

    cmsg->params = (char **)malloc(sizeof(char *) * cmsg->numparams);
    cmsg->paramstarts = (char **)malloc(sizeof(char *) * cmsg->numparams);

    p = 0;
    ptr = start;
    while (*ptr) {
      ptr += strcspn(ptr, " ");

      cmsg->paramstarts[p] = start;
      cmsg->params[p] = (char *)malloc(ptr - start + 1);
      strncpy(cmsg->params[p], start, ptr - start);
      cmsg->params[p][ptr - start] = 0;

      ptr += strspn(ptr, " ");
      start = ptr;
      p++;
    }

  } else {
    cmsg->numparams = 0;
    cmsg->params = 0;
    cmsg->paramstarts = 0;
  }

  return cmsg->numparams;
}

/* Free an CTCP message */
void ircprot_freectcp(struct ctcpmessage *cmsg) {
  int i;

  for (i = 0; i < cmsg->numparams; i++)
    free(cmsg->params[i]);

  free(cmsg->cmd);
  free(cmsg->params);
  free(cmsg->paramstarts);
  free(cmsg->orig);
}

/* Strip silly characters from a username */
char *ircprot_sanitize_username(const char *str) {
  char *ret, *out, *in;

  out = in = ret = x_strdup(str);

  while (*in) {
    if ((*in >= 'A') && (*in <= 'Z')) {
      *(out++) = *in;
    } else if ((*in >= 'a') && (*in <= 'z')) {
      *(out++) = *in;
    } else if ((*in >= '0') && (*in <= '9')) {
      *(out++) = *in;
    }

    in++;
  }
  *out = 0;

  if (!strlen(ret)) {
     free(ret);
     ret = x_strdup(FALLBACK_USERNAME);
  } 

  return ret;
}
