/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_server.c
 *  - Handling of servers connected to the proxy
 * --
 * @(#) $Id: irc_server.c,v 1.22.2.1 2000/10/10 13:17:34 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdio.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <dircproxy.h>
#include "sprintf.h"
#include "sock.h"
#include "dns.h"
#include "timers.h"
#include "irc_log.h"
#include "irc_net.h"
#include "irc_prot.h"
#include "irc_string.h"
#include "irc_client.h"
#include "irc_server.h"

/* forward declarations */
static void _ircserver_reconnect(struct ircproxy *, void *);
static int _ircserver_gotmsg(struct ircproxy *, const char *);
static int _ircserver_close(struct ircproxy *);
static void _ircserver_ping(struct ircproxy *, void *);
static void _ircserver_stoned(struct ircproxy *, void *);
static int _ircserver_forclient(struct ircproxy *, struct ircmessage *);

/* hook for timer code to reconnect to a server */
static void _ircserver_reconnect(struct ircproxy *p, void *data) {
  debug("Reconnecting to server");

  /* Choose the next server.  If we have no more, choose the first again and
     increment attempts */
  p->conn_class->next_server = p->conn_class->next_server->next;
  if (!p->conn_class->next_server) {
    p->conn_class->next_server = p->conn_class->servers;
    p->server_attempts++;
  }

  if (p->conn_class->server_maxattempts
      && (p->server_attempts >= p->conn_class->server_maxattempts)) {
    /* If we go over maximum reattempts, then give up */
    if (IS_CLIENT_READY(p))
      ircclient_send_notice(p, "Giving up on servers.  Time to quit");

    p->conn_class = 0;
    if (p->client_status & IRC_CLIENT_CONNECTED)
      ircclient_close(p);
    debug("Giving up on servers, reattempted too much");
    p->dead = 1;
  } else if (!(p->server_status & IRC_SERVER_SEEN)
             && p->conn_class->server_maxinitattempts
             && (p->server_attempts >= p->conn_class->server_maxinitattempts)) {
    /* Otherwise check initial attempts if its our first time */
    if (IS_CLIENT_READY(p))
      ircclient_send_notice(p, "Giving up on servers.  Time to quit");
    
    p->conn_class = 0;
    if (p->client_status & IRC_CLIENT_CONNECTED)
      ircclient_close(p);
    debug("Giving up on servers, can't get initial connection");
    p->dead = 1;
  } else {
    /* Attempt a new connection */
    ircserver_connect(p);
  }
}

/* Called to initiate a connection to a server */
int ircserver_connect(struct ircproxy *p) {
  struct sockaddr_in local_addr;
  char *host, *server;
  int ret;

  debug("Connecting to server");

  if (timer_exists(p, "server_recon")) {
    debug("Connection already in progress");
    if (IS_CLIENT_READY(p))
      ircclient_send_notice(p, "Connection already in progress...");
    return 0;
  }

  server = x_strdup(p->conn_class->next_server->str);
  if (strchr(server, ':') != strrchr(server, ':')) {
    /* More than one :, second denotes password */
    char *pass;

    pass = strrchr(server, ':');
    *(pass++) = 0;

    if (strlen(pass))
      p->serverpassword = x_strdup(pass);
  }
  
  host = 0;
  if (dns_filladdr(server, p->conn_class->server_port, 1,
                   &(p->server_addr), &host)) {
    debug("DNS failure, retrying");
    free(server);
    timer_new(p, "server_recon", p->conn_class->server_dnsretry,
              _ircserver_reconnect, (void *)0);
    return -1;
  } else {
    free(server);
    free(p->servername);
    p->servername = host;
  }

  debug("Connecting to %s port %d", p->servername,
        ntohs(p->server_addr.sin_port));
  if (IS_CLIENT_READY(p))
    ircclient_send_notice(p, "Connecting to %s port %d",
                          p->servername, ntohs(p->server_addr.sin_port));

  p->server_sock = sock_make();
  if (p->server_sock == -1) {
    ret = -1;

  } else {
    if (p->conn_class->local_address) {
      host = 0;
      memset(&local_addr, 0, sizeof(struct sockaddr_in));
      local_addr.sin_family = AF_INET;
      local_addr.sin_addr.s_addr = INADDR_ANY;

      if (dns_addrfromhost(p->conn_class->local_address, &(local_addr.sin_addr),
                           &host)) {
        if (IS_CLIENT_READY(p))
          ircclient_send_notice(p, "(warning) Couldn't find address for %s",
                                p->conn_class->local_address);
      } else if (bind(p->server_sock, (struct sockaddr *)&local_addr,
                      sizeof(struct sockaddr_in))) {
        if (IS_CLIENT_READY(p))
          ircclient_send_notice(p, "(warning) Couldn't use local address %s",
                                host);
      } else {
        free(p->hostname);
        p->hostname = host;
      }
    }

    if (connect(p->server_sock, (struct sockaddr *)&(p->server_addr),
                sizeof(struct sockaddr_in))
        && (errno != EINPROGRESS)) {
      syscall_fail("connect", p->servername, 0);
      sock_close(p->server_sock);
      ret = -1;
    } else {
      ret = 0;
    }
  }

  if (ret) {
    if (IS_CLIENT_READY(p))
      ircclient_send_notice(p, "Connection failed: %s", strerror(errno));
    debug("Connection failed: %s", strerror(errno));

    sock_close(p->server_sock);
    timer_new(p, "server_recon", p->conn_class->server_retry,
              _ircserver_reconnect, (void *)0);
  } else {
    p->server_status |= IRC_SERVER_CREATED;
  }

  debug("Connected");
  return ret;
}

/* Called when a new server has connected */
int ircserver_connected(struct ircproxy *p) {
  char *username;

  debug("Connection succeeded");
  p->server_status |= IRC_SERVER_CONNECTED | IRC_SERVER_SEEN;
  p->server_attempts = 0;

  if (IS_CLIENT_READY(p))
    ircclient_send_notice(p, "Connected to server");

  /* Icky to put this here, but I don't know where else to put it.
     We do need this you see... */ 
  if (!p->hostname) {
    struct sockaddr_in sock_addr;
    int len;

    len = sizeof(struct sockaddr_in);
    if (!getsockname(p->server_sock, (struct sockaddr *)&sock_addr, &len)) {
      p->hostname = dns_hostfromaddr(sock_addr.sin_addr);
    } else {
      syscall_fail("getsockname", "", 0);
    }
  }

  username = ircprot_sanitize_username(p->username);
  if (p->serverpassword)
    ircserver_send_peercmd(p, "PASS", ":%s", p->serverpassword);
  ircserver_send_peercmd(p, "NICK", ":%s", p->nickname);
  ircserver_send_peercmd(p, "USER", "%s 0 * :%s", username, p->realname);
  free(username);

  /* Begin stoned server checking */
  if (p->conn_class->server_pingtimeout) {
    timer_new(p, "server_ping", (int)(p->conn_class->server_pingtimeout / 2),
              _ircserver_ping, (void *)0);
    timer_new(p, "server_stoned", p->conn_class->server_pingtimeout,
              _ircserver_stoned, (void *)0);
  }

  return 0;
}

/* Called when a connection fails */
int ircserver_connectfailed(struct ircproxy *p, int error) {
  debug("Connection failed");

  if (IS_CLIENT_READY(p))
    ircclient_send_notice(p, "Connection failed: %s", strerror(error));

  sock_close(p->server_sock);
  p->server_status &= ~(IRC_SERVER_CREATED);

  timer_new(p, "server_recon", p->conn_class->server_retry,
            _ircserver_reconnect, (void *)0);

  return 0;
}

/* Called when a server sends us stuff.  -1 = closed, 0 = done */
int ircserver_data(struct ircproxy *p) {
  char *str;
  int ret;

  str = 0;
  ret = sock_recv(p->server_sock, &str, "\r\n");

  switch (ret) {
    case SOCK_ERROR:
      debug("Socket error");

    case SOCK_CLOSED:
      debug("Server disconnected");
      _ircserver_close(p);

      return -1;

    case SOCK_EMPTY:
      free(str);
      return 0;
  }

  debug("<< '%s'", str);
  _ircserver_gotmsg(p, str);
  free(str);

  return 0;
}

/* Called when we get an irc protocol data from a server */
static int _ircserver_gotmsg(struct ircproxy *p, const char *str) {
  struct ircmessage msg;
  int squelch = 1;

  if (ircprot_parsemsg(str, &msg) == -1)
    return -1;

  /* 437 is bizarre, it either means Nickname is juped or Channel is juped */
  if (!strcasecmp(msg.cmd, "437")) {
    if (msg.numparams >= 2) {
      if (!irc_strcasecmp(p->nickname, msg.params[1])) {
        /* Our nickname is Juped - make it a 433 */
        free(msg.cmd);
        msg.cmd = x_strdup("433");
      } else {
        /* Channel is juped - make it a 471 */
        free(msg.cmd);
        msg.cmd = x_strdup("471");
      }
    }
  }

  if (!strcasecmp(msg.cmd, "001")) {
    /* Use 001 to get the servername */
    if (msg.src.type & IRC_SERVER) {
      free(p->servername);
      p->servername = x_strdup(msg.src.name);
    }

  } else if (!strcasecmp(msg.cmd, "002")) {
    /* Ignore 002 */
  } else if (!strcasecmp(msg.cmd, "003")) {
    /* Ignore 003 */
  } else if (!strcasecmp(msg.cmd, "004")) {
    /* 004 contains all the juicy info, use it */
    if (msg.numparams >= 5) {
      free(p->servername);
      free(p->serverver);
      free(p->serverumodes);
      free(p->servercmodes);

      p->servername = x_strdup(msg.params[1]);
      p->serverver = x_strdup(msg.params[2]);
      p->serverumodes = x_strdup(msg.params[3]);
      p->servercmodes = x_strdup(msg.params[4]);

      p->server_status |= IRC_SERVER_GOTWELCOME;

      if (IS_CLIENT_READY(p) && !(p->client_status & IRC_CLIENT_SENTWELCOME))
        ircclient_welcome(p);
    }

    /* Also use this numeric to send everything state-related to the client.
       From this moment on, we assume the server is happy. */

    /* Restore the user mode */
    if (p->modes)
      ircserver_send_command(p, "MODE", "%s +%s", p->nickname, p->modes);

    /* Restore the away message */
    if (p->awaymessage) {
      ircserver_send_command(p, "AWAY", ":%s", p->awaymessage);
    } else if (!(p->client_status & IRC_CLIENT_AUTHED)
               && p->conn_class->away_message) {
      ircserver_send_command(p, "AWAY", ":%s", p->conn_class->away_message);
    }

    /* Restore the channel list */
    if (p->channels) {
      struct ircchannel *c;

      c = p->channels;
      while (c) {
        ircserver_send_command(p, "JOIN", ":%s", c->name);
        c = c->next;
      }
    }

  } else if (!strcasecmp(msg.cmd, "005")) {
    /* Ignore 005 */
  } else if (!strcasecmp(msg.cmd, "375")) {
    /* Ignore 375 unless allow_motd */
    if (p->allow_motd)
      squelch = 0;

  } else if (!strcasecmp(msg.cmd, "372")) {
    /* Ignore 372 unless allow_motd */
    if (p->allow_motd)
      squelch = 0;

  } else if (!strcasecmp(msg.cmd, "376")) {
    /* Ignore 376 unless allow_motd */
    if (p->allow_motd) {
      squelch = 0;
      p->allow_motd = 0;
    }

  } else if (!strcasecmp(msg.cmd, "422")) {
    /* Ignore 422 unless allow_motd */
    if (p->allow_motd) {
      squelch = 0;
      p->allow_motd = 0;
    }
    
  } else if (!strcasecmp(msg.cmd, "431") || !strcasecmp(msg.cmd, "432")
             || !strcasecmp(msg.cmd, "433") || !strcasecmp(msg.cmd, "436")
             || !strcasecmp(msg.cmd, "438")) {
    /* Our nickname got rejected */
    if (msg.numparams >= 2) {
      free(p->nickname);

      /* Fall back on our original if we can */
      if (strlen(msg.params[0]) && strcmp(msg.params[0], "*")) {
        /* Generate fake NICK command to make client see things our way */
        if (p->client_status & IRC_CLIENT_CONNECTED)
          ircclient_send_selfcmd(p, "NICK", ":%s", msg.params[0]);

        p->nickname = x_strdup(msg.params[0]);

        squelch = 0;
      } else {
        /* Forget the nickname... */
        p->nickname = 0;
        p->client_status &= ~(IRC_CLIENT_GOTNICK);

        /* If we don't have a client connected, then we have to regenerate
           a new nickname ourselves... Otherwise we can just let the client
           do it */
        if (!(p->client_status & IRC_CLIENT_CONNECTED)) {
          ircclient_generate_nick(p, msg.params[1]);
          ircserver_send_peercmd(p, "NICK", ":%s", p->nickname);
        } else {
          /* Have to do anti-squelch manually */
          sock_send(p->client_sock, "%s\r\n", msg.orig);
        }
      }
    } else {
      squelch = 0;
    }

  } else if (!strcasecmp(msg.cmd, "471") || !strcasecmp(msg.cmd, "473")
             || !strcasecmp(msg.cmd, "474")) {
    if (msg.numparams >= 2) {
      /* Can't join a channel */

      /* No client connected?  Lets rejoin it for it */
      if (p->client_status != IRC_CLIENT_ACTIVE) {
        struct ircchannel *chan;

        chan = ircnet_fetchchannel(p, msg.params[1]);
        if (chan) {
          chan->inactive = 1;
          ircnet_rejoin(p, chan->name);
        }
      } else {
        /* Let it handle it */
        ircnet_delchannel(p, msg.params[1]);
      }

      squelch = 0;
    }

  } else if (!strcasecmp(msg.cmd, "403") || !strcasecmp(msg.cmd, "475")
             || !strcasecmp(msg.cmd, "476") || !strcasecmp(msg.cmd, "405")) {
    if (msg.numparams >= 2) {
      /* Can't join a channel, permanent error */
      if (ircnet_fetchchannel(p, msg.params[1])) {
        /* No client connected?  Better notify it */
        if (p->client_status != IRC_CLIENT_ACTIVE) {
          if (msg.numparams >= 3) {
            irclog_notice(p, p->nickname, PACKAGE, 
                          "Couldn't rejoin %s: %s (%s)",
                          msg.params[1], msg.params[2], msg.cmd);
          } else {
            irclog_notice(p, p->nickname, PACKAGE,
                          "Couldn't rejoin %s (%s)", msg.params[1], msg.cmd);
          }
        }

        ircnet_delchannel(p, msg.params[1]);
      }

      squelch = 0;
    }

  } else if (!strcasecmp(msg.cmd, "PING")) {
    /* Reply to pings for the client */
    if (msg.numparams == 1) {
      ircserver_send_peercmd(p, "PONG", ":%s", msg.params[0]);
    } else if (msg.numparams >= 2) {
      ircserver_send_peercmd(p, "PONG", "%s :%s", msg.params[0], msg.params[1]);
    }

    /* but let it see them */
    squelch = 0;

  } else if (!strcasecmp(msg.cmd, "PONG")) {
    /* Use pongs to reset the server_stoned timer */
    if (p->allow_pong)
      squelch = 0;

    if (p->conn_class->server_pingtimeout) {
      timer_del(p, "server_stoned");
      timer_new(p, "server_stoned", p->conn_class->server_pingtimeout,
                _ircserver_stoned, (void *)0);
      p->allow_pong = 0;
    }

  } else if (!strcasecmp(msg.cmd, "NICK")) {
    if (_ircserver_forclient(p, &msg)) {
      /* Server telling us our nickname */
      if (msg.numparams >= 1) {
        if (irc_strcmp(p->nickname, msg.params[0])) {
          if (IS_CLIENT_READY(p))
            ircclient_send_selfcmd(p, "NICK", ":%s", msg.params[0]);

          ircclient_change_nick(p, msg.params[0]);
          irclog_notice(p, 0, p->servername,
                        "You changed your nickname to %s\n", msg.params[0]);
        }
      }
    } else {
      /* Someone changing their nickname */
      squelch = 0;
    }

  } else if (!strcasecmp(msg.cmd, "MODE")) {
    if (msg.numparams >= 2) {
      if (!irc_strcasecmp(p->nickname, msg.params[0])) {
        /* Personal mode change */
        int param;

        for (param = 1; param < msg.numparams; param++)
          ircclient_change_mode(p, msg.params[param]);
      }

      squelch = 0;
    }

  } else if (!strcasecmp(msg.cmd, "JOIN")) {
    if (_ircserver_forclient(p, &msg)) {
      /* Server telling us we joined a channel */
      if (msg.numparams >= 1) {
        struct ircchannel *c;

        c = ircnet_fetchchannel(p, msg.params[0]);
        if (c && c->inactive) {
          /* Must have got KICK'd or something ... */
          c->inactive = 0;

          /* If a client is connected, tell it we just joined and give it
             what they missed */
          if (p->client_status == IRC_CLIENT_ACTIVE) {
            sock_send(p->client_sock, "%s\r\n", msg.orig);
            irclog_autorecall(p, msg.params[0]);
          }
        } else if (!c) {
          /* Orginary join */
          ircnet_addchannel(p, msg.params[0]);
          squelch = 0;
        } else {
          /* Bizarre, joined a channel we thought we were already on */
          squelch = 0;
        }
      }
    } else {
      if (msg.numparams >= 1)
        irclog_notice(p, msg.params[0], p->servername,
                      "%s joined the channel", msg.src.fullname);
      squelch = 0;
    }

  } else if (!strcasecmp(msg.cmd, "PART")) {
    if (_ircserver_forclient(p, &msg)) {
      /* Server telling us we left a channel */
      if (msg.numparams >= 1) {
        ircnet_delchannel(p, msg.params[0]);
        squelch = 0;
      }
    } else {
      if (msg.numparams >= 1)
        irclog_notice(p, msg.params[0], p->servername,
                      "%s left the channel", msg.src.fullname);
      squelch = 0;
    }

  } else if (!strcasecmp(msg.cmd, "KICK")) {
    if (msg.numparams >= 2) {
      if (!irc_strcasecmp(p->nickname, msg.params[1])) {
        /* We got kicked off a channel */

        /* No client connected?  Lets rejoin it for it */
        if (p->client_status != IRC_CLIENT_ACTIVE) {
          struct ircchannel *chan;

          chan = ircnet_fetchchannel(p, msg.params[0]);
          if (chan) {
            irclog_notice(p, msg.params[0], p->servername,
                          "Kicked off by %s", msg.src.fullname);
            chan->inactive = 1;
            ircnet_rejoin(p, chan->name);
          }
        } else {
          /* Let it handle it */
          ircnet_delchannel(p, msg.params[1]);
        }

        squelch = 0;
      } else {
        squelch = 0;
      }
    }

  } else if (!strcasecmp(msg.cmd, "PRIVMSG")) {
    if (msg.numparams >= 2) {
      char *str;

      str = x_strdup(msg.params[1]);
      ircprot_stripctcp(str);
      if (strlen(str)) {
        if (msg.src.username && msg.src.hostname) {
          char *tmp;

          tmp = x_sprintf("%s!%s@%s", msg.src.name, msg.src.username,
                          msg.src.hostname);
          irclog_msg(p, msg.params[0], tmp, "%s", str);
          free(tmp);
        } else {
          irclog_msg(p, msg.params[0], msg.src.name, "%s", str);
        }
      }
      free(str);
    }

    /* All PRIVMSGs go to the client */
    squelch = 0;

  } else if (!strcasecmp(msg.cmd, "NOTICE")) {
    if (msg.numparams >= 1) {
      char *str;

      str = x_strdup(msg.params[1]);
      ircprot_stripctcp(str);
      if (strlen(str)) {
        if (msg.src.username && msg.src.hostname) {
          char *tmp;

          tmp = x_sprintf("%s!%s@%s", msg.src.name, msg.src.username,
                          msg.src.hostname);
          irclog_notice(p, msg.params[0], tmp, str);
          free(tmp);
        } else {
          irclog_notice(p, msg.params[0], msg.src.name, str);
        }
      }
      free(str);
    }

    /* All NOTICEs go to the client */
    squelch = 0;

  } else {
    squelch = 0;
  }

  if (!squelch && (p->client_status == IRC_CLIENT_ACTIVE))
    sock_send(p->client_sock, "%s\r\n", msg.orig);

  ircprot_freemsg(&msg);
  return 0;
}

/* Close the server socket itself */
int ircserver_close_sock(struct ircproxy *p) {
  sock_close(p->server_sock);

  p->server_status &= ~(IRC_SERVER_CREATED | IRC_SERVER_CONNECTED
                        | IRC_SERVER_GOTWELCOME);

  /* Make sure these don't get triggered */
  if (p->conn_class->server_pingtimeout) {
    timer_del(p, "server_ping");
    timer_del(p, "server_stoned");
  }

  return 0;
}

/* Close the connection to the server */
static int _ircserver_close(struct ircproxy *p) {
  ircserver_close_sock(p);

  if (IS_CLIENT_READY(p))
    ircclient_send_notice(p, "Lost connection to server");

  timer_new(p, "server_recon", p->conn_class->server_retry,
            _ircserver_reconnect, (void *)0);

  return 0;
}

/* hook for timer code to ping server */
static void _ircserver_ping(struct ircproxy *p, void *data) {
  debug("Pinging the server");
  ircserver_send_peercmd(p, "PING", ":%s", p->servername);
  timer_new(p, "server_ping", (int)(p->conn_class->server_pingtimeout / 2),
            _ircserver_ping, (void *)0);
}

/* hook for timer code to close a stoned server */
static void _ircserver_stoned(struct ircproxy *p, void *data) {
  /* Server is, like, stoned.  Yeah man! */
  debug("Server is stoned, reconnecting");
  ircserver_send_peercmd(p, "QUIT", ":Getting off stoned server - %s %s",
                         PACKAGE, VERSION);
  _ircserver_close(p);
}

/* Check if a message is bound for us, and if so check our username and
   hostname vars are the same */
static int _ircserver_forclient(struct ircproxy *p, struct ircmessage *msg) {
  if (!(msg->src.type & IRC_USER))
    return 0;

  if (irc_strcasecmp(p->nickname, msg->src.name))
    return 0;

  if (msg->src.username) {
    free(p->username);

    p->username = x_strdup(msg->src.username);
  }

  if (msg->src.hostname) {
    free(p->hostname);

    p->hostname = x_strdup(msg->src.hostname);
  }

  return 1;
}

/* send a command to the server from the nickname connected */
int ircserver_send_command(struct ircproxy *p, const char *command, 
                           const char *format, ...) {
  char *msg, *prefix;
  va_list ap;
  int ret;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  if (p->nickname) {
    prefix = x_sprintf(":%s ", p->nickname);
  } else {
    prefix = (char *)malloc(1);
    prefix[0] = 0;
  }

  ret = sock_send(p->server_sock, "%s%s %s\r\n", prefix, command, msg);

  free(prefix);
  free(msg);
  return ret;
}

/* send a command to the server with no prefix */
int ircserver_send_peercmd(struct ircproxy *p, const char *command, 
                                   const char *format, ...) {
  va_list ap;
  char *msg;
  int ret;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  ret = sock_send(p->server_sock, "%s %s\r\n", command, msg);

  free(msg);
  return ret;
}
