/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_server.c
 *  - Handling of servers connected to the proxy
 * --
 * @(#) $Id: irc_server.c,v 1.2 2000/05/13 04:41:55 keybuk Exp $
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
static void _ircserver_rejoin(struct ircproxy *, void *);
static int _ircserver_gotmsg(struct ircproxy *, const char *);
static int _ircserver_close(struct ircproxy *);
static int _ircserver_forclient(struct ircproxy *, struct ircmessage *);

/* hook for timer code to reconnect to a server */
static void _ircserver_reconnect(struct ircproxy *p, void *data) {
  /* Choose the next server.  If we have no more, choose the first again and
     increment attempts */
  p->conn_class->next_server = p->conn_class->next_server->next;
  if (!p->conn_class->next_server) {
    p->conn_class->next_server = p->conn_class->servers;
    p->server_attempts++;
  }

  if (TODO_CFG_MAXATTEMPTS && (p->server_attempts >= TODO_CFG_MAXATTEMPTS)) {
    /* If we go over maximum reattempts, then give up */
    if (IS_CLIENT_READY(p))
      ircclient_send_notice(p, "Giving up on servers.  Time to quit");

    p->conn_class = 0;
    if (p->client_status & IRC_CLIENT_CONNECTED)
      ircclient_close(p);
    p->dead = 1;
  } else if (!(p->server_status & IRC_SERVER_SEEN)
             && TODO_CFG_MAXINITATTEMPTS
             && (p->server_attempts >= TODO_CFG_MAXINITATTEMPTS)) {
    /* Otherwise check initial attempts if its our first time */
    if (IS_CLIENT_READY(p))
      ircclient_send_notice(p, "Giving up on servers.  Time to quit");
    
    p->conn_class = 0;
    if (p->client_status & IRC_CLIENT_CONNECTED)
      ircclient_close(p);
    p->dead = 1;
  } else {
    /* Attempt a new connection */
    ircserver_connect(p);
  }
}

/* hook for timer code to rejoin a channel after a kick */
static void _ircserver_rejoin(struct ircproxy *p, void *data) {
  ircserver_send_command(p, "JOIN", ":%s", (char *)data);
  free(data);
}

/* Called to initiate a connection to a server */
int ircserver_connect(struct ircproxy *p) {
  struct sockaddr_in local_addr;
  char *host;
  int ret;

  printf("Beginning connection...\n");

  if (timer_exists(p, "server_recon")) {
    if (IS_CLIENT_READY(p))
      ircclient_send_notice(p, "Connection already in progress...");
    return 0;
  }

  host = 0;
  if (dns_filladdr(p->conn_class->next_server->str, TODO_CFG_DEFAULTPORT, 1,
                   &(p->server_addr), &host)) {
    timer_new(p, "server_recon", TODO_CFG_DNSRETRY,
              _ircserver_reconnect, (void *)0);
    return -1;
  } else {
    free(p->servername);
    p->servername = host;
  }

  if (IS_CLIENT_READY(p))
    ircclient_send_notice(p, "Connecting to %s port %d",
                          p->servername, ntohs(p->server_addr.sin_port));

  p->server_sock = sock_make();
  if (p->server_sock == -1) {
    ret = -1;

  } else {
    if (p->conn_class->bind) {
      host = 0;
      memset(&local_addr, 0, sizeof(struct sockaddr_in));
      local_addr.sin_family = AF_INET;
      local_addr.sin_addr.s_addr = INADDR_ANY;

      if (dns_addrfromhost(p->conn_class->bind, &(local_addr.sin_addr),
                           &host)) {
        if (IS_CLIENT_READY(p))
          ircclient_send_notice(p, "(warning) Couldn't find address for %s",
                                p->conn_class->bind);
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
      DEBUG_SYSCALL_FAIL("connect");
      sock_close(p->server_sock);
      ret = -1;
    } else {
      ret = 0;
    }
  }

  if (ret) {
    if (IS_CLIENT_READY(p))
      ircclient_send_notice(p, "Connection failed: %s", strerror(errno));

    sock_close(p->server_sock);
    timer_new(p, "server_recon", TODO_CFG_RETRY,
              _ircserver_reconnect, (void *)0);
  } else {
    p->server_status |= IRC_SERVER_CREATED;
  }

  return ret;
}

/* Called when a new server has connected */
int ircserver_connected(struct ircproxy *p) {
  char *username;

  printf("Connection succeeded\n");
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
      DEBUG_SYSCALL_FAIL("getsockname");
    }
  }

  username = ircprot_sanitize_username(p->username);
  ircserver_send_peercmd(p, "NICK", ":%s", p->nickname);
  ircserver_send_peercmd(p, "USER", "%s 0 * :%s", username, p->realname);
  free(username);

  return 0;
}

