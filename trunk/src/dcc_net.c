/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * dcc_net.c
 *  - Creating new DCC connections
 *  - Connecting to DCC Senders
 *  - The list of currently active DCC proxies
 *  - Miscellaneous DCC functions
 * --
 * @(#) $Id: dcc_net.c,v 1.3 2000/11/02 16:13:10 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <dircproxy.h>
#include "net.h"
#include "dns.h"
#include "timers.h"
#include "sprintf.h"
#include "stringex.h"
#include "dcc_chat.h"
#include "dcc_net.h"

/* forward declarations */
static int _dccnet_listen(struct dccproxy *, void *, short *);
static int _dccnet_connect(struct dccproxy *, struct in_addr, short);
static void _dccnet_accept(struct dccproxy *, int);
static void _dccnet_free(struct dccproxy *);

/* list of currently proxied connections */
static struct dccproxy *proxies = 0;

/* Create a new DCC connection */
int dccnet_new(int type, void *range, short *lport,
               struct in_addr addr, short port) {
  struct dccproxy *p;

  p = (struct dccproxy *)malloc(sizeof(struct dccproxy));
  memset(p, 0, sizeof(struct dccproxy));
  p->type = type;

  if (_dccnet_listen(p, range, lport)) {
    free(p);
    return -1;
  }

  if (_dccnet_connect(p, addr, port)) {
    net_close(p->sendee_sock);
    free(p);
    return -1;
  }

  p->next = proxies;
  proxies = p;

  return 0;
}

/* Create socket to listen on */
static int _dccnet_listen(struct dccproxy *p, void *range, short *port) {
  struct sockaddr_in local_addr;
  int len;

  local_addr.sin_family = AF_INET;
  local_addr.sin_addr.s_addr = INADDR_ANY;
  local_addr.sin_port = 0;

  p->sendee_sock = net_socket();
  if (p->sendee_sock == -1)
    return -1;

  if (bind(p->sendee_sock, (struct sockaddr *)&local_addr,
           sizeof(struct sockaddr_in))) {
    syscall_fail("bind", "dcc_listen", 0);
    net_close(p->sendee_sock);
    return -1;
  }

  len = sizeof(struct sockaddr_in);
  if (getsockname(p->sendee_sock, (struct sockaddr *)&local_addr, &len)) {
    syscall_fail("getsockname", 0, 0);
    net_close(p->sendee_sock);
    return -1;
  }
                  
  if (listen(p->sendee_sock, SOMAXCONN)) {
    syscall_fail("listen", 0, 0);
    net_close(p->sendee_sock);
    return -1;
  }

  if (port)
    *port = ntohs(local_addr.sin_port);
  debug("Listening for DCC Sendees on port %d", ntohs(local_addr.sin_port));
  net_hook(p->sendee_sock, SOCK_LISTENING, (void *)p,
           ACTIVITY_FUNCTION(_dccnet_accept), 0);

  return 0;
}

/* Connect to remote user */
static int _dccnet_connect(struct dccproxy *p,
                           struct in_addr addr, short port) {
  p->sender_addr.sin_family = AF_INET;
  p->sender_addr.sin_addr.s_addr = htonl(addr.s_addr);
  p->sender_addr.sin_port = htons(port);

  debug("Connecting to DCC Sender %s:%d", inet_ntoa(p->sender_addr.sin_addr),
        ntohs(p->sender_addr.sin_port));

  p->sender_sock = net_socket();
  if (p->sender_sock == -1)
    return -1;

  if (connect(p->sender_sock, (struct sockaddr *)&(p->sender_addr),
              sizeof(struct sockaddr_in)) && (errno != EINPROGRESS)) {
    syscall_fail("connect", inet_ntoa(addr), 0);
    net_close(p->sender_sock);
    return -1;
  }

  p->sender_status |= DCC_SENDER_CREATED;

  if (p->type == DCC_SEND) {
    debug("DCC_SEND unimplemented");
    net_hook(p->sender_sock, SOCK_CONNECTING, 0, 0, 0);
  } else if (p->type == DCC_CHAT) {
    net_hook(p->sender_sock, SOCK_CONNECTING, (void *)p,
             ACTIVITY_FUNCTION(dccchat_connected),
             ERROR_FUNCTION(dccchat_connectfailed));
  }

  return 0;
}

/* Accept a sendee connection */
static void _dccnet_accept(struct dccproxy *p, int sock) {
  int newsock;
  int len;

  /* Accept the connection */
  len = sizeof(struct sockaddr_in);
  newsock = accept(sock, (struct sockaddr *)&(p->sendee_addr), &len);
  if (newsock == -1) {
    syscall_fail("accept", 0, 0);
    p->dead = 1;
    return;
  }

  /* Close the listening socket and make the new socket the sendee */
  net_close(p->sendee_sock);
  p->sendee_status &= ~(DCC_SENDEE_LISTENING);
  p->sendee_sock = newsock;
  net_create(&(p->sendee_sock));
  
  if (p->sendee_sock != -1) {
    p->sendee_status |= DCC_SENDEE_CONNECTED;

    if (p->type == DCC_SEND) {
      debug("DCC_SEND unimplemented");
      net_hook(p->sendee_sock, SOCK_NORMAL, 0, 0, 0);
    } else if (p->type == DCC_CHAT) {
      dccchat_accepted(p);
    }

    debug("DCC Sendee connected from %s:%d",
          inet_ntoa(p->sendee_addr.sin_addr),
          ntohs(p->sendee_addr.sin_port));
  }
}

/* Free a DCC proxy */
static void _dccnet_free(struct dccproxy *p) {
  debug("Freeing DCC proxy %p", p);

  net_close(p->sender_sock);
  net_close(p->sendee_sock);

  dns_delall((void *)p);
  timer_delall((void *)p);
  
  free(p);
}

/* Get rid of any dead proxies */
int dccnet_expunge_proxies(void) {
  struct dccproxy *p, *l;

  l = 0;
  p = proxies;

  while (p) {
    if (p->dead) {
      struct dccproxy *n;

      n = p->next;
      _dccnet_free(p);

      p = (l ? l->next : proxies) = n;
    } else {
      l = p;
      p = p->next;
    }
  }

  return 0;
}

/* Delete all of the proxies */
void dccnet_flush(void) {
  struct dccproxy *p;

  p = proxies;

  while (p) {
    struct dccproxy *n;

    n = p->next;
    _dccnet_free(p);
    p = n;
  }

  proxies = 0;
}
