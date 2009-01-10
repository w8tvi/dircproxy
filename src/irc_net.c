/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * irc_net.c
 *  - Socket to listen for new connections on
 *  - The list of connection classes
 *  - The list of currently active proxies
 *  - Miscellaneous IRC functions
 * --
 * @(#) $Id: irc_net.c,v 1.50 2003/12/10 18:55:34 fharvey Exp $
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
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>

#include <dircproxy.h>
#include "net.h"
#include "dns.h"
#include "timers.h"
#include "sprintf.h"
#include "irc_log.h"
#include "irc_string.h"
#include "irc_client.h"
#include "irc_server.h"
#include "irc_net.h"

/* forward declarations */
static int _ircnet_listen(SOCKADDR *);
static struct ircproxy *_ircnet_newircproxy(void);
static int _ircnet_client_connected(struct ircproxy *);
static void _ircnet_acceptclient(void *, int);
static void _ircnet_freeproxy(struct ircproxy *);
static void _ircnet_rejoin(struct ircproxy *, void *);

/* list of connection classes */
struct ircconnclass *connclasses = 0;

/* whether we are a dedicated proxy or not */
static int dedicated_proxy = 0;

/* list of currently proxied connections */
static struct ircproxy *proxies = 0;

/* socket we are listening for new client connections on */
static int listen_sock = -1;

#define incopy(a)       *((struct in_addr *)a)

/* Create a socket to listen on. 0 = ok, other = error */
int ircnet_listen(const char *bindaddr) {
  SOCKADDR local_addr;

  char host[256];
  char ip[40];
  char portbuf[32];
  unsigned short port;
  
  /* 1. IPv6 [addr]:port */
  if ((sscanf(bindaddr, "[%39[^]]]:%31s", host, portbuf) == 2) ||
      /* 2. host/ipv4:port */
      (sscanf(bindaddr, "%255[^:]:%31s", host, portbuf) == 2))
    port = dns_portfromserv(portbuf);
  else {
    /* 3. port name/number */
    port = dns_portfromserv(bindaddr);
    host[0] = '\0';
  }

  if (*host) {
    if (!dns_getip(host, ip))
      return -1;
    
    if (!net_filladdr(&local_addr, ip, port))
      return -1;
  } else
    if (!net_filladdr(&local_addr, NULL, port))
      return -1;
  
  return _ircnet_listen(&local_addr);
}

/* Does the actual work of creating a listen socket. 0 = ok, other = error */
int _ircnet_listen(SOCKADDR *local_addr) {
  int this_sock;

  this_sock = net_socket(SOCKADDR_FAMILY(local_addr));
  if (this_sock == -1)
    return -1;

  if (local_addr) {
    if (bind(this_sock, (struct sockaddr *)local_addr,
             SOCKADDR_LEN(local_addr))) {
      syscall_fail("bind", "listen", 0);
      net_close(&this_sock);
      return -1;
    }
  }

  if (listen(this_sock, SOMAXCONN)) {
    syscall_fail("listen", 0, 0);
    net_close(&this_sock);
    return -1;
  }

  if (listen_sock != -1) {
    debug("Closing existing listen socket %d", listen_sock);
    net_close(&listen_sock);
  }
  debug("Listening on socket %d", this_sock);
  listen_sock = this_sock;
  net_hook(listen_sock, SOCK_LISTENING, 0,
           ACTIVITY_FUNCTION(_ircnet_acceptclient), 0);

  return 0;
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

  len = sizeof(SOCKADDR);
  if (getpeername(p->client_sock, (struct sockaddr *)&(p->client_addr), &len)) {
    syscall_fail("getpeername", "", 0);
    free(p);
    return -1;
  }

  p->die_on_close = 1;
  net_create(&(p->client_sock));

  if (p->client_sock != -1) {
    return _ircnet_client_connected(p);
  } else {
    free(p);
    return -1;
  }
}

/* Accept a client. */
static void _ircnet_acceptclient(void *data, int sock) {
  struct ircproxy *p;
  int len;

  p = _ircnet_newircproxy();

  len = sizeof(SOCKADDR);
  p->client_sock = accept(sock, (struct sockaddr *)&(p->client_addr), &len);
  if (p->client_sock == -1) {
    syscall_fail("accept", 0, 0);
    free(p);
    return;
  }
  net_create(&(p->client_sock));

  if (p->client_sock != -1) {
    _ircnet_client_connected(p);
  } else {
    free(p);
  }
  return;
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
  debug("Joined channel '%s'", c->name);

  if (p->channels) {
    struct ircchannel *cc;

    cc = p->channels;
    while (cc->next)
      cc = cc->next;

    cc->next = c;
  } else {
    p->channels = c;
  }

  /* Intialise the channel log */
  irclog_init(p, c->name);

  /* Open the channel log */
  if (p->conn_class->chan_log_enabled && p->conn_class->chan_log_always) {
    if (irclog_open(p, c->name))
      ircclient_send_channotice(p, c->name,
                                "(warning) Unable to log channel: %s", c->name);
  }

  return 0;
}

/* Remove a channel from a proxy */
int ircnet_delchannel(struct ircproxy *p, const char *name) {
  struct ircchannel *c, *l;

  l = 0;
  c = p->channels;

  debug("Parted channel '%s'", name);

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

  debug("    (which didn't exist)");
  return -1;
}

/* Got a channel mode change */
int ircnet_channel_mode(struct ircproxy *p, struct ircchannel *c,
                        struct ircmessage *msg, int modes) {
  int add = 1;
  int param;
  char *ptr;

  if (msg->numparams < (modes + 1))
    return -1;

  debug("Channel '%s' mode change '%s'", c->name, msg->paramstarts[modes]);
  ptr = msg->params[modes];
  param = modes + 1;

  while (*ptr) {
    switch (*ptr) {
      case '+':
        add = 1;
        break;
      case '-':
        add = 0;
        break;
      /* RFC2812 modes that have a parameter */
      case 'O':
      case 'o':
      case 'v':
      case 'b':
      case 'e':
      case 'I':
      case 'l':
        param++;
        break;
      /* Channel key */  
      case 'k':
        if (add) {
          if (msg->numparams >= (param + 1)) {
            debug("Set channel '%s' key '%s'", c->name, msg->params[param]);
            free(c->key);
            c->key = x_strdup(msg->params[param]);
          } else {
            debug("Bad mode from server, said +k without a key");
          }
        } else if (c->key) {
          debug("Remove channel '%s' key");
          free(c->key);
          c->key = 0;
        }
        param++;
        break;
    }

    ptr++;
  }

  return 0;
}

/* Free an ircchannel structure, returns the next */
struct ircchannel *ircnet_freechannel(struct ircchannel *chan) {
  struct ircchannel *ret;

  ret = chan->next;

  irclog_free(&(chan->log));
  free(chan->name);
  free(chan->key);
  free(chan);

  return ret;
}

/* Free an ircproxy structure */
static void _ircnet_freeproxy(struct ircproxy *p) {
  debug("Freeing proxy");

  if (p->server_status & IRC_SERVER_CONNECTED) {
    ircserver_send_command(p, "QUIT",
                           ":Terminated with extreme prejudice - %s %s",
                           PACKAGE, VERSION);
    ircserver_close_sock(p);
  }

  if (p->client_status & IRC_CLIENT_CONNECTED) {
    ircclient_send_error(p, "dircproxy going bye-bye");
    ircclient_close(p);
  }

  dns_delall((void *)p);
  timer_delall((void *)p);
  free(p->client_host);

  free(p->nickname);
  free(p->setnickname);
  free(p->oldnickname);

  free(p->username);
  free(p->hostname);
  free(p->realname);
  free(p->servername);
  free(p->serverver);
  free(p->serverumodes);
  free(p->servercmodes);
  free(p->serverpassword);

  free(p->password);

  free(p->awaymessage);
  free(p->modes);

  if (p->channels) {
    struct ircchannel *c;

    c = p->channels;
    while (c)
      c = ircnet_freechannel(c);
  }

  if (p->squelch_modes) {
    struct strlist *s;

    s = p->squelch_modes;
    while (s) {
      struct strlist *n;

      n = s->next;
      free(s->str);
      free(s);
      s = n;
    }
  }

  irclog_free(&(p->private_log));
  irclog_free(&(p->server_log));
  irclog_closetempdir(p);
  free(p);
}

/* Get rid of any dead proxies */
int ircnet_expunge_proxies(void) {
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
  ircnet_flush_proxies(&proxies);
  ircnet_flush_connclasses(&connclasses);
}

/* Get rid of all the proxies */
void ircnet_flush_proxies(struct ircproxy **p) {
  while (*p) {
    struct ircproxy *t;

    t = *p;
    *p = (*p)->next;
    _ircnet_freeproxy(t);
  }
  *p = 0;
}

/* Get rid of all the connection classes */
void ircnet_flush_connclasses(struct ircconnclass **c) {
  while (*c) {
    struct ircconnclass *t;

    t = *c;
    *c = (*c)->next;
    ircnet_freeconnclass(t);
  }
  *c = 0;
}

/* Free a connection class structure */
void ircnet_freeconnclass(struct ircconnclass *class) {
  struct strlist *s;

  free(class->server_port);
  free(class->server_throttle);
  free(class->initial_modes);
  free(class->drop_modes);
  free(class->refuse_modes);
  free(class->local_address);
  free(class->away_message);
  free(class->quit_message);
  free(class->attach_message);
  free(class->detach_message);
  free(class->detach_nickname);
  free(class->log_dir);
  free(class->log_program);
  free(class->dcc_proxy_ports);
  free(class->dcc_capture_directory);
  free(class->dcc_tunnel_incoming);
  free(class->dcc_tunnel_outgoing);
  free(class->switch_user);
  free(class->motd_file);

  free(class->orig_local_address);
  free(class->nickserv_password);
  free(class->password);
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

  s = class->channels;
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
  struct ircchannel *c;

  debug("Rejoining '%s'", (char *)data);
  c = ircnet_fetchchannel(p, (char *)data);
  if (c) {
    if (c->key) {
      ircserver_send_command(p, "JOIN", "%s :%s", c->name, c->key);
    } else {
      ircserver_send_command(p, "JOIN", ":%s", c->name);
    }
  } else {
    ircserver_send_command(p, "JOIN", ":%s", (char *)data);
  }

  free(data);
}

/* Set a timer to rejoin a channel */
int ircnet_rejoin(struct ircproxy *p, const char *name) {
  char *str;

  str = x_strdup(name);
  if (p->conn_class->channel_rejoin == 0) {
    _ircnet_rejoin(p, (void *)str);
  } else if (p->conn_class->channel_rejoin > 0) {
    debug("Will rejoin '%s' in %d seconds", str, p->conn_class->channel_rejoin);
    timer_new((void *)p, 0, p->conn_class->channel_rejoin,
              TIMER_FUNCTION(_ircnet_rejoin), (void *)str);
  } 

  return 0;
}

/* Dedicate this proxy and create a listening socket */
int ircnet_dedicate(struct ircproxy *p) {
  struct ircconnclass *c;

  if (dedicated_proxy)
    return -1;

  /* Can't dedicate if there are multiple proxies */
  if ((p != proxies) || p->next) {
    debug("Multiple active proxies, won't dedicate");
    return -1;
  }

  debug("Dedicating proxy");

  if (_ircnet_listen(0))
    return -1;

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
  SOCKADDR listen_addr;
  unsigned int port;
  int len;

  if (!IS_CLIENT_READY(p))
    return -1;

  len = sizeof(SOCKADDR);
  if (!getsockname(listen_sock, (struct sockaddr *)&listen_addr, &len)) {
    port = ntohs(SOCKADDR_PORT(&listen_addr));
  } else {
    syscall_fail("getsockname", "listen_sock", 0);
    return -1;
  }

  ircclient_send_notice(p, "Reconnect to this session at %s:%d",
                        p->hostname, port);

  return 0;
}

/* tell the client they can't reconnect */
int ircnet_announce_nolisten(struct ircproxy *p) {
  if (!IS_CLIENT_READY(p))
    return -1;

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
