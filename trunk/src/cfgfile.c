/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * cfgfile.c
 *  - reading of configuration file
 * --
 * @(#) $Id: cfgfile.c,v 1.6 2000/08/24 11:10:21 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dircproxy.h>
#include "sprintf.h"
#include "irc_net.h"
#include "cfgfile.h"

/* forward declaration */
static int _cfg_read_numeric(char **, long *);
static int _cfg_read_string(char **, char **);

/* Whitespace */
#define WS " \t\r\n"

/* Quick and easy "Unmatched Quote" define */
#define UNMATCHED_QUOTE { fprintf(stderr, "%s: Unmatched quote for key '%s' " \
                                "at line %ld of %s\n", progname, key, line, \
                                filename); valid = 0; break; }

/* Read a config file */
int cfg_read(const char *filename) {
  long server_maxattempts, server_maxinitattempts;
  long server_retry, server_dnsretry;
  struct ircconnclass *class;
  long channel_rejoin;
  char *server_port;
  int valid;
  long line;
  FILE *fd;

  class = 0;
  line = 0;
  valid = 1;
  fd = fopen(filename, "r");
  if (!fd)
    return -1;

  /* Initialise using defaults */
  server_port = x_strdup(DEFAULT_SERVER_PORT);
  server_retry = DEFAULT_SERVER_RETRY;
  server_dnsretry = DEFAULT_SERVER_DNSRETRY;
  server_maxattempts = DEFAULT_SERVER_MAXATTEMPTS;
  server_maxinitattempts = DEFAULT_SERVER_MAXINITATTEMPTS;
  channel_rejoin = DEFAULT_CHANNEL_REJOIN;

  while (valid) {
    char buff[512], *buf;

    if (!fgets(buff, 512, fd))
      break;
    line++;

    buf = buff;
    while ((buf < (buff + 512)) && strlen(buf)) {
      char *key;

      /* Skip whitespace, and ignore lines that are comments */
      buf += strspn(buf, WS);
      if (*buf == '#')
        break;

      /* Find the end of the key, and if there isn't one, exit */
      key = buf;
      buf += strcspn(buf, WS);
      if (!strlen(key))
        break;

      /* If there isn't a newline, then we could reach the end of the
         buffer - so be a bit careful and ensure we don't skip past it */
      if (*buf) {
        *(buf++) = 0;

        /* Close brace is only allowed when class is defined
           (we check here, because its a special case and has no value) */
        if (!strcmp(key, "}") && !class) {
          fprintf(stderr, "%s: Close brace without open at line %ld of %s\n",
                  progname, line, filename);
          valid = 0;
          break;
        }

        buf += strspn(buf, WS);
      }

      /* If we reached the end of the buffer, or a comment, that means
         this key has no value.  Unless the key is '}' then thats bad */
      if ((!*buf || (*buf == '#')) && strcmp(key, "}")) {
        fprintf(stderr, "%s: Missing value for key '%s' at line %ld of %s\n",
                progname, key, line, filename);
        valid = 0;
        break;
      }

      /* Handle the keys */
      if (!class && !strcasecmp(key, "listen_port")) {
        /* listen_port 57000
           listen_port "dircproxy"    # From /etc/services
         
           ( cannot go in a connection {} ) */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        free(listen_port);
        listen_port = str;

      } else if (!strcasecmp(key, "server_port")) {
        /* server_port 6667
           server_port "irc"    # From /etc/services */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (class) {
          free(class->server_port);
          class->server_port = str;
        } else {
          free(server_port);
          server_port = str;
        }

      } else if (!strcasecmp(key, "server_retry")) {
        /* server_retry 15 */
        _cfg_read_numeric(&buf,
                          &(class ? class->server_retry : server_retry));

      } else if (!strcasecmp(key, "server_dnsretry")) {
        /* server_dnsretry 15 */
        _cfg_read_numeric(&buf,
                          &(class ? class->server_dnsretry : server_dnsretry));

      } else if (!strcasecmp(key, "server_maxattempts")) {
        /* server_maxattempts 0 */
        _cfg_read_numeric(&buf,
                          &(class ? class->server_maxattempts
                                  : server_maxattempts));

      } else if (!strcasecmp(key, "server_maxinitattempts")) {
        /* server_maxinitattempts 5 */
        _cfg_read_numeric(&buf,
                          &(class ? class->server_maxinitattempts
                                  : server_maxinitattempts));

      } else if (!strcasecmp(key, "channel_rejoin")) {
        /* channel_rejoin 5 */
        _cfg_read_numeric(&buf,
                          &(class ? class->channel_rejoin : channel_rejoin));
 
      } else if (!class && !strcasecmp(key, "log_autorecall")) {
        /* log_autorecall 128 */
        _cfg_read_numeric(&buf, &log_autorecall);
 
      } else if (!class && !strcasecmp(key, "connection")) {
        /* connection {
             :
             :
           } */

        if (*buf != '{') {
          /* Connection is a bit special, because the only valid value is '{'
             the internals are handled elsewhere */
          fprintf(stderr,
                  "%s: Expected open brace for key '%s' at line %ld of %s\n",
                  progname, key, line, filename);
          valid = 0;
          break;
        }
        buf++;

        /* Allocate memory, it'll be filled later */
        class = (struct ircconnclass *)malloc(sizeof(struct ircconnclass));
        memset(class, 0, sizeof(struct ircconnclass));
        class->awaymessage = x_strdup(DEFAULT_DETACH_AWAY);
        class->server_port = x_strdup(server_port);
        class->server_retry = server_retry;
        class->server_dnsretry = server_dnsretry;
        class->server_maxattempts = server_maxattempts;
        class->server_maxinitattempts = server_maxinitattempts;
        class->channel_rejoin = channel_rejoin;

      } else if (class && !strcasecmp(key, "password")) {
        /* connection {
             :
             password "foo"
             :
           } */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        free(class->password);
        class->password = str;

      } else if (class && !strcasecmp(key, "local_address")) {
        /* connection {
             :
             local_address "i.am.a.virtual.host.com"
             :
           } */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        free(class->bind);
        class->bind = str;

      } else if (class && !strcasecmp(key, "away_message")) {
        /* connection {
             :
             away_message "Not available, messages are logged"
             :
           } */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        free(class->awaymessage);
        if (strcmp(str, "none")) {
          class->awaymessage = str;
        } else {
          free(str);
          class->awaymessage = 0;
        }

      } else if (class && !strcasecmp(key, "server")) {
        /* connection {
             :
             server "irc.linux.com"
             server "irc.linux.com:6670"    # Port other than default
             :
           } */
        struct strlist *s;
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        s = (struct strlist *)malloc(sizeof(struct strlist));
        s->str = str;
        s->next = 0;

        if (class->servers) {
          struct strlist *ss;

          ss = class->servers;
          while (ss->next)
            ss = ss->next;

          ss->next = s;
        } else {
          class->servers = s;
        }

      } else if (class && !strcasecmp(key, "from")) {
        /* connection {
             :
             from "static-132.myisp.com"    # Static hostname
             from "*.myisp.com"             # Masked hostname
             from "192.168.1.1"             # Specific IP
             from "192.168.*"               # IP range
             :
           } */
        struct strlist *s;
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        s = (struct strlist *)malloc(sizeof(struct strlist));
        s->str = str;
        s->next = class->masklist;
        class->masklist = s;

      } else if (class && !strcmp(key, "}")) {
        /* Check that a password and at least one server were defined */
        if (!class->password) {
          fprintf(stderr, "%s: Connection class defined without password "
                  "before line %ld of %s\n", progname, line, filename);
          valid = 0;
        } else if (!class->servers) {
          fprintf(stderr, "%s: Connection class defined without a server "
                  "before line %ld of %s\n", progname, line, filename);
          valid = 0;
        }

        /* Add to the list of servers if valid, otherwise free it */
        if (valid) {
          class->next_server = class->servers;
          class->next = connclasses;
          connclasses = class;
          class = 0;
        } else {
          ircnet_freeconnclass(class);
          class = 0;
          break;
        }

      } else {
        /* Bad key! */
        fprintf(stderr, "%s: Unknown config file key '%s' at line %ld of %s\n",
                progname, key, line, filename);
        valid = 0;
        break;
      }

      /* Skip whitespace.  The only things that can trail are comments
         and close braces, and we re-pass to do those */
      buf += strspn(buf, WS);
      if (*buf && (*buf != '#') && (*buf != '}')) {
        fprintf(stderr, "%s: Unexpected data at and of line %ld of %s\n",
                progname, line, filename);
        valid = 0;
        break;
      }
    }
  }

  /* Argh, untidy stuff left around */
  if (class) {
    ircnet_freeconnclass(class);
    class = 0;
    if (valid) {
      fprintf(stderr, "%s: Unmatched open brace in %s\n", progname, filename);
      valid = 0;
    }
  }

  fclose(fd);
  free(server_port);
  return (valid ? 0 : -1);
}

/* Read a numeric value */
static int _cfg_read_numeric(char **buf, long *val) {
  char *ptr;

  ptr = *buf;
  *buf += strcspn(*buf, WS);
  *((*buf)++) = 0;

  *val = atol(ptr);

  return 0;
}

/* Read a string value */
static int _cfg_read_string(char **buf, char **val) {
  char *ptr;

  if (**buf == '"') {
    ptr = ++(*buf);

    while (1) {
      *buf += strcspn(*buf, "\"");
      if (**buf != '"') {
        return -1;
      } else if (*(*buf - 1) == '\\') {
        (*buf)++;
        continue;
      } else {
        break;
      }
    }
  } else {
    ptr = *buf;
    *buf += strcspn(*buf, WS);
  }
  *((*buf)++) = 0;

  *val = x_strdup(ptr);

  return 0;
}
