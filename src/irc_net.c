/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_net.c
 *  - Polling of sockets and acting on any data
 * --
 * @(#) $Id: irc_net.c,v 1.11 2000/08/24 11:08:27 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdio.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>

#include <dircproxy.h>
#include "sock.h"
#include "dns.h"
#include "timers.h"
#include "sprintf.h"
#include "irc_log.h"
#include "irc_string.h"
#include "irc_client.h"
#include "irc_server.h"
#include "irc_net.h"

/* forward declarations */
static struct ircproxy *_ircnet_newircproxy(void);
static int _ircnet_client_connected(struct ircproxy *);
static int _ircnet_acceptclient(int);
static void _ircnet_freeproxy(struct ircproxy *);
static int _ircnet_expunge_proxies(void);
static void _ircnet_rejoin(struct ircproxy *, void *);

/* list of connection classes */
struct ircconnclass *connclasses = 0;

/* whether we are a dedicated proxy or not */
int dedicated_proxy = 0;

/* list of currently proxied connections */
static struct ircproxy *proxies = 0;

/* socket we are listening for new client connections on */
static int listen_sock = -1;

/* Create a socket to listen on. 0 = ok, other = error */
int ircnet_listen(const char *port) {
  struct sockaddr_in local_addr;

  local_addr.sin_family = AF_INET;
  local_addr.sin_addr.s_addr = INADDR_ANY;
  local_addr.sin_port = dns_portfromserv(port);

  if (!local_addr.sin_port)
    return -1;

  if (listen_sock != -1)
    sock_close(listen_sock);

  listen_sock = sock_make();
  if (listen_sock == -1)
    return -1;

  if (bind(listen_sock, (struct sockaddr *)&local_addr,
           sizeof(struct sockaddr_in))) {
    DEBUG_SYSCALL_FAIL("bind");
    sock_close(listen_sock);
    listen_sock = -1;
    return -1;
  }

  if (listen(listen_sock, SOMAXCONN)) {
    DEBUG_SYSCALL_FAIL("listen");
    sock_close(listen_sock);
    listen_sock = -1;
    return -1;
  }

  return 0;
}

/* Poll all the sockets for activity. Returns number that did things */
int ircnet_poll(void) {
  fd_set readset, writeset;
  struct timeval timeout;
  struct ircproxy *p;
  int nr, ns, hs;

  FD_ZERO(&readset);
  FD_ZERO(&writeset);
  nr = ns = hs = 0;

  _ircnet_expunge_proxies();

  p = proxies;
  while (p) {
    if (p->server_status & IRC_SERVER_CREATED) {
      hs = (p->server_sock > hs ? p->server_sock : hs);
      ns++;

      FD_SET(p->server_sock, &readset);
      if (!(p->server_status & IRC_SERVER_CONNECTED))
        FD_SET(p->server_sock, &writeset);
    }

    if (p->client_status & IRC_CLIENT_CONNECTED) {
      if ((p->server_status == IRC_SERVER_ACTIVE) 
          || !(p->client_status & IRC_CLIENT_AUTHED)
          || !(p->client_status & IRC_CLIENT_GOTNICK)
          || !(p->client_status & IRC_CLIENT_GOTUSER)) {
        hs = (p->client_sock > hs ? p->client_sock : hs);
        ns++;
        FD_SET(p->client_sock, &readset);
      }
    }

    p = p->next;
  }

  if (listen_sock != -1) {
    hs = (listen_sock > hs ? listen_sock : hs);
    ns++;
    FD_SET(listen_sock, &readset);
  }

  if (!ns)
    return 0;

  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  nr = select(hs + 1, &readset, &writeset, 0, &timeout);

  if (nr == -1) {
    if ((errno != EINTR) && (errno != EAGAIN)) {
      DEBUG_SYSCALL_FAIL("select");
      return -1;
    } else {
      return ns;
    }
  } else if (!nr) {
    return ns;
  }

  if ((listen_sock != -1) && FD_ISSET(listen_sock, &readset))
    _ircnet_acceptclient(listen_sock);

  p = proxies;
  while (p) {
    if ((p->client_status & IRC_CLIENT_CONNECTED)
        && FD_ISSET(p->client_sock, &readset) && !p->dead)
      ircclient_data(p);

    if ((p->server_status & IRC_SERVER_CREATED) && !p->dead) {
      if (p->server_status & IRC_SERVER_CONNECTED) {
        if (FD_ISSET(p->server_sock, &readset))
          ircserver_data(p);
      } else {
        if (FD_ISSET(p->server_sock, &writeset)
            || FD_ISSET(p->server_sock, &writeset)) {
          int error, len;

          len = sizeof(int);
          if (getsockopt(p->server_sock, SOL_SOCKET,
                         SO_ERROR, &error, &len) < 0) {
            ircserver_connectfailed(p, error);
          } else if (error) {
            ircserver_connectfailed(p, error);
          } else {
            ircserver_connected(p);
          }
        }
      }
    }

    p = p->next;
  }

  return ns;
}

/* Creates a new ircproxy structure */
static struct ircproxy *_ircnet_newircproxy(void) {
  struct ircproxy *p;

  p = (struct ircproxy *)malloc(sizeof(struct ircproxy));
  memset(p, 0, sizeof(struct ircproxy));

  return p;
}

/* Finishes off the creation of an ircproxy structure */
static int _ircnet_client_connected(struct ircproxy *p) {
  p->next = proxies;
  proxies = p;

  return ircclient_connected(p);
}


/* Creates a client hooked onto a socket */
int ircnet_hooksocket(int sock) {
  struct ircproxy *p;
  int len;

  p = _ircnet_newircproxy();
  p->client_sock = sock;

  len = sizeof(struct sockaddr_in);
  if (getpeername(p->client_sock, (struct sockaddr *)&(p->client_addr), &len)) {
    DEBUG_SYSCALL_FAIL("getpeername");
    free(p);
    return -1;
  }

  p->die_on_close = 1;

  return _ircnet_client_connected(p);
}

/* Accept a client. 0 = okay */
static int _ircnet_acceptclient(int sock) {
  struct ircproxy *p;
  int len;

  p = _ircnet_newircproxy();

  len = sizeof(struct sockaddr_in);
  p->client_sock = accept(sock, (struct sockaddr *)&(p->client_addr), &len);
  if (p->client_sock == -1) {
    DEBUG_SYSCALL_FAIL("accept");
    free(p);
    return -1;
  }

  return _ircnet_client_connected(p);
}

/* Fetch a proxy for a connection class if one exists */
struct ircproxy *ircnet_fetchclass(struct ircconnclass *class) {
  struct ircproxy *p;

  p = proxies;
  while (p) {
     if (!p->dead && (p->conn_class == class))
       return p;
     p = p->next;
  }

  return 0;
}

/* Fetch a channel from a proxy */
struct ircchannel *ircnet_fetchchannel(struct ircproxy *p, const char *name) {
  struct ircchannel *c;

  c = p->channels;
  while (c) {
    if (!irc_strcasecmp(c->name, name))
      return c;

    c = c->next;
  }

  return 0;
}

/* Add a channel to a proxy */
int ircnet_addchannel(struct ircproxy *p, const char *name) {
  struct ircchannel *c;

  if (ircnet_fetchchannel(p, name))
    return 0;

  c = (struct ircchannel *)malloc(sizeof(struct ircchannel));
  memset(c, 0, sizeof(struct ircchannel));
  c->name = x_strdup(name);

  if (irclog_open(p, c->name, &(c->log)))
    ircclient_send_channotice(p, c->name, "(warning) Unable to log channel: %s",
                              c->name);

  if (p->channels) {
    struct ircchannel *cc;

    cc = p->channels;
    while (cc->next)
      cc = cc->next;

    cc->next = c;
  } else {
    p->channels = c;
  }

  return 0;
}

/* Remove a channel from a proxy */
int ircnet_delchannel(struct ircproxy *p, const char *name) {
  struct ircchannel *c, *l;

  l = 0;
  c = p->channels;

  while (c) {
    if (!irc_strcasecmp(c->name, name)) {
      if (l) {
        l->next = c->next;
      } else {
        p->channels = c->next;
      }

      ircnet_freechannel(c);
      return 0;
    } else {
      l = c;
      c = c->next;
    }
  }

  return -1;
}

/* Free an ircchannel structure, returns the next */
struct ircchannel *ircnet_freechannel(struct ircchannel *chan) {
  struct ircchannel *ret;

  ret = chan->next;

  irclog_close(&(chan->log));
  free(chan->name);
  free(chan);

  return ret;
}

/* Free an ircproxy structure */
static void _ircnet_freeproxy(struct ircproxy *p) {
  if (p->client_status & IRC_CLIENT_CONNECTED)
    ircclient_close(p);

  if (p->server_status & IRC_SERVER_CONNECTED)
    ircserver_close_sock(p);

  timer_delall(p);
  free(p->client_host);

  free(p->nickname);
  free(p->username);
  free(p->hostname);
  free(p->realname);
  free(p->servername);
  free(p->serverver);
  free(p->serverumodes);
  free(p->servercmodes);

  free(p->awaymessage);
  free(p->modes);

  if (p->channels) {
    struct ircchannel *c;

    c = p->channels;
    while (c)
      c = ircnet_freechannel(c);
  }

  irclog_close(&(p->misclog));
  irclog_closedir(p);
  free(p);
}

/* Get rid of any dead proxies */
static int _ircnet_expunge_proxies(void) {
  struct ircproxy *p, *l;

  l = 0;
  p = proxies;

  while (p) {
    if (p->dead) {
      struct ircproxy *n;

      n = p->next;
      _ircnet_freeproxy(p);

      if (l) {
        p = l->next = n;
      } else {
        p = proxies = n;
      }
    } else {
      l = p;
      p = p->next;
    }
  }

  return 0;
}

/* Get rid of all the proxies and connection classes */
void ircnet_flush(void) {
  struct ircconnclass *c;
  struct ircproxy *p;

  p = proxies;
  while (p) {
    struct ircproxy *t;

    t = p;
    p = p->next;
    _ircnet_freeproxy(t);
  }
  proxies = 0;

  c = connclasses;
  while (c) {
    struct ircconnclass *t;

    t = c;
    c = c->next;
    ircnet_freeconnclass(t);
  }
  connclasses = 0;
}

/* Free a connection class structure */
void ircnet_freeconnclass(struct ircconnclass *class) {
  struct strlist *s;

  free(class->password);
  free(class->bind);
  free(class->awaymessage);
  free(class->server_port);

  s = class->servers;
  while (s) {
    struct strlist *t;

    t = s;
    s = s->next;

    free(t->str);
    free(t);
  }

  s = class->masklist;
  while (s) {
    struct strlist *t;

    t = s;
    s = s->next;

    free(t->str);
    free(t);
  }

  free(class);
}

/* hook to rejoin a channel after a kick */
static void _ircnet_rejoin(struct ircproxy *p, void *data) {
  ircserver_send_command(p, "JOIN", ":%s", (char *)data);
  free(data);
}

/* Set a timer to rejoin a channel */
int ircnet_rejoin(struct ircproxy *p, const char *name) {
  char *str;

  str = x_strdup(name);
  if (p->conn_class->channel_rejoin == 0) {
    _ircnet_rejoin(p, (void *)str);
  } else if (p->conn_class->channel_rejoin > 0) {
    timer_new(p, 0, p->conn_class->channel_rejoin, _ircnet_rejoin, (void *)str);
  } 

  return 0;
}

/* Dedicate this proxy and create a listening socket */
int ircnet_dedicate(struct ircproxy *p) {
  struct ircconnclass *c;

  if (dedicated_proxy)
    return 1;

  if (listen_sock != -1)
    sock_close(listen_sock);

  listen_sock = sock_make();
  if (listen_sock == -1)
    return -1;

  if (listen(listen_sock, SOMAXCONN)) {
    DEBUG_SYSCALL_FAIL("listen");
    sock_close(listen_sock);
    listen_sock = -1;
    return -1;
  }

  c = connclasses;
  while (c) {
    if (c == p->conn_class) {
      c = c->next;
    } else {
      struct ircconnclass *t;
      
      t = c;
      c = c->next;
      ircnet_freeconnclass(t);
    }
  }
  connclasses = p->conn_class;
  connclasses->next = 0;

  /* Okay we're dedicated */
  dedicated_proxy = 1;
  ircnet_announce_dedicated(p);
  
  return 0;
}

/* send the dedicated listening port to the user */
int ircnet_announce_dedicated(struct ircproxy *p) {
  struct sockaddr_in listen_addr;
  int len;

  if (!IS_CLIENT_READY(p))
    return 1;

  len = sizeof(struct sockaddr_in);
  if (!getsockname(listen_sock, (struct sockaddr *)&listen_addr, &len)) {
    char *hostname;

    hostname = dns_hostfromaddr(listen_addr.sin_addr);
    ircclient_send_notice(p, "Reconnect to this session at %s:%d",
                          (hostname ? hostname : p->hostname),
                          ntohs(listen_addr.sin_port));
    free(hostname);
  } else {
    return 1;
  }

  return 0;
}

/* tell the client they can't reconnect */
int ircnet_announce_nolisten(struct ircproxy *p) {
  if (!IS_CLIENT_READY(p))
    return 1;

  ircclient_send_notice(p, "You cannot reconnect to this session");

  return 0;
}

/* tell the client whether we're dedicated or not listening */
int ircnet_announce_status(struct ircproxy *p) {
  if (p->die_on_close) {
    return ircnet_announce_nolisten(p);
  } else if (dedicated_proxy) {
    return ircnet_announce_dedicated(p);
  } else {
    return 0;
  }
}