/* Called when a connection fails */
int ircserver_connectfailed(struct ircproxy *p, int error) {
  printf("Connection failed\n");

  if (IS_CLIENT_READY(p))
    ircclient_send_notice(p, "Connection failed: %s", strerror(error));

  sock_close(p->server_sock);
  p->server_status &= ~(IRC_SERVER_CREATED);

  timer_new(p, "server_recon", TODO_CFG_RETRY,
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
      printf("Socket error\n");

    case SOCK_CLOSED:
      printf("Server disconnected\n");
      _ircserver_close(p);

      return -1;

    case SOCK_EMPTY:
      free(str);
      return 0;
  }

  printf("<< '%s'\n", str);
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

  if (!strcasecmp(msg.cmd, "001")) {
    /* Use 001 to get the servername */
    if (msg.src.type & IRC_SERVER) {
      free(p->servername);
      p->servername = strdup(msg.src.name);
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

      p->servername = strdup(msg.params[1]);
      p->serverver = strdup(msg.params[2]);
      p->serverumodes = strdup(msg.params[3]);
      p->servercmodes = strdup(msg.params[4]);

      p->server_status |= IRC_SERVER_GOTWELCOME;

      if (IS_CLIENT_READY(p) && !(p->client_status & IRC_CLIENT_SENTWELCOME))
        ircclient_welcome(p);
    }

  } else if (!strcasecmp(msg.cmd, "005")) {
    /* Ignore 005 */
  } else if (!strcasecmp(msg.cmd, "375")) {
    /* Ignore 255 */
  } else if (!strcasecmp(msg.cmd, "372")) {
    /* Ignore 255 */
  } else if (!strcasecmp(msg.cmd, "376") || !strcasecmp(msg.cmd, "422")) {
    /* Use the end of the message of the day as a hook.  This is probably
       not ideal, but X-Chat does this so I guess it works, and what's good
       enough for Peter is good enough for me. */

    /* Restore the user mode */
    if (p->modes)
      ircserver_send_command(p, "MODE", "+%s", p->modes);

    /* Restore the away message */
    if (p->awaymessage) {
      ircserver_send_command(p, "AWAY", ":%s", p->awaymessage);
    } else if (!(p->client_status & IRC_CLIENT_AUTHED)) {
      ircserver_send_command(p, "AWAY", ":%s", TODO_CFG_DETACHAWAY);
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

  } else if (!strcasecmp(msg.cmd, "431") || !strcasecmp(msg.cmd, "432")
             || !strcasecmp(msg.cmd, "433") || !strcasecmp(msg.cmd, "436")
             || !strcasecmp(msg.cmd, "437") || !strcasecmp(msg.cmd, "438")) {
    /* Our nickname got rejected */
    if (msg.numparams >= 2) {
      free(p->nickname);

      /* Fall back on our original if we can */
      if (strlen(msg.params[0]) && strcmp(msg.params[0], "*")) {
        /* Generate fake NICK command to make client see things our way */
        if (p->client_status & IRC_CLIENT_CONNECTED)
          ircclient_send_selfcmd(p, "NICK", ":%s", msg.params[0]);

        p->nickname = strdup(msg.params[0]);

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

  } else if (!strcasecmp(msg.cmd, "PING")) {
    /* Reply to pings for the client */
    if (msg.numparams == 1) {
      ircserver_send_peercmd(p, "PONG", ":%s", msg.params[0]);
    } else if (msg.numparams >= 2) {
      ircserver_send_peercmd(p, "PONG", "%s :%s", msg.params[0], msg.params[1]);
    }

    /* but let it see them */
    squelch = 0;

  } else if (!strcasecmp(msg.cmd, "NICK")) {
    if (_ircserver_forclient(p, &msg)) {
      /* Server telling us our nickname */
      if (msg.numparams >= 1) {
        if (irc_strcmp(p->nickname, msg.params[0])) {
          if (IS_CLIENT_READY(p))
            ircclient_send_selfcmd(p, "NICK", ":%s", msg.params[0]);

          ircclient_change_nick(p, msg.params[0]);
          irclog_notice_toall(p, "You changed your nickname to %s\n",
                              msg.params[0]);
        }
      }
    } else {
      /* Someone changing their nickname */
      irclog_notice_toall(p, "%s is now known as %s", msg.src.fullname,
                          msg.params[0]);
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
        printf("Joined channel '%s'\n", msg.params[0]);
        ircnet_addchannel(p, msg.params[0]);
        squelch = 0;
      }
    } else {
      if (msg.numparams >= 1)
        irclog_notice_to(p, msg.params[0], "%s joined the channel",
                        msg.src.fullname);
      squelch = 0;
    }

  } else if (!strcasecmp(msg.cmd, "PART")) {
    if (_ircserver_forclient(p, &msg)) {
      /* Server telling us we left a channel */
      if (msg.numparams >= 1) {
        printf("Left channel '%s'\n", msg.params[0]);
        ircnet_delchannel(p, msg.params[0]);
        squelch = 0;
      }
    } else {
      if (msg.numparams >= 1)
        irclog_notice_to(p, msg.params[0], "%s left the channel",
                         msg.src.fullname);
      squelch = 0;
    }

  } else if (!strcasecmp(msg.cmd, "KICK")) {
    if (msg.numparams >= 2) {
      if (!irc_strcasecmp(p->nickname, msg.params[1])) {
        /* We got kicked off a channel */
        printf("Got kicked off '%s'\n", msg.params[0]);
        irclog_notice_to(p, p->nickname, "Kicked off %s by %s",
                         msg.params[0], msg.src.fullname);
        ircnet_delchannel(p, msg.params[0]);
        squelch = 0;

        /* No client connected?  Lets rejoin it for it */
        if (p->client_status != IRC_CLIENT_ACTIVE) {
          char *str;

          str = strdup(msg.params[0]);
          timer_new(p, 0, TODO_CFG_REJOIN, _ircserver_rejoin, (void *)str);
        }
      } else {
        squelch = 0;
      }
    }

  } else if (!strcasecmp(msg.cmd, "PRIVMSG")) {
    if (msg.numparams >= 2) {
      char *str;

      str = strdup(msg.params[1]);
      ircprot_stripctcp(str);
      if (strlen(str)) {
        if (msg.src.username && msg.src.hostname) {
          irclog_write_to(p, msg.params[0], ":%s!%s@%s PRIVMSG %s :%s",
                          msg.src.name, msg.src.username, msg.src.hostname,
                          msg.params[0], str);
        } else {
          irclog_write_to(p, msg.params[0], ":%s PRIVMSG %s :%s",
                          msg.src.name, msg.params[0], str);
        }
      }
      free(str);
    }

    /* All PRIVMSGs go to the client */
    squelch = 0;

  } else if (!strcasecmp(msg.cmd, "NOTICE")) {
    if (msg.numparams >= 1) {
      char *str;

      str = strdup(msg.params[1]);
      ircprot_stripctcp(str);
      if (strlen(str)) {
        if (msg.src.username && msg.src.hostname) {
          irclog_write_to(p, msg.params[0], ":%s!%s@%s NOTICE %s :%s",
                          msg.src.name, msg.src.username, msg.src.hostname,
                          msg.params[0], str);
        } else {
          irclog_write_to(p, msg.params[0], ":%s NOTICE %s :%s",
                          msg.src.name, msg.params[0], str);
        }
      }
      free(str);
    }

    /* All NOTICEs go to the client */
    squelch = 0;

  } else {
    squelch = 0;
  }

  if (!squelch && (p->client_status == IRC_CLIENT_ACTIVE)) {
    printf("(( '%s'\n", msg.orig);
    sock_send(p->client_sock, "%s\r\n", msg.orig);
  }

  ircprot_freemsg(&msg);
  return 0;
}

/* Close the server socket itself */
int ircserver_close_sock(struct ircproxy *p) {
  printf("Disconnecting server...\n");
  sock_close(p->server_sock);
  p->server_status &= ~(IRC_SERVER_CREATED | IRC_SERVER_CONNECTED
                        | IRC_SERVER_GOTWELCOME);

  return 0;
}

/* Close the connection to the server */
static int _ircserver_close(struct ircproxy *p) {
  ircserver_close_sock(p);

  if (IS_CLIENT_READY(p))
    ircclient_send_notice(p, "Lost connection to server");

  timer_new(p, "server_recon", TODO_CFG_RETRY, _ircserver_reconnect, (void *)0);

  return 0;
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

    p->username = strdup(msg->src.username);
  }

  if (msg->src.hostname) {
    free(p->hostname);

    p->hostname = strdup(msg->src.hostname);
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
