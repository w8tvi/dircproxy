/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 *
 * irc_client.c
 *  - Handling of clients connected to the proxy
 *  - Functions to send data to the client in the correct protocol format
 * --
 * @(#) $Id: irc_client.c,v 1.96 2004/04/24 09:34:37 fharvey Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include <dircproxy.h>

#ifdef HAVE_CRYPT_H
# include <crypt.h>
#else /* HAVE_CRYPT_H */
# include <unistd.h>
#endif /* HAVE_CRYPT_H */

#include "sprintf.h"
#include "net.h"
#include "dns.h"
#include "timers.h"
#include "dcc_net.h"
#include "irc_log.h"
#include "irc_net.h"
#include "irc_prot.h"
#include "irc_string.h"
#include "irc_server.h"
#include "irc_client.h"
#include "logo.h"

/* forward declarations */
static void _ircclient_connected2(struct ircproxy *, void *, const char *,
                                  const char *);
static void _ircclient_data(struct ircproxy *, int);
static void _ircclient_error(struct ircproxy *, int, int);
static int _ircclient_detach(struct ircproxy *, const char *);
static int _ircclient_gotmsg(struct ircproxy *, const char *);
static int _ircclient_authenticate(struct ircproxy *, const char *);
static void _ircclient_resetnick(struct ircproxy *, void *);
static int _ircclient_got_details(struct ircproxy *, const char *,
                                  const char *, const char *, const char *);
static int _ircclient_motd(struct ircproxy *);
static void _ircclient_timedout(struct ircproxy *, void *);
static int _ircclient_send_dccreject(struct ircproxy *, const char *,
                                     const char *);
int  _ircclient_handle_privmsg(struct ircproxy *, struct ircmessage);
void _ircclient_handle_recall(struct ircproxy *, struct ircmessage);
void _ircclient_handle_users(struct ircproxy *, struct ircmessage);
void _ircclient_handle_kill(struct ircproxy *, struct ircmessage);
void _ircclient_handle_notify(struct ircproxy *, struct ircmessage);
int  _ircclient_handle_jump(struct ircproxy *, struct ircmessage);
void _ircclient_handle_status(struct ircproxy *, struct ircmessage);
void _ircclient_handle_help(struct ircproxy *, struct ircmessage);

/* New user mode bits */
#define RFC2812_MODE_W 0x04
#define RFC2812_MODE_I 0x08

/* Time/date format for strftime(3) */
#define START_TIMEDATE_FORMAT "%a, %d %b %Y %H:%M:%S %z"

/* Define MIN() */
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif /* MIN */

/* Called when a new client has connected */
int ircclient_connected(struct ircproxy *p) {
  char ip[DNS_MAX_HOSTLEN];

  ircclient_send_notice(p, "Looking up your hostname...");

  net_ntop(&p->client_addr, ip, sizeof(ip));
  dns_hostfromaddr(p, 0, ip, (dns_fun_t) _ircclient_connected2);

  return 0;
}

/* Called once a client DNS lookup has completed */
static void _ircclient_connected2(struct ircproxy *p, void *data,
                                  const char *ip, const char *name) {
  p->client_host = x_strdup(name);
  if (!p->hostname)
    p->hostname = x_strdup(name);
  ircclient_send_notice(p, "Got your hostname.");

  p->client_status |= IRC_CLIENT_CONNECTED;
  net_hook(p->client_sock, SOCK_NORMAL, (void *)p,
           ACTIVITY_FUNCTION(_ircclient_data),
           ERROR_FUNCTION(_ircclient_error));

  debug("Client connected from %s", p->client_host);

  timer_new((void *)p, "client_auth", g.client_timeout,
            TIMER_FUNCTION(_ircclient_timedout), (void *)0);
}

/* Called when a client sends us stuff. */
static void _ircclient_data(struct ircproxy *p, int sock) {
  char *str;

  if (sock != p->client_sock) {
    error("Unexpected socket %d in _ircclient_data, expected %d", sock,
          p->client_sock);
    net_close(&sock);
    return;
  }

  str = 0;
  while (!p->dead && (p->client_status & IRC_CLIENT_CONNECTED)
         && net_gets(p->client_sock, &str, "\r\n") > 0) {
    debug(">> '%s'", str);
    _ircclient_gotmsg(p, str);
    free(str);
  }
}

/* Called on client disconnection or error */
static void _ircclient_error(struct ircproxy *p, int sock, int bad) {
  if (sock != p->client_sock) {
    error("Unexpected socket %d in _ircclient_error, expected %d", sock,
          p->client_sock);
    net_close(&sock);
    return;
  }

  if (bad) {
    debug("Socket error");
  } else {
    debug("Client disconnect");
  }

  _ircclient_detach(p, 0);
}

/* Called to detach an irc client */
static int _ircclient_detach(struct ircproxy *p, const char *message) {
  if (p->die_on_close) {
    debug("Killing proxy");

    if (message) {
      ircserver_send_command(p, "QUIT", ":%s", message);
    } else if (p->conn_class && p->conn_class->quit_message) {
      ircserver_send_command(p, "QUIT", ":%s", p->conn_class->quit_message);
    } else {
      ircserver_send_command(p, "QUIT", ":Leaving IRC - %s %s",
                             PACKAGE, VERSION);
    }
    ircserver_close_sock(p);

    p->conn_class = 0;
    ircclient_close(p);

  } else {
    debug("Detaching proxy");
    if (p->client_status == IRC_CLIENT_ACTIVE)
      irclog_log(p, IRC_LOG_CLIENT, IRC_LOGFILE_ALL, PACKAGE,
                 "You disconnected");

    /* Drop modes */
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

    /* Send detach message to all channels we're on */
    if ((p->server_status == IRC_SERVER_ACTIVE)
        && (p->client_status == IRC_CLIENT_ACTIVE)) {
      if (p->conn_class->detach_message) {
        struct ircchannel *c;
        int slashme;
        char *msg;

        msg = p->conn_class->detach_message;
        if ((strlen(msg) >= 5) && !strncasecmp(msg, "/me ", 4)) {
          /* Starts with /me */
          slashme = 1;
          msg += 4;
        } else {
          slashme = 0;
        }

        c = p->channels;
        while (c) {
          if (!c->inactive && !c->unjoined) {
            if (slashme) {
              ircserver_send_command(p, "PRIVMSG", "%s :\001ACTION %s\001",
                                     c->name, msg);
            } else {
              ircserver_send_command(p, "PRIVMSG", "%s :%s", c->name, msg);
            }
          }
          c = c->next;
        }
      }
    }

    /* Leave channels until they come back */
    if ((p->server_status == IRC_SERVER_ACTIVE)
        && (p->client_status == IRC_CLIENT_ACTIVE)) {
      if (p->conn_class->channel_leave_on_detach) {
        struct ircchannel *c;

        c = p->channels;
        while (c) {
          struct ircchannel *t;

          t = c;
          c = c->next;

          /* Leave the channel and decide whether to delete it or rejoin */
          if (!t->inactive && !t->unjoined) {
            ircserver_send_command(p, "PART", ":%s", t->name);
            if (p->conn_class->channel_rejoin_on_attach) {
              t->unjoined = 1;
            } else {
              ircnet_delchannel(p, t->name);
            }
          }
        }
      }
    }

    /* Set away message */
    if ((p->server_status == IRC_SERVER_ACTIVE)
        && (p->client_status == IRC_CLIENT_ACTIVE)) {
      if (message) {
        ircserver_send_command(p, "AWAY", ":%s", message);
      } else if (!p->awaymessage && p->conn_class->away_message) {
        ircserver_send_command(p, "AWAY", ":%s", p->conn_class->away_message);
      }
    }

    /* Change Nickname */
    if ((p->client_status == IRC_CLIENT_ACTIVE)
        && p->conn_class->detach_nickname) {
      char *nick, *ptr;

      nick = x_strdup(p->conn_class->detach_nickname);
      ptr = strchr(nick, '*');
      if (ptr) {
        char *newnick;

        *(ptr++) = 0;
        newnick = x_sprintf("%s%s%s", nick, p->nickname, ptr);
        free(nick);
        nick = newnick;
      }
      debug("Auto-nick-change '%s'", nick);

      /* We need to remember what the setnickname is now so when the client
         comes back we can reset it again.  So put it in oldnickname. */
      if (p->oldnickname)
        free(p->oldnickname);
      p->oldnickname = p->setnickname;
      p->setnickname = 0;

      ircclient_change_nick(p, nick);

      free(nick);
    }

    /* Open private_log */
    if ((p->client_status == IRC_CLIENT_ACTIVE)
        && p->conn_class->private_log_enabled
        && !p->conn_class->private_log_always) {
      if (irclog_open(p, p->nickname))
        ircclient_send_notice(p, "(warning) Unable to log private messages");
    }

    /* Open server log */
    if ((p->client_status == IRC_CLIENT_ACTIVE)
        && p->conn_class->server_log_enabled
        && !p->conn_class->server_log_always) {
      if (irclog_open(p, 0))
        ircclient_send_notice(p, "(warning) Unable to log server messages");
    }

    /* Open channel logs */
    if ((p->client_status == IRC_CLIENT_ACTIVE)
        && p->conn_class->chan_log_enabled
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

    /* Close the socket */
    ircclient_close(p);
  }

  return 0;
}

/* Called when we get an irc protocol data from a client */
static int _ircclient_gotmsg(struct ircproxy *p, const char *str) {
  struct ircmessage msg;

  if (ircprot_parsemsg(str, &msg) == -1)
    return -1;

  debug("c=%02x, s=%02x", p->client_status, p->server_status);

  if (!(p->client_status & IRC_CLIENT_AUTHED)) {
    /* Accept PASS, NICK and USER commands only until we've authenticated */
    if (!irc_strcasecmp(msg.cmd, "PASS")) {
      if (msg.numparams >= 1) {
        if (p->password)
          free(p->password);
        p->password = x_strdup(msg.params[0]);
        p->client_status |= IRC_CLIENT_GOTPASS;
      } else {
        ircclient_send_numeric(p, 461, ":Not enough parameters");
      }

    } else if (!irc_strcasecmp(msg.cmd, "NICK")) {
      if (msg.numparams >= 1) {
        if (!(p->client_status & IRC_CLIENT_GOTNICK)
            || strcmp(p->nickname, msg.params[0]))
          ircclient_change_nick(p, msg.params[0]);
      } else {
        ircclient_send_numeric(p, 431, ":No nickname given");
      }

    } else if (!irc_strcasecmp(msg.cmd, "USER")) {
      if (msg.numparams >= 4) {
        if (!(p->client_status & IRC_CLIENT_GOTUSER))
          _ircclient_got_details(p, msg.params[0], msg.params[1],
                                 msg.params[2], msg.params[3]);
      } else {
        ircclient_send_numeric(p, 461, ":Not enough parameters");
      }

    } else if (!(p->client_status & IRC_CLIENT_GOTPASS)) {
      ircclient_send_notice(p, "Please send /QUOTE PASS <password> to login");

    } else {
      ircclient_send_notice(p, "Please send /QUOTE NICK and /QUOTE USER");
    }

  } else if (!(p->client_status & IRC_CLIENT_GOTNICK)) {
    /* We've lost the nickname */
    if (!irc_strcasecmp(msg.cmd, "NICK")) {
      if (msg.numparams >= 1) {
        ircclient_change_nick(p, msg.params[0]);
      } else {
        ircclient_send_numeric(p, 431, ":No nickname given");
      }

    } else {
      ircclient_send_notice(p, "Please send a /NICK command");
    }

  } else {
    /* The server MUST be active to use most of the commands.  The only
       exception is /DIRCPROXY. */

    if (p->server_status == IRC_SERVER_ACTIVE) {
      /* By default we squelch everything, but the else clause turns this off.
         Effectively it means that all handled commands are not passed to the
         server unless you set squelch to 0 */
      int squelch = 1;

      if (!irc_strcasecmp(msg.cmd, "PASS")) {
        /* Ignore PASS */
      } else if (!irc_strcasecmp(msg.cmd, "USER")) {
        /* Ignore USER */
      } else if (!irc_strcasecmp(msg.cmd, "DIRCPROXY")) {
        /* Ignore DIRCPROXY (handled in a minute) */
      } else if (!irc_strcasecmp(msg.cmd, "QUIT")) {
        /* User wants to detach */
        ircnet_announce_status(p);
        ircclient_send_error(p, "Detached from %s %s", PACKAGE, VERSION);
        _ircclient_detach(p, 0);
        ircprot_freemsg(&msg);
        return 0;

      } else if (!irc_strcasecmp(msg.cmd, "PONG")) {
        /* Ignore PONG */

      } else if (!irc_strcasecmp(msg.cmd, "NICK")) {
        /* User changing their nickname */
        if (msg.numparams >= 1) {
          ircclient_change_nick(p, msg.params[0]);
        } else {
          ircclient_send_numeric(p, 431, ":No nickname given");
        }

      } else if (!irc_strcasecmp(msg.cmd, "AWAY")) {
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

      } else if (!irc_strcasecmp(msg.cmd, "MOTD")) {
        /* User requesting the message of the day from the server */
        p->allow_motd = 1;
        squelch = 0;

      } else if (!irc_strcasecmp(msg.cmd, "PING")) {
        /* User requesting a ping from the server */
        p->allow_pong = 1;
        squelch = 0;

      } else if (!irc_strcasecmp(msg.cmd, "PRIVMSG")) {
        /* All PRIVMSGs go to the server unless we fiddle */
        squelch = _ircclient_handle_privmsg(p, msg);

      } else if (!irc_strcasecmp(msg.cmd, "NOTICE")) {
        /* Notices from us get logged */
        if (msg.numparams >= 2) {
          char *str;

          ircprot_stripctcp(msg.params[1], &str, 0);

          if (str && strlen(str)) {
            char *tmp;

            tmp = x_sprintf("%s!%s@%s", p->nickname, p->username,
                            p->hostname);
            irclog_log(p, IRC_LOG_NOTICE, msg.params[0], tmp, "%s", str);
            free(tmp);
          }
          free(str);
        }

        if (p->conn_class->idle_maxtime)
          ircserver_resetidle(p);
        squelch = 0;

      } else {
        squelch = 0;
      }

      /* Send command up to server? (We know there is one at this point) */
      if (!squelch)
        net_send(p->server_sock, "%s\r\n", msg.orig);

    } else if (irc_strcasecmp(msg.cmd, "DIRCPROXY")) {
      /* Command didn't (and won't be) handled.  We better stick to the
         RFC and send a RPL_TRYAGAIN back. */
      ircclient_send_numeric(p, 263, "%s :Please wait a while and try again.",
                             msg.cmd);
    }

    /* /DIRCPROXY can be used at *any* time, if it ever sends anything to the
       server it has to do it explicitly (no automatic sending) and has to
       check there is a server there */
    if (!irc_strcasecmp(msg.cmd, "DIRCPROXY")) {
      if (msg.numparams >= 1) {
        if (!irc_strcasecmp(msg.params[0], "RECALL")) {
          _ircclient_handle_recall(p, msg);

        } else if (p->conn_class->allow_persist
                   && !irc_strcasecmp(msg.params[0], "PERSIST")) {
          /* User wants a die_on_close proxy to persist */
          if (p->die_on_close) {
            if (p->conn_class->disconnect_on_detach) {
              /* Its die_on_close because of configuration, can't dedicate! */
              p->die_on_close = 0;
              ircnet_announce_dedicated(p);
            } else if (!ircnet_dedicate(p)) {
              /* Okay, it was inetd - we can dedicate this */
              p->die_on_close = 0;
            } else {
              ircclient_send_notice(p, "Could not persist");
            }
          } else {
            ircnet_announce_dedicated(p);
          }
	} else if (!irc_strcasecmp(msg.params[0], "GET")) {
	   /* User want to get a configuration item */
	   if (p->conn_class->allow_dynamic >= 1) {
	      // todo
	   } else {	   
	      ircclient_send_notice(p, "You are not authorized to use GET command");
	   }
	} else if (!irc_strcasecmp(msg.params[0], "SET")) {
	   /* User want to set a configuration item */	      
	   if (p->conn_class->allow_dynamic == 2) {
	      // todo
	   } else {	   
	      ircclient_send_notice(p, "You are not authorized to use SET command");
	   }
        } else if (!irc_strcasecmp(msg.params[0], "RELOAD")) {
          /* User wants to reload the configuration file */
          ircclient_send_notice(p, "RELOAD in progress");
          reload();

        } else if (!irc_strcasecmp(msg.params[0], "DETACH")) {
          /* User wants to detach and can't be bothered to use /QUIT */
          ircnet_announce_status(p);
          ircclient_send_error(p, "Detached from %s %s", PACKAGE, VERSION);

          /* Optional AWAY message can be supplied */
          if ((msg.numparams >= 2) && strlen(msg.paramstarts[1])) {
            _ircclient_detach(p, msg.paramstarts[1]);
          } else {
            _ircclient_detach(p, 0);
          }
          ircprot_freemsg(&msg);
          return 0;

        } else if (!irc_strcasecmp(msg.params[0], "QUIT")) {
          /* User wants to detach and end their proxy session */

          if (IS_SERVER_READY(p)) {
            /* Optional QUIT message can be supplied */
            if ((msg.numparams >= 2) && strlen(msg.paramstarts[1])) {
              ircserver_send_command(p, "QUIT", ":%s", msg.paramstarts[1]);
            } else if (p->conn_class->quit_message) {
              ircserver_send_command(p, "QUIT", ":%s",
                                     p->conn_class->quit_message);
            } else {
              ircserver_send_command(p, "QUIT", ":Leaving IRC - %s %s",
                                     PACKAGE, VERSION);
            }
          }

          ircserver_close_sock(p);
          p->conn_class = 0;
          ircclient_close(p);
          ircprot_freemsg(&msg);
          return 0;

        } else if (!irc_strcasecmp(msg.params[0], "MOTD")) {
          /* Display message of the day file */
          _ircclient_motd(p);

        } else if (p->conn_class->allow_die
                   && !irc_strcasecmp(msg.params[0], "DIE")) {
          /* User wants to kill us :( */
          ircclient_send_notice(p, "I'm melting!");
          stop();

        } else if (p->conn_class->allow_users
                   && !irc_strcasecmp(msg.params[0], "USERS")) {
					_ircclient_handle_users(p, msg);

        } else if (p->conn_class->allow_kill
                   && !irc_strcasecmp(msg.params[0], "KILL")) {
          _ircclient_handle_kill(p, msg);
	   
	} else if (p->conn_class->allow_notify
		   && !irc_strcasecmp(msg.params[0], "NOTIFY")) {
	  _ircclient_handle_notify(p, msg);
	   
        } else if (!irc_strcasecmp(msg.params[0], "SERVERS")) {
          struct strlist *s;
          int i;

          s = p->conn_class->servers;
          i = 0;

          /* User wants a server list */
          if (s) {
            ircclient_send_notice(p, "You can connect to:");
          } else {
            ircclient_send_notice(p, "No servers");
          }

          while (s) {
            ircclient_send_notice(p, "-%s %2d. %s",
                                  (s == p->conn_class->next_server ? ">" : " "),
                                  ++i, s->str);
            s = s->next;
          }

        } else if (p->conn_class->allow_jump
                   && (!irc_strcasecmp(msg.params[0], "JUMP")
                       || !irc_strcasecmp(msg.params[0], "CONNECT"))) {
          if (_ircclient_handle_jump(p, msg))
            return 0;

        } else if (p->conn_class->allow_host
                   && !irc_strcasecmp(msg.params[0], "HOST")) {
          /* User wants to change their hostname */
          free(p->conn_class->local_address);
          p->conn_class->local_address = 0;

          if (msg.numparams >= 2) {
            if (irc_strcasecmp(msg.params[1], "none"))
              p->conn_class->local_address = x_strdup(msg.params[1]);

          } else if (p->conn_class->orig_local_address) {
            p->conn_class->local_address =
                x_strdup(p->conn_class->orig_local_address);
          }

          ircserver_connectagain(p);

          /* We have no server now, so need to get out of here */
          ircprot_freemsg(&msg);
          return 0;

        } else if (!irc_strcasecmp(msg.params[0], "STATUS")) {
          _ircclient_handle_status(p, msg);

        } else if (!irc_strcasecmp(msg.params[0], "HELP")) {
          /* User needs a little help */
          _ircclient_handle_help(p, msg);

        } else {
          /* Invalid command */
          ircclient_send_numeric(p, 421, "%s :Unknown DIRCPROXY command",
                                 msg.params[0]);

        }
      } else {
        ircclient_send_numeric(p, 461, ":Not enough parameters");
      }
    }
  }

  /* Do we have enough information to authenticate them? */
  if (!(p->client_status & IRC_CLIENT_AUTHED)
      && (p->client_status & IRC_CLIENT_GOTPASS)
      && (p->client_status & IRC_CLIENT_GOTNICK)
      && (p->client_status & IRC_CLIENT_GOTUSER))
  {
    _ircclient_authenticate(p, p->password);
    free(p->password);
    p->password = 0;
    p->client_status &= ~(IRC_CLIENT_GOTPASS);
  }

  /* Do we have enough information to connect to a server? */
  if (IS_CLIENT_READY(p) && !p->dead) {
    if (p->server_status != IRC_SERVER_ACTIVE) {
      if (!(p->server_status & IRC_SERVER_CREATED)) {
        if (p->conn_class && p->conn_class->server_autoconnect) {
          ircserver_connect(p);
        } else {
          ircclient_send_notice(p, "Please send /DIRCPROXY JUMP "
                                "<hostname>[:[port][:[password]]] to choose a "
                                "server");

          /* This won't delete an existing timer */
          timer_new((void *)p, "client_connect", g.connect_timeout,
                    TIMER_FUNCTION(_ircclient_timedout), (void *)1);
        }
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
        const char *ip;
        char buf[40];

        ip = net_ntop(&p->client_addr, buf, sizeof(buf));

        m = cc->masklist;
        while (m) {
          if (strcasematch(ip, m->str) || strcasematch(p->client_host, m->str))
            break;

          m = m->next;
        }

        /* We got a matching masklist, so this one's ok */
        if (m)
          break;
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
      net_hook(tmp_p->client_sock, SOCK_NORMAL, (void *)tmp_p,
               ACTIVITY_FUNCTION(_ircclient_data),
               ERROR_FUNCTION(_ircclient_error));

      /* If the connecting client doesn't agree with the proxy about its
         nickname, then correct it. */
      if (strcmp(p->nickname, tmp_p->nickname))
        ircclient_send_selfcmd(p, "NICK", ":%s", tmp_p->nickname);

      /* If we've got to restore a different nickname, then do that now */
      if (tmp_p->oldnickname && strcmp(tmp_p->oldnickname, tmp_p->nickname))
        ircclient_change_nick(tmp_p, tmp_p->oldnickname);

      /* We don't need this anymore */
      free(tmp_p->oldnickname);
      tmp_p->oldnickname = 0;

      /* Notify nickserv */
      if (tmp_p->conn_class->nickserv_password)
	 ircserver_send_command(tmp_p, "PRIVMSG", " %s :IDENTIFY %s", "NICKSERV",tmp_p->conn_class->nickserv_password);

      /* Unset any away message if we set one */
      if (!tmp_p->awaymessage && (tmp_p->server_status == IRC_SERVER_ACTIVE)
          && tmp_p->conn_class->away_message)
        ircserver_send_command(tmp_p, "AWAY", "");

      /* Rejoin any channels we parted */
      if ((tmp_p->server_status == IRC_SERVER_ACTIVE) && tmp_p->channels) {
        struct ircchannel *c;

        c = tmp_p->channels;
        while (c) {
          if (c->unjoined) {
            if (c->key) {
              ircserver_send_command(tmp_p, "JOIN", "%s :%s", c->name, c->key);
            } else {
              ircserver_send_command(tmp_p, "JOIN", ":%s", c->name);
            }
          }

          c = c->next;
        }
      }

      /* Send attach message to all channels we're on */
      if (tmp_p->server_status == IRC_SERVER_ACTIVE) {
        if (tmp_p->conn_class->attach_message) {
          struct ircchannel *c;
          int slashme;
          char *msg;

          msg = tmp_p->conn_class->attach_message;
          if ((strlen(msg) >= 5) && !strncasecmp(msg, "/me ", 4)) {
            /* Starts with /me */
            slashme = 1;
            msg += 4;
          } else {
            slashme = 0;
          }

          c = tmp_p->channels;
          while (c) {
            if (!c->inactive) {
              if (slashme) {
                ircserver_send_command(tmp_p, "PRIVMSG",
                                       "%s :\001ACTION %s\001", c->name, msg);
              } else {
                ircserver_send_command(tmp_p, "PRIVMSG", "%s :%s",
                                       c->name, msg);
              }
            }
            c = c->next;
          }
        }
      }

      if ((tmp_p->server_status == IRC_SERVER_ACTIVE)
          && !(tmp_p->client_status & IRC_CLIENT_SENTWELCOME))
        ircclient_welcome(tmp_p);

      p->client_status = IRC_CLIENT_NONE;
      p->client_sock = -1;
      p->dead = 1;

    } else {
      struct strlist *s;

      p->conn_class = cc;
      p->client_status |= IRC_CLIENT_AUTHED;
      time(&(p->start));

      if (p->conn_class->disconnect_on_detach)
        p->die_on_close = 1;

      /* Okay, they've authed for the first time, make the log directory
         here */
      if (p->conn_class->chan_log_enabled
          || p->conn_class->private_log_enabled
          || p->conn_class->server_log_enabled) {
        if (irclog_maketempdir(p))
          ircclient_send_notice(p, "(warning) Unable to create log "
                                   "directory, logging disabled");
      }

      /* Initialise the private message log */
      irclog_init(p, "");

      /* Open a log file if we're always logging */
      if (p->conn_class->private_log_enabled
          && p->conn_class->private_log_always) {
        if (irclog_open(p, ""))
          ircclient_send_notice(p, "(warning) Unable to log private messages");
      }

      /* Initialise the server message log */
      irclog_init(p, 0);

      /* Open a log file if we're always logging */
      if (p->conn_class->server_log_enabled
          && p->conn_class->server_log_always) {
        if (irclog_open(p, 0))
          ircclient_send_notice(p, "(warning) Unable to log server messages");
      }

      /* Join initial channels */
      s = p->conn_class->channels;
      while (s) {
        struct ircchannel *c;
        char *name, *key;

        name = x_strdup(s->str);
        key = strchr(name, ' ');
        if (key)
          *(key++) = 0;

        ircnet_addchannel(p, name);
        c = ircnet_fetchchannel(p, name);
        if (c) {
          c->inactive = 1;
          if (key)
            c->key = x_strdup(key);
        }

        free(name);

        s = s->next;
      }

      /* Set initial modes */
      if (p->conn_class->initial_modes)
        ircclient_change_mode(p, p->conn_class->initial_modes);

      /* Notify nickserv */
      // currently broken i will look in next revision
      /* if (p->conn_class->nickserv_password) */
      /* ircserver_send_command(p, "PRIVMSG", " %s :IDENTIFY %s", "NICKSERV",p->conn_class->nickserv_password); */
    }

    return 0;
  }

  ircclient_send_numeric(p, 464, ":You are not permitted to use this proxy");
  ircclient_send_error(p, "Permission Denied");
  ircclient_close(p);
  return -1;
}

/* Request a nickname change */
int ircclient_change_nick(struct ircproxy *p, const char *newnick) {
  /* If a server is ready to accept a NICK command, send it */
  if (IS_SERVER_READY(p)) {
    debug("Requesting nick change from '%s' to '%s'",
          (p->nickname ? p->nickname : ""), newnick);
    ircserver_send_command(p, "NICK", ":%s", newnick);
  }

  /* If we have a nickname already then the server will confirm that, otherwise
     we should remember it ourselves */
  if (p->client_status & IRC_CLIENT_GOTNICK) {
    debug("Server will change it for us");
    p->expecting_nick = 1;

    return 0;
  } else {
    int ret;

    /* Because we're not expecting a server confirmation, then we better
       do the confirm for the client ourselves */
    if ((p->client_status & IRC_CLIENT_CONNECTED) &&
        (p->client_status & IRC_CLIENT_AUTHED))
      ircclient_send_selfcmd(p, "NICK", ":%s", newnick);

    /* Make the change in the wings */
    ret = ircclient_nick_changed(p, newnick);
    ircclient_setnickname(p);
    ircclient_checknickname(p);

    return ret;
  }
}

/* Nickname has now definitly been changed */
int ircclient_nick_changed(struct ircproxy *p, const char *newnick) {
  if (p->nickname)
    debug("nickname WAS '%s'", p->nickname);
  free(p->nickname);

  p->nickname = x_strdup(newnick);
  p->client_status |= IRC_CLIENT_GOTNICK;
  debug("nickname NOW '%s'", p->nickname);

  return 0;
}

/* Make the current nickname the set one */
int ircclient_setnickname(struct ircproxy *p) {
  /* Update setnickname too */
  if (p->setnickname)
    free(p->setnickname);
  p->setnickname = x_strdup(p->nickname);
  debug("Changed setnickname to '%s'", p->setnickname);

  return 0;
}

/* Check whether we need to restore the nickname later */
int ircclient_checknickname(struct ircproxy *p) {
  if (p->conn_class && p->conn_class->nick_keep
      && strcmp(p->nickname, p->setnickname))
    timer_new((void *)p, "client_resetnick", NICK_GUARD_TIME,
              TIMER_FUNCTION(_ircclient_resetnick), (void *)0);

  return 0;
}

/* Change the nickname to something we generate ourselves */
int ircclient_generate_nick(struct ircproxy *p, const char *tried) {
  char *c, *nick;
  int ret;

  c = nick = (char *)malloc(strlen(tried) + 2);
  strcpy(nick, tried);
  c += strlen(nick) - 1;

  /* We add -'s until we can't, then we move back through them cycling them
     0..9 then finally _ until the whole nickname is _________.  Once that
     happens we just use 'dircproxy' and do it all over again */
  if (strlen(nick) < 9) {
    *(++c) = '-';
    *(++c) = 0;
  } else {
    while (c >= nick) {
      if (*c == '-') {
        *c = '0';
        break;
      } else if ((*c >= '0') && (*c < '9')) {
        (*c)++;
        break;
      } else if (*c == '9') {
        *c = '_';
        break;
      } else if (*c == '_') {
        c--;
      } else {
        *c = '-';
        break;
      }
    }

    if (c < nick) {
      free(nick);
      nick = x_strdup(FALLBACK_NICKNAME);
    }
  }

  /* Ask the server to change the nickname */
  if (IS_SERVER_READY(p)) {
    debug("Requesting nick change from '%s' to '%s'",
          (p->nickname ? p->nickname : ""), nick);
    ircserver_send_command(p, "NICK", ":%s", nick);
  }

  /* If we don't have a nickname yet, make the change ourselves */
  if (!(p->client_status & IRC_CLIENT_GOTNICK)) {
    /* We know that there is no client connected, otherwise this would
       never have been called, so no point sending a nickname to the client.

       Just change it in our memory */
    ret = ircclient_nick_changed(p, nick);
    ircclient_checknickname(p);
  } else {
    debug("Server will change it for us");
  }

  free(nick);
  return 0;
}

/* Timer hook to restore a lost nickname */
static void _ircclient_resetnick(struct ircproxy *p, void *data) {
  /* We don't have a server anymore, setnickname will be restored on
     connection attempt */
  if (!IS_SERVER_READY(p))
    return;

  /* Is it worth doing this? */
  if (!strcmp(p->nickname, p->setnickname))
    return;

  /* Ask the server to change the nickname */
  debug("Attempting to restore nickname to '%s'", p->setnickname);
  ircclient_change_nick(p, p->setnickname);
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
        } else if (p->modes) {
          char *pos;

          pos = strchr(p->modes, *ptr);
          if (pos) {
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
  timer_del((void *)p, "client_auth");
  timer_del((void *)p, "client_connect");

  net_close(&(p->client_sock));
  p->client_sock = -1;
  p->client_status &= ~(IRC_CLIENT_CONNECTED | IRC_CLIENT_AUTHED
                        | IRC_CLIENT_SENTWELCOME);

  /* No connection class, or no nick or user? Die! */
  if (!p->conn_class || !(p->client_status & IRC_CLIENT_GOTNICK)
      || !(p->client_status & IRC_CLIENT_GOTUSER)) {
    if (p->server_status & IRC_SERVER_CREATED) {
      ircserver_send_command(p, "QUIT", ":I shouldn't really be here - %s %s",
                             PACKAGE, VERSION);
      ircserver_close_sock(p);
    }
    p->dead = 1;
  }

  return p->dead;
}

/* send message of the day to the user */
static int _ircclient_motd(struct ircproxy *p) {
  FILE *motd_file;

  if (p->conn_class->motd_file) {
    motd_file = fopen(p->conn_class->motd_file, "r");
    if (!motd_file)
      syscall_fail("fopen", p->conn_class->motd_file, 0);
  } else {
    motd_file = (FILE *)0;
  }

  /* Check whether to do anything, and send appropriate numerics */
  if (!p->conn_class->motd_logo && !p->conn_class->motd_stats && !motd_file) {
    if (p->conn_class->motd_file) {
      ircclient_send_numeric(p, 422, ":MOTD File is missing");
    } else {
      ircclient_send_numeric(p, 422, ":No MOTD");
    }
    return 0;
  } else {
    ircclient_send_numeric(p, 375, ":- %s Message of the Day -", PACKAGE);
  }

  /* Send the pretty dircproxy logo */
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
    ircclient_send_numeric(p, 372, ":-");
    free(ver);
  }

  /* Send from file */
  if (motd_file) {
    char buff[512];

    while (fgets(buff, 512, motd_file)) {
      char *ptr;

      ptr = buff + strlen(buff);
      while ((ptr >= buff) && (!ptr || strchr(" \t\r\n", *ptr))) *(ptr--) = 0;
      ircclient_send_numeric(p, 372, ":- %s", buff);
    }

    ircclient_send_numeric(p, 372, ":-");
  }

  /* Send some stats */
  if (p->conn_class->motd_stats) {
    /* Private messages */
    if (p->private_log.filename) {
      char *s;

      if (p->conn_class->private_log_recall == -1) {
        s = x_strdup(p->private_log.nlines ? "all" : "none");
      } else if (p->conn_class->private_log_recall == 0) {
        s = x_strdup("none");
      } else if (p->conn_class->private_log_recall == p->private_log.nlines) {
        s = x_strdup("all");
      } else {
        s = x_sprintf("%ld", p->conn_class->private_log_recall);
      }

      ircclient_send_numeric(p, 372, ":- %ld private message%s "
                             "(%s will be sent)", p->private_log.nlines,
                             (p->private_log.nlines == 1 ? "" : "s"), s);
      ircclient_send_numeric(p, 372, ":-");

      free(s);
    }

    /* Server messages */
    if (p->server_log.filename) {
      char *s;

      if (p->conn_class->server_log_recall == -1) {
        s = x_strdup(p->server_log.nlines ? "all" : "none");
      } else if (p->conn_class->server_log_recall == 0) {
        s = x_strdup("none");
      } else if (p->conn_class->server_log_recall == p->server_log.nlines) {
        s = x_strdup("all");
      } else {
        s = x_sprintf("%ld", p->conn_class->server_log_recall);
      }

      ircclient_send_numeric(p, 372, ":- %ld server message%s "
                             "(%s will be sent)", p->server_log.nlines,
                             (p->server_log.nlines == 1 ? "" : "s"), s);
      ircclient_send_numeric(p, 372, ":-");

      free(s);
    }


    /* Channels they were on */
    if (p->channels) {
      struct ircchannel *c;

      c = p->channels;
      while (c) {
        if (c->inactive) {
          if (c->log.nlines) {
            ircclient_send_numeric(p, 372, ":- was on %s but removed by force",
                                   c->name);
          } else {
            ircclient_send_numeric(p, 372, ":- yet to join %s", c->name);
          }
        } else if (c->unjoined) {
          ircclient_send_numeric(p, 372, ":- was on %s, yet to rejoin",
                                 c->name);
        } else if (c->log.filename) {
          char *s;

          if (p->conn_class->chan_log_recall == -1) {
            s = x_strdup(c->log.nlines ? "all" : "none");
          } else if (p->conn_class->chan_log_recall == 0) {
            s = x_strdup("none");
          } else if (p->conn_class->chan_log_recall == c->log.nlines) {
            s = x_strdup("all");
          } else {
            s = x_sprintf("%ld", MIN(c->log.nlines,
                                     p->conn_class->chan_log_recall));
          }

          ircclient_send_numeric(p, 372, ":- %s. %ld line%s logged. "
                                 "(%s will be sent)", c->name, c->log.nlines,
                                 (c->log.nlines == 1 ? "" : "s"), s);

          free(s);
        } else {
          ircclient_send_numeric(p, 372, ":- %s (not logged)", c->name);
        }
        c = c->next;
      }
      ircclient_send_numeric(p, 372, ":-");
    }
  }

  /* Done */
  ircclient_send_numeric(p, 376, ":End of /MOTD command");
  if (motd_file)
    fclose(motd_file);

  return 1;
}

/* send welcome headers to the user */
int ircclient_welcome(struct ircproxy *p) {
  char tbuf[40];

  strftime(tbuf, sizeof(tbuf), START_TIMEDATE_FORMAT, localtime(&(p->start)));

  ircclient_send_numeric(p, 1, ":Welcome to the Internet Relay Network %s",
                         p->nickname);
  ircclient_send_numeric(p, 2, ":Your host is %s running %s via %s %s",
                         p->servername,
                         (p->serverver ? p->serverver : "(unknown)"),
                         PACKAGE, VERSION);
  ircclient_send_numeric(p, 3, ":This proxy has been running since %s", tbuf);
  if (p->serverver)
    ircclient_send_numeric(p, 4, "%s %s %s %s",
                           p->servername, p->serverver,
                           p->serverumodes, p->servercmodes);
  struct strlist *s = p->serversupported;
  while (s) {
    ircclient_send_numeric(p, 5, "%s", s->str);
    s = s->next;
  }

  _ircclient_motd(p);

  if (p->modes)
    ircclient_send_selfcmd(p, "MODE", "%s +%s", p->nickname, p->modes);

  if (p->awaymessage) {
    /* Ack.  There's no reason for a client to expect AWAY from a server,
       so we cheat and send a 306, reminding them what their away message
       was in the text.  This might not trick the client either, but hey,
       I can't do anything about that. */
    ircclient_send_numeric(p, 306, ":%s: %s",
                           "You left yourself away.  Your message was",
                           p->awaymessage);
  }

  /* Recall other log file */
  if (p->conn_class->server_log_enabled) {
    irclog_autorecall(p, 0);
    if (!p->conn_class->server_log_always)
      irclog_close(p, 0);
  }

  /* Recall channel log files, and get channel topic and members from server */
  if (p->channels) {
    struct ircchannel *c;

    c = p->channels;
    while (c) {
      if (!c->inactive && !c->unjoined) {
        ircclient_send_selfcmd(p, "JOIN", ":%s", c->name);
        ircserver_send_command(p, "TOPIC", ":%s", c->name);
        ircserver_send_command(p, "NAMES", ":%s", c->name);

        if (p->conn_class->chan_log_enabled) {
          irclog_autorecall(p, c->name);
          if (!p->conn_class->chan_log_always)
            irclog_close(p, c->name);
        }
      }

      c = c->next;
    }
  }

  /* Recall private log file */
  if (p->conn_class->private_log_enabled) {
    irclog_autorecall(p, p->nickname);
    if (!p->conn_class->private_log_always)
      irclog_close(p, p->nickname);
  }

  irclog_log(p, IRC_LOG_CLIENT, IRC_LOGFILE_ALL, PACKAGE, "You connected");
  ircnet_announce_status(p);

  p->client_status |= IRC_CLIENT_SENTWELCOME;
  return 0;
}

/* Timer hook when something's timed out */
static void _ircclient_timedout(struct ircproxy *p, void *data) {
  int connect;

  /* These are always called after the timeout if the client's still
     connected, check the event they were looking for has happened */
  connect = (int)data;
  if (connect && (p->server_status & IRC_SERVER_CREATED)) {
    /* Connecting to server, and a socket has been created to do it */
    debug("Server has been chosen");
    return;
  } else if (!connect && IS_CLIENT_READY(p)) {
    /* Authorization, and client is ready to accept data */
    debug("They are authorized");
    return;
  }

  /* Timeout! */
  debug("Timed out");
  ircclient_send_error(p, "%s Timeout", (connect ? "Connect" : "Login"));
  ircclient_close(p);
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

  ret = net_send(p->client_sock, ":%s %03d %s %s\r\n",
                 (p->servername ? p->servername : PACKAGE), numeric,
                 (p->nickname ? p->nickname : "*"), msg);
  debug("<- ':%s %03d %s %s'", (p->servername ? p->servername : PACKAGE),
        numeric, (p->nickname ? p->nickname : "*"), msg);

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

  ret = net_send(p->client_sock, ":%s %s %s :%s\r\n", PACKAGE, "NOTICE",
                 (p->nickname ? p->nickname : "AUTH"), msg);
  debug("<- ':%s %s %s :%s'", PACKAGE, "NOTICE",
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

  ret = net_send(p->client_sock, ":%s %s %s :%s\r\n",
                 (p->servername ? p->servername : PACKAGE), "NOTICE",
                 channel, msg);
  debug("<- ':%s %s %s :%s'", (p->servername ? p->servername : PACKAGE),
        "NOTICE", channel, msg);

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

  ret = net_send(p->client_sock, ":%s %s %s\r\n",
                 (p->servername ? p->servername : PACKAGE), command, msg);
  debug("<- ':%s %s %s'", (p->servername ? p->servername : PACKAGE),
        command, msg);

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

  ret = net_send(p->client_sock, "%s%s %s\r\n", prefix, command, msg);
  debug("<- '%s%s %s'", prefix, command, msg);

  free(prefix);
  free(msg);
  return ret;
}

/* send an error to the user */
int ircclient_send_error(struct ircproxy *p, const char *format, ...) {
  char *msg, *nick, *user, *host;
  va_list ap;
  int ret;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  nick = p->nickname ? p->nickname : "";
  user = p->username ? p->username : "user";
  host = p->hostname ? p->hostname : "host";

  ret = net_send(p->client_sock, "%s :%s: %s[%s@%s] (%s)\r\n",
                 "ERROR", "Closing Link", nick, user, host, msg);
  debug("<- '%s :%s: %s[%s@%s] (%s)'", "ERROR", "Closing Link",
        nick, user, host, msg);

  free(msg);
  return ret;
}

/* Send a DCC reject message */
static int _ircclient_send_dccreject(struct ircproxy *p, const char *msg,
                                     const char *reason) {
  int ret = 1;

  if (p && p->conn_class && p->conn_class->dcc_proxy_sendreject &&
      (p->client_status == IRC_CLIENT_ACTIVE)) {
    if (reason) {
      ret = net_send(p->client_sock, "%s (%s: %s)\001\r\n", msg,
                     PACKAGE, reason);
      debug("<- '%s (%s: %s)\001'", msg, PACKAGE, reason);
    } else {
      ret = net_send(p->client_sock, "%s\001\r\n", msg);
      debug("<- '%s\001'", msg);
    }
  }

  return ret;
}

  /* /DIRCPROXY STATUS handler */
void _ircclient_handle_status(struct ircproxy *p, struct ircmessage msg) {
  struct ircchannel *c;
  struct strlist *s;

  ircclient_send_notice(p, "%s %s status:", PACKAGE, VERSION);
  ircclient_send_notice(p, "- Nickname on server: %s", p->nickname);
  ircclient_send_notice(p, "- Nickname to guard: %s", p->setnickname);
  ircclient_send_notice(p, "- Username for server: %s", p->username);
  ircclient_send_notice(p, "- Hostname for server: %s", p->hostname);
  ircclient_send_notice(p, "- Real name for server: %s", p->realname);
  ircclient_send_notice(p, "-");

  ircclient_send_notice(p, "- Client status: %s",
                        IS_CLIENT_READY(p) ? "Ready" : "");
  if (p->client_status != IRC_CLIENT_ACTIVE) {
    if (p->client_status & IRC_CLIENT_CONNECTED)
      ircclient_send_notice(p, "-   Connected");
    if (p->client_status & IRC_CLIENT_GOTPASS)
      ircclient_send_notice(p, "-   Received password");
    if (p->client_status & IRC_CLIENT_GOTNICK)
      ircclient_send_notice(p, "-   Received nickname");
    if (p->client_status & IRC_CLIENT_GOTUSER)
      ircclient_send_notice(p, "-   Received user information");
    if (p->client_status & IRC_CLIENT_AUTHED)
      ircclient_send_notice(p, "-   Authorised");
    if (p->client_status & IRC_CLIENT_SENTWELCOME)
      ircclient_send_notice(p, "-   Welcomed");
  }
  ircclient_send_notice(p, "-");

  ircclient_send_notice(p, "- Server status: %s",
                        IS_SERVER_READY(p) ? "Ready" : "");
  if (p->server_status != IRC_SERVER_ACTIVE) {
    if (p->server_status & IRC_SERVER_CREATED)
      ircclient_send_notice(p, "-   Created");
    if (p->server_status & IRC_SERVER_SEEN)
      ircclient_send_notice(p, "-   Seen");
    if (p->server_status & IRC_SERVER_CONNECTED)
      ircclient_send_notice(p, "-   Connected");
    if (p->server_status & IRC_SERVER_INTRODUCED)
      ircclient_send_notice(p, "-   Introduced ourselves");
    if (p->server_status & IRC_SERVER_GOTWELCOME)
      ircclient_send_notice(p, "-   Have been welcomed");
  }
  ircclient_send_notice(p, "-");

  ircclient_send_notice(p,  "- Servers.  Current marked by '->'");
  s = p->conn_class->servers;
  while (s) {
    ircclient_send_notice(p, "-%s  %s",
                          (s == p->conn_class->next_server ? ">" : " "),
                          s->str);
    s = s->next;
  }
  ircclient_send_notice(p, "-");

  ircclient_send_notice(p, "- Channels");
  c = p->channels;
  while (c) {
    ircclient_send_notice(p, "-   %s%s%s%s%s%s",
                          c->name,
                          c->key ? " (key: " : "",
                          c->key ? c->key : "",
                          c->key ? ")" : "",
                          c->inactive ? " (removed by force)" : "",
                          c->unjoined ? " (left on detach)" : "");
    c = c->next;
  }
  ircclient_send_notice(p, "-");

  ircclient_send_notice(p, "- Advanced:");
  ircclient_send_notice(p, "-   Allow MOTD count: %d", p->allow_motd);
  ircclient_send_notice(p, "-   Allow PONG count: %d", p->allow_pong);
  ircclient_send_notice(p, "-   411 Squelch count: %d", p->squelch_411);
  ircclient_send_notice(p, "-   Expecting NICK count: %d",
                        p->expecting_nick);

  if (p->squelch_modes)
    ircclient_send_notice(p, "-   Squelching mode changes:");
  s = p->squelch_modes;
  while (s) {
    ircclient_send_notice(p, "-     %s", s->str);
    s = s->next;
  }
}

  /* /DIRCPROXY JUMP handler */
int _ircclient_handle_jump(struct ircproxy *p, struct ircmessage msg) {
  struct strlist *server;

  /* User wants to jump to a new server */
  if (msg.numparams >= 2) {
    struct strlist *s;
    int i;

    /* Check the server list to see whether its a plain jump */
    server = 0;
    s = p->conn_class->servers;
    i = 0;
    while (s) {
      if ((atoi(msg.params[1]) == ++i)
          || !irc_strcasecmp(msg.params[1], s->str)) {
        server = s;
        break;
      }

      s = s->next;
    }

  } else {
    /* User wants to jump to the next server */
    server = 0;
    if (p->conn_class->next_server)
      server = p->conn_class->next_server->next;
    if (!server)
      server = p->conn_class->servers;
  }

  /* Allocate new server if jump_new */
  if (!server && p->conn_class->allow_jump_new
      && (msg.numparams >= 2)) {
    debug("New server");

    server = (struct strlist *)malloc(sizeof(struct strlist));
    server->str = x_strdup(msg.params[1]);
    server->next = 0;

    if (p->conn_class->servers) {
      struct strlist *ss;

      ss = p->conn_class->servers;
      while (ss->next)
        ss = ss->next;

      ss->next = server;
    } else {
      p->conn_class->servers = server;
    }

  } else if (!server) {
    ircclient_send_numeric(p, 402, "No such server, "
                           "use /DIRCPROXY SERVERS to see them");
  }

  if (server) {
    debug("Jumping to %s", server->str);

    p->conn_class->next_server = server;
    ircserver_connectagain(p);

    /* We have no server now, so need to get out of here */
    ircprot_freemsg(&msg);
    return 1;
  } else {
    return 0;
  }
}

  /* /DIRCPROXY KILL handler */
void _ircclient_handle_kill(struct ircproxy *p, struct ircmessage msg) {
  struct ircproxy *proxy;

  /* User wants to kill a user */
  if (msg.numparams >= 2) {
    struct ircconnclass *c;
    struct ircproxy *cp;
    int i;

    /* Check the user list */
    proxy = 0;
    c = connclasses;
    i = 0;
    while (c) {
      cp = ircnet_fetchclass(c);
      if (!cp) {
        c = c->next;
        continue;
      }

      if ((atoi(msg.params[1]) == ++i)
          || (cp->client_host
              && !irc_strcasecmp(msg.params[1], cp->client_host))
          || (cp->servername
              && !irc_strcasecmp(msg.params[1], cp->servername))
          || (cp->nickname
              && !irc_strcasecmp(msg.params[1], cp->nickname))) {
        proxy = cp;
        break;
      }

      c = c->next;
    }

    if (proxy && (proxy == p)) {
      ircclient_send_notice(p, "Use /DIRCPROXY QUIT to kill yourself");
    } else if (proxy) {
      if (IS_SERVER_READY(proxy)) {
        ircserver_send_command(proxy, "QUIT",
                               ":Killed by adminstrator - %s %s",
                               PACKAGE, VERSION);
      }
      if (IS_CLIENT_READY(proxy)) {
        ircclient_send_error(proxy, "Killed by administrator");
      }

      ircserver_close_sock(proxy);
      proxy->conn_class = 0;
      ircclient_close(proxy);

    } else {
      ircclient_send_numeric(p, 401, "No such user, "
                             "use /DIRCPROXY USERS to see them");
    }

  } else {
    ircclient_send_numeric(p, 461, ":Not enough parameters");
  }
}

  /* /DIRCPROXY NOTIFY handler */
void _ircclient_handle_notify(struct ircproxy *p, struct ircmessage msg) 
{   
   struct ircproxy *proxy;
   /* User wants to kill a user */
   if (msg.numparams >= 2) { 
      struct ircconnclass *c;
      struct ircproxy *cp;
      int i;
        
      /* Check the user list */
      proxy = 0;
      c = connclasses;
      i = 0;
      while (c)  {  
         cp = ircnet_fetchclass(c);
         if (!cp) {
            c = c->next;
            continue;
         }
         if ((atoi(msg.params[1]) == ++i)
             || (cp->client_host
                 && !irc_strcasecmp(msg.params[1], cp->client_host))
             || (cp->servername
                 && !irc_strcasecmp(msg.params[1], cp->servername))
             || (cp->nickname
                 && !irc_strcasecmp(msg.params[1], cp->nickname))) {
            proxy = cp;
            break;
         }
         c = c->next;
      }
      
      if (proxy) {
         net_send(proxy->client_sock, ":dircproxy!dircproxy@localhost NOTICE %s :%s\r\n",cp->nickname, msg.paramstarts[2]);
      } else {
         ircclient_send_numeric(p, 401, "No such user, use /DIRCPROXY USERS to see them");
      }
   }
	 
}
   
   
  /* /DIRCPROXY USERS handler */
void _ircclient_handle_users(struct ircproxy *p, struct ircmessage msg) {
  struct ircconnclass *c;
  struct ircproxy *cp;
  int i;

  c = connclasses;
  i = 0;

  ircclient_send_notice(p, "Connection classes:");

  while (c) {
    cp = ircnet_fetchclass(c);
    if (!cp) {
      c = c->next;
      continue;
    }

    ircclient_send_notice(p, "-%s %2d. %s -> %s (%s)",
                          (cp == p ? ">" : " "), ++i,
                          cp->client_host ? cp->client_host : "(none)",
                          cp->servername ? cp->servername : "(none)",
                          cp->nickname ? cp->nickname : "no nickname");
    c = c->next;
  }
}

  /* /DIRCPROXY RECALL handler */
void _ircclient_handle_recall(struct ircproxy *p, struct ircmessage msg) {
  char *src, *filter;
  long start, lines;

  /* User wants to recall stuff from log files */
  src = filter = 0;
  start = -1;
  lines = 0;

  if (msg.numparams >= 4) {
    src = msg.params[1];
    start = atol(msg.params[2]);
    lines = atol(msg.params[3]);
  } else if (msg.numparams >= 3) {
    if (!irc_strcasecmp(msg.params[2], "ALL")) {
      src = msg.params[1];
      lines = -1;
    } else if (strspn(msg.params[1], "0123456789")
               == strlen(msg.params[1])) {
      start = atol(msg.params[1]);
      lines = atol(msg.params[2]);
    } else {
      src = msg.params[1];
      lines = atol(msg.params[2]);
    }
  } else if (msg.numparams >= 2) {
    if (!irc_strcasecmp(msg.params[1], "ALL")) {
      lines = -1;
    } else {
      lines = atol(msg.params[1]);
    }
  } else {
    ircclient_send_numeric(p, 461, ":Not enough parameters");
  }

  if (src && !irc_strcasecmp(src, "SERVER")) {
    src = 0;
  } else if (src) {
    struct ircchannel *c;

    c = ircnet_fetchchannel(p, src);
    if (!c) {
      filter = src;
      src = p->nickname;
    }
  } else {
    src = p->nickname;
  }

  irclog_recall(p, src, start, lines, filter);
}

  /* PRIVMSG handler */
int _ircclient_handle_privmsg(struct ircproxy *p, struct ircmessage msg) {
   int squelch = 0;

  if (msg.numparams >= 2) {
    struct strlist *list, *s;
    char *str;

    ircprot_stripctcp(msg.params[1], &str, &list);

    /* Privmsgs from us get logged */
    if (str && strlen(str)) {
      char *tmp;

      tmp = x_sprintf("%s!%s@%s", p->nickname, p->username,
                      p->hostname);
      irclog_log(p, IRC_LOG_MSG, msg.params[0], tmp, "%s", str);
      free(tmp);
    }
    free(str);

    /* Handle CTCP */
    str = x_strdup(msg.params[1]);
    s = list;
    while (s) {
      struct ctcpmessage cmsg;
      struct strlist *n;
      char *unquoted;
      int r;

      n = s->next;
      r = ircprot_parsectcp(s->str, &cmsg);
      unquoted = s->str;
      free(s);
      s = n;
      if (r == -1) {
        free(unquoted);
        continue;
      }

      if (!strcmp(cmsg.cmd, "ACTION")) {
        char *tmp;

        tmp = x_sprintf("%s!%s@%s", p->nickname, p->username,
                        p->hostname);
	if (cmsg.paramstarts)	   
	   irclog_log(p, IRC_LOG_ACTION, msg.params[0], tmp, "%s", cmsg.paramstarts[0]);
	else 
	   irclog_log(p, IRC_LOG_ACTION, msg.params[0], tmp, NULL);	 
        free(tmp);

      } else if (!strcmp(cmsg.cmd, "DCC")
                 && p->conn_class->dcc_proxy_outgoing) {
        struct sockaddr_in vis_addr;
        int len;

        /* We need our local address to do anything DCC related */
        len = sizeof(struct sockaddr_in);
        if (getsockname(p->server_sock, (struct sockaddr *)&vis_addr,
                         &len)) {
          syscall_fail("getsockname", "", 0);

        } else if ((cmsg.numparams >= 4)
                   && (!irc_strcasecmp(cmsg.params[0], "CHAT")
                       || !irc_strcasecmp(cmsg.params[0], "SEND"))) {
          char *tmp, *ptr, *dccmsg, *rejmsg;
          struct in_addr l_addr, r_addr;
          int l_port, r_port, t_port;
          char *rest = 0;
          int type = 0;

          /* Find out what type of DCC request this is */
          if (!irc_strcasecmp(cmsg.params[0], "CHAT")) {
            type = DCC_CHAT;
          } else if (!irc_strcasecmp(cmsg.params[0], "SEND")) {
            if (p->conn_class->dcc_send_fast) {
              type = DCC_SEND_FAST;
            } else {
              type = DCC_SEND_SIMPLE;
            }
          }

          /* Check whether there's a tunnel port */
          t_port = 0;
          if (p->conn_class->dcc_tunnel_outgoing)
            t_port = dns_portfromserv(p->conn_class->dcc_tunnel_outgoing);

          /* Eww, host order, how the hell does this even work
             between machines of a different byte order? */
          if (!t_port) {
            r_addr.s_addr = strtoul(cmsg.params[2], (char **)NULL, 10);
            r_port = atoi(cmsg.params[3]);
          } else {
            r_addr.s_addr = INADDR_LOOPBACK;
            r_port = ntohs(t_port);
          }
          l_addr.s_addr = ntohl(vis_addr.sin_addr.s_addr);
          if (cmsg.numparams >= 5)
            rest = cmsg.paramstarts[4];

          /* Strip out this CTCP from the message, replacing it in
             a moment with dccmsg */
          tmp = x_sprintf("\001%s\001", unquoted);
          ptr = strstr(str, tmp);
          dccmsg = 0;

          /* Save this in case we need it later */
          rejmsg = x_sprintf(":%s NOTICE %s :\001DCC REJECT %s %s",
                             msg.params[0], p->nickname,
                             cmsg.params[0], cmsg.params[1]);

          /* Set up a dcc proxy */
          if (ptr && !dccnet_new(type, p->conn_class->dcc_proxy_timeout,
                                 p->conn_class->dcc_proxy_ports,
                                 p->conn_class->dcc_proxy_ports_sz,
                                 &l_port, r_addr, r_port, 0, 0,
                                 DCCN_FUNCTION(_ircclient_send_dccreject),
                                 p, rejmsg, 0)) {
            char *me_tmp;

            dccmsg = x_sprintf("\001DCC %s %s %lu %u%s%s\001",
                               cmsg.params[0], cmsg.params[1],
                               l_addr.s_addr, l_port,
                               (rest ? " " : ""), (rest ? rest : ""));

            me_tmp = x_sprintf("%s!%s@%s", p->nickname, p->username,
                               p->hostname);
            irclog_log(p, IRC_LOG_CTCP, msg.params[0], me_tmp,
                       "Sent DCC %s Request", cmsg.params[0]);
            free(me_tmp);

          } else if (ptr) {
            dccmsg = x_strdup("");
            _ircclient_send_dccreject(p, rejmsg,
                                      "Couldn't establish proxy");
          }

          /* Don't need this now */
          free(rejmsg);

          /* Cut out the old CTCP and replace with dccmsg */
          if (ptr) {
            char *oldstr;

            *ptr = 0;
            ptr += strlen(tmp);

            oldstr = str;
            str = x_sprintf("%s%s%s", oldstr, dccmsg, ptr);

            free(oldstr);
            free(dccmsg);
          }

          free(tmp);

        } else {
          /* Unknown DCC */
	   if (cmsg.numparams > 0)
	     debug("Unknown or Unimplemented DCC request - %s", cmsg.params[0]);
        }

      } else {
        char *tmp;

        tmp = x_sprintf("%s!%s@%s", p->nickname, p->username,
                        p->hostname);
	if (cmsg.numparams > 0)
	   irclog_log(p, IRC_LOG_CTCP, msg.params[0], tmp, "Sent CTCP %s", cmsg.params[0]);
        free(tmp);
      }

      ircprot_freectcp(&cmsg);
      free(unquoted);
    }

    /* Send str */
    if (strlen(str))
      net_send(p->server_sock, ":%s PRIVMSG %s :%s\r\n",
               (msg.src.orig ? msg.src.orig : p->nickname),
               msg.params[0], str);
    squelch = 1;
    free(str);
  }

  if (p->conn_class->idle_maxtime)
    ircserver_resetidle(p);

  return squelch;
}

  /* /DIRCPROXY HELP handler */
void _ircclient_handle_help(struct ircproxy *p, struct ircmessage msg) {
  char **help_page;

  help_page = 0;

  if ((msg.numparams >= 2) && strlen(msg.params[1])) {
    if (!irc_strcasecmp(msg.params[1], "RECALL")) {
      help_page = command_help[I_HELP_RECALL];
    } else if (p->conn_class->allow_persist
               && !irc_strcasecmp(msg.params[1], "PERSIST")) {
      help_page = command_help[I_HELP_PERSIST];
    } else if (!irc_strcasecmp(msg.params[1], "RELOAD")) {
      help_page = command_help[I_HELP_RELOAD];
    } else if (!irc_strcasecmp(msg.params[1], "DETACH")) {
      help_page = command_help[I_HELP_DETACH];
    } else if (!irc_strcasecmp(msg.params[1], "QUIT")) {
      help_page = command_help[I_HELP_QUIT];
    } else if (!irc_strcasecmp(msg.params[1], "MOTD")) {
      help_page = command_help[I_HELP_MOTD];
    } else if (p->conn_class->allow_die
               && !irc_strcasecmp(msg.params[1], "DIE")) {
      help_page = command_help[I_HELP_DIE];
    } else if (!irc_strcasecmp(msg.params[1], "SERVERS")) {
      help_page = command_help[I_HELP_SERVERS];
    } else if (p->conn_class->allow_jump
               && !irc_strcasecmp(msg.params[1], "JUMP")) {
      help_page = (p->conn_class->allow_jump_new
                   ? command_help[I_HELP_JUMP_NEW] : command_help[I_HELP_JUMP]);
    } else if (p->conn_class->allow_host
               && !irc_strcasecmp(msg.params[1], "HOST")) {
      help_page = command_help[I_HELP_HOST];
    } else if (!irc_strcasecmp(msg.params[1], "STATUS")) {
      help_page = command_help[I_HELP_STATUS];
    } else if (!irc_strcasecmp(msg.params[1], "USERS")) {
      help_page = command_help[I_HELP_USERS];
    } else if (!irc_strcasecmp(msg.params[1], "KILL")) {
      help_page = command_help[I_HELP_KILL];
    } else if (!irc_strcasecmp(msg.params[1], "NOTIFY")) {
      help_page = command_help[I_HELP_NOTIFY]; 
    } else if (!irc_strcasecmp(msg.params[1], "HELP")) {
      help_page = command_help[I_HELP_HELP];
    } else {
      help_page = command_help[I_HELP_INDEX];
    }

  } else {
    help_page = command_help[I_HELP_INDEX];
  }

  if (help_page) {
    int i;

    i = 0;
    ircclient_send_notice(p, "%s %s help", PACKAGE, VERSION);
    while (help_page[i]) {
      ircclient_send_notice(p, "- %s", help_page[i]);
      i++;
    }

    if (help_page == command_help[I_HELP_INDEX]) {
      ircclient_send_notice(p, "-     HELP      "
                           "(help on /dircproxy commands)");
      ircclient_send_notice(p, "-     MOTD      "
                           "(show dircproxy message of the day)");
      ircclient_send_notice(p, "-     STATUS    "
                            "(show dircproxy status information)");
      ircclient_send_notice(p, "-     RECALL    "
                            "(recall text from log files)");
      ircclient_send_notice(p, "-     GET    "
			    "(Get the value of a configuration item)");
      ircclient_send_notice(p, "-     SET    "
			     "(Set the value of a configuration item)");       
      ircclient_send_notice(p, "-     RELOAD    "
                            "(reload configuration file)");
      ircclient_send_notice(p, "-     DETACH    "
                            "(detach from dircproxy)");
      if (p->conn_class->allow_persist)
        ircclient_send_notice(p, "-     PERSIST   "
                              "(keep session after detach)");
      ircclient_send_notice(p, "-     QUIT      "
                            "(end dircproxy session)");
      if (p->conn_class->allow_die)
        ircclient_send_notice(p, "-     DIE       "
                              "(terminate dircproxy)");
      if (p->conn_class->allow_users)
        ircclient_send_notice(p, "-     USERS     "
                              "(show users using dircproxy)");
      if (p->conn_class->allow_kill)
        ircclient_send_notice(p, "-     KILL      "
                              "(terminate a user's session)");
      if (p->conn_class->allow_notify)
	ircclient_send_notice(p, "-     NOTIFY    "
			      "(send a notice to a user's session");
      ircclient_send_notice(p, "-     SERVERS   "
                            "(show servers list)");
      if (p->conn_class->allow_jump)
        ircclient_send_notice(p, "-     JUMP      "
                              "(jump to a different server)");
      if (p->conn_class->allow_host)
        ircclient_send_notice(p, "-     HOST      "
                              "(change your visible hostname)");

      i = 0;
      while (command_help[I_HELP_INDEX_END][i]) {
        ircclient_send_notice(p, "- %s", command_help[I_HELP_INDEX_END][i]);
        i++;
      }
    }

    ircclient_send_notice(p, "-");
  }
}
