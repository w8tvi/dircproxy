/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_client.c
 *  - Handling of clients connected to the proxy
 * --
 * @(#) $Id: irc_client.c,v 1.25 2000/08/30 10:51:44 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <crypt.h>

#include <dircproxy.h>
#include "sprintf.h"
#include "sock.h"
#include "dns.h"
#include "irc_log.h"
#include "irc_net.h"
#include "irc_prot.h"
#include "irc_string.h"
#include "irc_server.h"
#include "irc_client.h"
#include "logo.h"

/* forward declarations */
static int _ircclient_gotmsg(struct ircproxy *, const char *);
static int _ircclient_authenticate(struct ircproxy *, const char *);
static int _ircclient_got_details(struct ircproxy *, const char *,
                                  const char *, const char *, const char *);

/* New user mode bits */
#define RFC2812_MODE_W 0x04
#define RFC2812_MODE_I 0x08

/* Define MIN() */
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif /* MIN */

/* Called when a new client has connected */
int ircclient_connected(struct ircproxy *p) {
  p->client_status |= IRC_CLIENT_CONNECTED;

  ircclient_send_notice(p, "Looking up your hostname...");
  p->client_host = dns_hostfromaddr(p->client_addr.sin_addr);
  ircclient_send_notice(p, "Got your hostname.");

  debug("Client connected from %s", p->client_host);

  return 0;
}

/* Called when a client sends us stuff.  -1 = closed, 0 = done */
int ircclient_data(struct ircproxy *p) {
  char *str;
  int ret;

  str = 0;
  ret = sock_recv(p->client_sock, &str, "\r\n");

  switch (ret) {
    case SOCK_ERROR:
      debug("Socket error");

    case SOCK_CLOSED:
      if (p->die_on_close) {
        debug("Client disconnected, killing proxy");
        p->conn_class = 0;
        ircclient_close(p);

        ircserver_send_command(p, "QUIT", ":Leaving IRC - %s %s",
                               PACKAGE, VERSION);
        ircserver_close_sock(p);
      } else {
        debug("Client disconnected, detaching proxy");
        irclog_notice(p, 0, PACKAGE, "You disconnected");

        if ((p->server_status == IRC_SERVER_ACTIVE)
            && (p->client_status == IRC_CLIENT_ACTIVE)
            && !p->awaymessage && p->conn_class->away_message)
          ircserver_send_command(p, "AWAY", ":%s", p->conn_class->away_message);

        if ((p->client_status == IRC_CLIENT_ACTIVE)
            && p->conn_class->drop_modes) {
          char *mode;

          mode = x_sprintf("-%s", p->conn_class->drop_modes);
          debug("Auto-mode-change '%s'", mode);

          ircclient_change_mode(p, mode);
          if (p->server_status == IRC_SERVER_ACTIVE)
            ircserver_send_command(p, "MODE", "%s %s", p->nickname, mode);

          free(mode);
        }

        if ((p->client_status == IRC_CLIENT_ACTIVE)
            && !p->conn_class->other_log_always) {
          if (irclog_open(p, p->nickname))
            ircclient_send_notice(p, "(warning) Unable to log server/private "
                                  "messages");
        }
        if ((p->client_status == IRC_CLIENT_ACTIVE)
            && !p->conn_class->chan_log_always) {
          struct ircchannel *c;

          c = p->channels;
          while (c) {
            if (irclog_open(p, c->name))
              ircclient_send_notice(p, "(warning) Unable to log channel: %s",
                                    c->name);
            c = c->next;
          }
        }

        ircclient_close(p);
      }

      return -1;

    case SOCK_EMPTY:
      free(str);
      return 0;
  }

  debug(">> '%s'", str);
  _ircclient_gotmsg(p, str);
  free(str);

  return 0;
}

/* Called when we get an irc protocol data from a client */
static int _ircclient_gotmsg(struct ircproxy *p, const char *str) {
  struct ircmessage msg;

  if (ircprot_parsemsg(str, &msg) == -1)
    return -1;

  debug("c=%02x, s=%02x", p->client_status, p->server_status);

  if (!(p->client_status & IRC_CLIENT_AUTHED)) {
    if (!strcasecmp(msg.cmd, "PASS")) {
      if (msg.numparams >= 1) {
        _ircclient_authenticate(p, msg.params[0]);
      } else {
        ircclient_send_numeric(p, 416, ":Not enough parameters");
      }

    } else if (!strcasecmp(msg.cmd, "NICK")) {
      /* The PASS command MUST be sent before the NICK/USER combo
         (RFC 1459 section 4.11.1 and RFC 2812 section 3.1.1).
         However ircII deliberately sends the NICK command first
         *sigh*. */
      if (msg.numparams >= 1) {
        if (!(p->client_status & IRC_CLIENT_GOTNICK)
            || irc_strcmp(p->nickname, msg.params[0]))
          ircclient_change_nick(p, msg.params[0]);
      } else {
        ircclient_send_numeric(p, 431, ":No nickname given");
      }
    
    } else {
      ircclient_send_notice(p, "Please send /QUOTE PASS <password> to login");
    }

  } else if (!(p->client_status & IRC_CLIENT_GOTNICK)
             || !(p->client_status & IRC_CLIENT_GOTUSER)) {
    /* Full information not received yet. Only allow NICK or USER */

    if (!strcasecmp(msg.cmd, "NICK")) {
      if (msg.numparams >= 1) {
        /* If we already have a nick, only accept a change */
        if (!(p->client_status & IRC_CLIENT_GOTNICK)
            || irc_strcmp(p->nickname, msg.params[0])) {
          if (IS_SERVER_READY(p))
            ircserver_send_command(p, "NICK", ":%s", msg.params[0]);

          ircclient_change_nick(p, msg.params[0]);
        }
      } else {
        ircclient_send_numeric(p, 431, ":No nickname given");
      }

    } else if (!strcasecmp(msg.cmd, "USER")) {
      if (msg.numparams >= 4) {
        if (!(p->client_status & IRC_CLIENT_GOTUSER))
          _ircclient_got_details(p, msg.params[0], msg.params[1],
                                 msg.params[2], msg.params[3]);
      } else {
        ircclient_send_numeric(p, 416, ":Not enough parameters");
      }

    } else {
      ircclient_send_notice(p, "Please send /QUOTE NICK and /QUOTE USER");
    }

  } else {
    /* Okay, full command set.  By default we squelch everything, but the else
       clause turns this off.  Effectively it means that all handled commands
       are not passed to the server unless you set squelch to 0 */
    int squelch = 1;

    if (!strcasecmp(msg.cmd, "PASS")) {
      /* Ignore PASS */
    } else if (!strcasecmp(msg.cmd, "USER")) {
      /* Ignore USER */
    } else if (!strcasecmp(msg.cmd, "QUIT")) {
      /* Ignore QUIT */
    } else if (!strcasecmp(msg.cmd, "PONG")) {
      /* Ignore PONG */
    } else if (!strcasecmp(msg.cmd, "NICK")) {
      if (msg.numparams >= 1) {
        if (irc_strcmp(p->nickname, msg.params[0])) {
          if (p->server_status == IRC_SERVER_ACTIVE)
            ircserver_send_command(p, "NICK", ":%s", msg.params[0]);

          ircclient_change_nick(p, msg.params[0]);
        }
      } else {
        ircclient_send_numeric(p, 431, ":No nickname given");
      }

    } else if (!strcasecmp(msg.cmd, "DQUIT")) {
      /* Special command telling us to quit */
      p->conn_class = 0;
      ircclient_close(p);

      if (msg.numparams >= 1) {
        ircserver_send_command(p, "QUIT", ":%s", msg.params[0]);
      } else {
        ircserver_send_command(p, "QUIT", ":Leaving IRC - %s %s",
                               PACKAGE, VERSION);
      }
      ircserver_close_sock(p);

    } else if (!strcasecmp(msg.cmd, "AWAY")) {
      /* User marking themselves as away or back */
      squelch = 0;

      /* ircII sends an empty parameter to mark back *grr* */
      if ((msg.numparams >= 1) && strlen(msg.params[0])) {
        free(p->awaymessage);
        p->awaymessage = x_strdup(msg.params[0]);
      } else {
        free(p->awaymessage);
        p->awaymessage = 0;
      }
      
    } else if (!strcasecmp(msg.cmd, "PRIVMSG")) {
      /* Privmsgs from us get logged */
      if (msg.numparams >= 2) {
        char *str;

        str = x_strdup(msg.params[1]);
        ircprot_stripctcp(str);
        if (strlen(str)) {
          struct ircchannel *c;

          c = ircnet_fetchchannel(p, msg.params[0]);
          if (c) {
            char *tmp;

            tmp = x_sprintf("%s!%s@%s", p->nickname, p->username, p->hostname);
            irclog_msg(p, msg.params[0], tmp, "%s", str);
            free(tmp);
          }
        }
        free(str);
      }

      squelch = 0;

    } else if (!strcasecmp(msg.cmd, "NOTICE")) {
      /* Notices from us get logged */
      if (msg.numparams >= 2) {
        char *str;

        str = x_strdup(msg.params[1]);
        ircprot_stripctcp(str);
        if (strlen(str)) {
          struct ircchannel *c;

          c = ircnet_fetchchannel(p, msg.params[0]);
          if (c) {
            char *tmp;

            tmp = x_sprintf("%s!%s@%s", p->nickname, p->username, p->hostname);
            irclog_notice(p, msg.params[0], tmp, "%s", str);
            free(tmp);
          }
        }
        free(str);
      }

      squelch = 0;

    } else {
      squelch = 0;
    }

    /* Send command up to server? (We know there is one at this point) */
    if (!squelch)
      sock_send(p->server_sock, "%s\r\n", msg.orig);
  }

  /* If as a result of this command, we have sufficient information for
     the server, we can connect, or if we are connected then we can
     send the welcome text back to the user */
  if ((p->client_status & IRC_CLIENT_GOTNICK)
      && (p->client_status & IRC_CLIENT_GOTUSER) && !p->dead) {

    if (p->server_status != IRC_SERVER_ACTIVE) {
      if (!(p->server_status & IRC_SERVER_CREATED)) {
        ircserver_connect(p);
      } else if (!IS_SERVER_READY(p)) {
        ircclient_send_notice(p, "Connection to server is in progress...");
      }
    } else if (!(p->client_status & IRC_CLIENT_SENTWELCOME)) {
      ircclient_welcome(p);
    }
  }

  ircprot_freemsg(&msg);
  return 0;
}

/* Got a password */
static int _ircclient_authenticate(struct ircproxy *p, const char *password) {
  struct ircconnclass *cc;

  cc = connclasses;
  while (cc) {
#ifdef ENCRYPTED_PASSWORDS
    char *cmp;

    cmp = crypt(password, cc->password);

    if (!strcmp(cc->password, cmp)) {
#else
    if (!strcmp(cc->password, password)) {
#endif
      if (cc->masklist) {
        struct strlist *m;

        m = cc->masklist;
        while (m) {
          if (strmatch(p->client_host, m->str))
            break;

          m = m->next;
        }
      } else {
        break;
      }
    }

    cc = cc->next;
  }

  if (cc) {
    struct ircproxy *tmp_p;

    tmp_p = ircnet_fetchclass(cc);
    if (tmp_p && (tmp_p->client_status & IRC_CLIENT_CONNECTED)) {
      if (tmp_p->conn_class->disconnect_existing) {
        debug("Already connected, disconnecting existing");

        ircclient_send_error(tmp_p, "Collided with new user");
        ircclient_close(tmp_p);

        if (tmp_p->dead) {
          debug("Kicked off client, and they died");
          tmp_p = 0;
        }
      } else {
        debug("Already connected, disconnecting incoming");
        ircclient_send_error(p, "Already connected");
        ircclient_close(p);
        return -1;
      }
    }

    /* Check again, in case killing existing user killed the proxy */
    if (tmp_p) {
      debug("Attaching new client to old server session");

      tmp_p->client_sock = p->client_sock;
      tmp_p->client_status |= IRC_CLIENT_CONNECTED | IRC_CLIENT_AUTHED;
      tmp_p->client_addr = p->client_addr;

      /* Cope with NICK before PASS */
      if ((p->client_status & IRC_CLIENT_GOTNICK)
          && (!tmp_p->nickname || irc_strcmp(p->nickname, tmp_p->nickname))) {
        if (tmp_p->server_status == IRC_SERVER_ACTIVE)
          ircserver_send_peercmd(tmp_p, "NICK", ":%s", p->nickname);
        ircclient_change_nick(tmp_p, p->nickname);
      }

      if (!tmp_p->awaymessage && (tmp_p->server_status == IRC_SERVER_ACTIVE)
          && tmp_p->conn_class->away_message)
        ircserver_send_command(tmp_p, "AWAY", "");

      p->client_status = IRC_CLIENT_NONE;
      p->client_sock = -1;
      p->dead = 1;

    } else {
      p->conn_class = cc;
      p->client_status |= IRC_CLIENT_AUTHED;

      /* Okay, they've authed for the first time, make the log directory
         here */
      if (!p->conn_class->chan_log_dir || !p->conn_class->other_log_dir) {
        if (irclog_maketempdir(p))
          ircclient_send_notice(p, "(warning) Unable to create log "
                                   "directory, logging disabled");
      }

      if (p->conn_class->other_log_always) {
        if (irclog_open(p, ""))
          ircclient_send_notice(p, "(warning) Unable to log server/private "
                                "messages");
      }

    }

    return 0;
  }

  ircclient_send_numeric(p, 464, ":Password incorrect");
  ircclient_close(p);
  return -1;
}

/* Change the nickname */
int ircclient_change_nick(struct ircproxy *p, const char *newnick) {
  free(p->nickname);

  p->nickname = x_strdup(newnick);
  p->client_status |= IRC_CLIENT_GOTNICK;

  return 0;
}

/* Change the nickname to something we generate ourselves */
int ircclient_generate_nick(struct ircproxy *p, const char *tried) {
  char *c, *nick;
  int ret;

  c = nick = (char *)malloc(strlen(tried) + 2);
  strcpy(nick, tried);
  c += strlen(nick) - 1;

  /* Okay the principle of this is we add a '-' character to the end of the
     nickname.  If the last char is already - we increment it through 0..9
     once its past nine, we set the character before that to '-' and repeat.
     Keep going until we end up with 9999... as a nickname.  Once that happens
     we just use 'dircproxy' and do it all over again */
  if ((strlen(nick) < 9) && (*c != '-') && ((*c < '0') || (*c > '9'))) {
    *(++c) = '-';
    *(++c) = 0;
  } else {
    while (c >= tried) {
      if (*c == '-') {
        *c = '0';
        break;
      } else if ((*c >= '0') && (*c < '9')) {
        (*c)++;
        break;
      } else if (*c == '9') {
        c--;
      } else {
        *c = '-';
        break;
      }
    }

    if (c < tried) {
      free(nick);
      nick = x_strdup(FALLBACK_NICKNAME);
    }
  }

  ret = ircclient_change_nick(p, nick);
  free(nick);

  return 0;
}

/* Got some details */
static int _ircclient_got_details(struct ircproxy *p, const char *newusername,
                                  const char *newmode, const char *unused,
                                  const char *newrealname) {
  int mode;

  if (!p->username)
    p->username = x_strdup(newusername);

  if (!p->realname)
    p->realname = x_strdup(newrealname);

  /* RFC2812 states that the second parameter to USER is a numeric stating
     what default modes to set.  This disagrees with RFC1459.  We follow
     the newer one if we can, as the old hostname/servername combo were
     useless and ignored anyway. */
  mode = atoi(newmode);
  if (mode & RFC2812_MODE_W)
    ircclient_change_mode(p, "+w");
  if (mode & RFC2812_MODE_I)
    ircclient_change_mode(p, "+w");

  /* Okay we have the username now */
  p->client_status |= IRC_CLIENT_GOTUSER;

  return 0;
}

/* Got a personal mode change */
int ircclient_change_mode(struct ircproxy *p, const char *change) {
  char *ptr, *str;
  int add = 1;

  ptr = str = x_strdup(change);
  debug("Mode change from '%s', '%s'", (p->modes ? p->modes : ""), str);

  while (*ptr) {
    switch (*ptr) {
      case '+':
        add = 1;
        break;
      case '-':
        add = 0;
        break;
      default:
        if (add) {
          if (!p->modes || !strchr(p->modes, *ptr)) {
            if (p->modes) {
              p->modes = (char *)realloc(p->modes, strlen(p->modes) + 2);
            } else {
              p->modes = (char *)malloc(2);
              p->modes[0] = 0;
            }
            p->modes[strlen(p->modes) + 1] = 0;
            p->modes[strlen(p->modes)] = *ptr;
          }
        } else {
          char *pos;

          pos = strchr(p->modes, *ptr);
          if (p->modes && pos) {
            char *tmp;

            tmp = p->modes;
            p->modes = (char *)malloc(strlen(p->modes));
            *(pos++) = 0;
            strcpy(p->modes, tmp);
            strcpy(p->modes + strlen(p->modes), pos);
            free(tmp);

            if (!strlen(p->modes)) {
              free(p->modes);
              p->modes = 0;
            } 
          }
        }
    }

    ptr++;
  }

  debug("    now '%s'", (p->modes ? p->modes : ""));
  free(str);
  return 0;
}

/* Close the client socket */
int ircclient_close(struct ircproxy *p) {
  sock_close(p->client_sock);
  p->client_status &= ~(IRC_CLIENT_CONNECTED | IRC_CLIENT_AUTHED
                        | IRC_CLIENT_SENTWELCOME);

  /* No connection class, or no nick or user? Die! */
  if (!p->conn_class || !(p->client_status & IRC_CLIENT_GOTNICK)
      || !(p->client_status & IRC_CLIENT_GOTUSER)) {
    if (p->server_status & IRC_SERVER_CREATED)
      ircserver_close_sock(p); 
    p->dead = 1;
  }

  return p->dead;
}

/* send welcome headers to the user */
int ircclient_welcome(struct ircproxy *p) {
  ircclient_send_numeric(p, 1, ":Welcome to the Internet Relay Network %s",
                         p->nickname);
  ircclient_send_numeric(p, 2, ":Your host is %s running %s via %s %s",
                         p->servername,
                         (p->serverver ? p->serverver : "(unknown)"),
                         PACKAGE, VERSION);
  ircclient_send_numeric(p, 3, ":This proxy has been running since %s",
                         "(unknown)");
  if (p->serverver)
    ircclient_send_numeric(p, 4, "%s %s %s %s",
                           p->servername, p->serverver,
                           p->serverumodes, p->servercmodes);
  if (p->conn_class->motd_logo || p->conn_class->motd_stats)
    ircclient_send_numeric(p, 375, ":- %s Message of the Day -", PACKAGE);
  if (p->conn_class->motd_logo) {
    char *ver;
    int line;
   
    line = 0;
    while (logo[line]) {
      ircclient_send_numeric(p, 372, ":- %s", logo[line]);
      line++;
    }

    ver = x_sprintf(verstr, VERSION);
    ircclient_send_numeric(p, 372, ":- %s", ver);
    free(ver);
  }
  if (p->conn_class->motd_stats) {
    if (p->other_log.open && p->other_log.nlines) {
      char *s;

      if (p->conn_class->other_log_recall == -1) {
        s = x_strdup("all");
      } else if (p->conn_class->other_log_recall == 0) {
        s = x_strdup("none");
      } else {
        s = x_sprintf("%ld", p->conn_class->other_log_recall);
      }

      ircclient_send_numeric(p, 372, ":- %s", "You missed:");
      ircclient_send_numeric(p, 372, ":-   %ld server/private message%s "
                             "(%s will be sent)", p->other_log.nlines,
                             (p->other_log.nlines == 1 ? "" : "s"), s);
      ircclient_send_numeric(p, 372, ":-");

      free(s);
    }
    if (p->channels) {
      struct ircchannel *c;

      ircclient_send_numeric(p, 372, ":- %s", "You were on:");
      c = p->channels;
      while (c) {
        if (c->inactive) {
          ircclient_send_numeric(p, 372, ":-   %s %s", c->name,
                                 "(forcefully removed)");
        } else if (c->log.open && c->log.nlines) {
          char *s;

          if (p->conn_class->chan_log_recall == -1) {
            s = x_strdup("all");
          } else if (p->conn_class->chan_log_recall == 0) {
            s = x_strdup("none");
          } else {
            s = x_sprintf("%ld", MIN(c->log.nlines,
                                     p->conn_class->chan_log_recall));
          }

          ircclient_send_numeric(p, 372, ":-   %s. %ld line%s logged. "
                                 "(%s will be sent)", c->name, c->log.nlines,
                                 (c->log.nlines == 1 ? "" : "s"), s);

          free(s);
        } else {
          ircclient_send_numeric(p, 372, ":-   %s %s", c->name,
                                 "(not logged)");
        }
        c = c->next;
      }
      ircclient_send_numeric(p, 372, ":-");
    }
  }
  if (p->conn_class->motd_logo || p->conn_class->motd_stats) {
    ircclient_send_numeric(p, 376, ":End of /MOTD command.");
  } else {
    ircclient_send_numeric(p, 422, ":No MOTD.");
  }

  if (p->modes)
    ircclient_send_selfcmd(p, "MODE", "%s +%s", p->nickname, p->modes);

  /* Recall other log file */
  if (p->other_log.open) {
    irclog_autorecall(p, p->nickname);

    if (!p->conn_class->other_log_always)
      irclog_close(p, p->nickname);
  }

  if (p->awaymessage) {
    /* Ack.  There's no reason for a client to expect AWAY from a server,
       so we cheat and send a 306, reminding them what their away message
       was in the text.  This might not trick the client either, but hey,
       I can't do anything about that. */
    ircclient_send_numeric(p, 306, ":%s: %s",
                           "You left yourself away.  Your message was",
                           p->awaymessage);
  }

  if (p->channels) {
    struct ircchannel *c;

    c = p->channels;
    while (c) {
      if (!c->inactive) {
        ircclient_send_selfcmd(p, "JOIN", ":%s", c->name);
        ircserver_send_command(p, "TOPIC", ":%s", c->name);
        ircserver_send_command(p, "NAMES", ":%s", c->name);

        irclog_autorecall(p, c->name);
        if (!p->conn_class->chan_log_always)
          irclog_close(p, c->name);
      }

      c = c->next;
    }
  }

  irclog_notice(p, 0, PACKAGE, "You connected");
  ircnet_announce_status(p);

  p->client_status |= IRC_CLIENT_SENTWELCOME;
  return 0;
}

/* send a numeric to the user */
int ircclient_send_numeric(struct ircproxy *p, short numeric,
                           const char *format, ...) {
  va_list ap;
  char *msg;
  int ret;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  ret = sock_send(p->client_sock, ":%s %03d %s %s\r\n",
                  (p->servername ? p->servername : PACKAGE), numeric,
                  (p->nickname ? p->nickname : "*"), msg);

  free(msg);
  return ret;
}

/* send a notice to the user */
int ircclient_send_notice(struct ircproxy *p, const char *format, ...) {
  va_list ap;
  char *msg;
  int ret;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  ret = sock_send(p->client_sock, ":%s %s %s :%s\r\n", PACKAGE, "NOTICE",
                  (p->nickname ? p->nickname : "AUTH"), msg);

  free(msg);
  return ret;
}

/* send a notice to a channel */
int ircclient_send_channotice(struct ircproxy *p, const char *channel,
                              const char *format, ...) {
  va_list ap;
  char *msg;
  int ret;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  ret = sock_send(p->client_sock, ":%s %s %s :%s\r\n",
                  (p->servername ? p->servername : PACKAGE), "NOTICE",
                  channel, msg);

  free(msg);
  return ret;
}

/* send a command to the user from the server */
int ircclient_send_command(struct ircproxy *p, const char *command,
                           const char *format, ...) {
  va_list ap;
  char *msg;
  int ret;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  ret = sock_send(p->client_sock, ":%s %s %s\r\n",
                  (p->servername ? p->servername : PACKAGE), command, msg);

  free(msg);
  return ret;
}

/* send a command to the user making it look like its from them */
int ircclient_send_selfcmd(struct ircproxy *p, const char *command,
                           const char *format, ...) {
  char *msg, *prefix;
  va_list ap;
  int ret;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  if (p->nickname && p->username && p->hostname) {
    prefix = x_sprintf(":%s!%s@%s ", p->nickname, p->username, p->hostname);
  } else if (p->nickname) {
    prefix = x_sprintf(":%s ", p->nickname);
  } else {
    prefix = (char *)malloc(1);
    prefix[0] = 0;
  }

  ret = sock_send(p->client_sock, "%s%s %s\r\n", prefix, command, msg);

  free(prefix);
  free(msg);
  return ret;
}

/* send an error to the user */
int ircclient_send_error(struct ircproxy *p, const char *format, ...) {
  va_list ap;
  char *msg;
  int ret;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  ret = sock_send(p->client_sock, "%s :%s: %s\r\n", "ERROR", "Closing Link",
                  msg);

  free(msg);
  return ret;
}
