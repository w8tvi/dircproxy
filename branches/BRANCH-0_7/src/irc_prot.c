/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_prot.c
 *  - IRC protocol message parsing
 *  - IRC x!y@z parsing
 * --
 * @(#) $Id: irc_prot.c,v 1.5.2.1 2000/10/16 11:21:00 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdlib.h>
#include <string.h>

#include <dircproxy.h>
#include "sprintf.h"
#include "irc_prot.h"
#include "irc_string.h"

/* forward declarations */
static int _ircprot_parse_prefix(char *, struct ircsource *);
static int _ircprot_count_params(char *);
static int _ircprot_get_params(char *, char ***);
static char *_ircprot_skip_spaces(char *);

/* Parse an IRC message. num of params or -1 if no command */
int ircprot_parsemsg(const char *message, struct ircmessage *msg) {
  char *start, *ptr;

  /* Copy the original message as well */
  ptr = start = msg->orig = x_strdup(message);

  /* Begins with a prefix? */
  if (*ptr == ':') {
    char *tmp;

    while (*ptr && (*ptr != ' ')) ptr++;

    tmp = (char *)malloc(ptr - start);
    strncpy(tmp, start + 1, ptr - start - 1);
    tmp[ptr - start - 1] = 0;

    _ircprot_parse_prefix(tmp, &(msg->src));
    free(tmp);

    ptr = _ircprot_skip_spaces(ptr);
  } else {
    /* It just came from our peer */
    msg->src.name = msg->src.username = msg->src.hostname = 0;
    msg->src.fullname = 0;
    msg->src.type = IRC_PEER;
  }

  /* No command? */
  if (!*ptr) {
    free(msg->src.name);
    free(msg->src.username);
    free(msg->src.hostname);
    free(msg->src.fullname);
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
    _ircprot_get_params(ptr, &(msg->params));
  } else {
    msg->params = 0;
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
  free(msg->cmd);
  free(msg->params);
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
static int _ircprot_get_params(char *message, char ***params) {
  char *ptr, *start;
  int param;

  param = 0;
  ptr = start = message;

  while (*ptr) {
    if (*ptr == ':') {
      (*params)[param] = x_strdup(ptr + 1);
      break;
    } else {
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

/* Strip CTCP from a strip */
void ircprot_stripctcp(char *msg) {
  char *out, *in;
  int inctcp;

  out = in = msg;
  inctcp = 0;

  while (*in) {
    if (*in == 1) {
      inctcp = (inctcp ? 0 : 1);
    } else if (!inctcp) {
      *(out++) = *in;
    }
    in++;
  }
  *out = 0;
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
