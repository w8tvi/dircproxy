/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * dcc_chat.c
 *  - DCC chat protocol
 * --
 * @(#) $Id: dcc_chat.c,v 1.3 2000/11/02 13:29:41 keybuk Exp $
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
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <dircproxy.h>
#include "sprintf.h"
#include "net.h"
#include "dns.h"
#include "timers.h"
#include "dcc_net.h"
#include "dcc_chat.h"

/* Called when we've connected to the sender */
void dccchat_connected(struct dccproxy *p, int sock) {
  if (sock != p->sender_sock) {
    error("Unexpected socket %d in dccchat_connected, expected %d", sock,
          p->sender_sock);
    return;
  }

  debug("DCC Connection succeeded");
  p->sender_status |= DCC_SENDER_CONNECTED;
  net_hook(p->sender_sock, SOCK_NORMAL, (void *)p,
           ACTIVITY_FUNCTION(dccchat_data),
           ERROR_FUNCTION(dccchat_error));

  if (p->sendee_status != DCC_SENDEE_ACTIVE) {
    net_send(p->sender_sock, "--(%s)-- Awaiting connection from remote peer\n",
             PACKAGE);
  } else {
    net_send(p->sendee_sock, "--(%s)-- Connected to remote peer\n", PACKAGE);
  }
}

/* Called when a connection fails */
void dccchat_connectfailed(struct dccproxy *p, int sock, int bad) {
  if (sock != p->sender_sock) {
    error("Unexpected socket %d in dccchat_connectfailed, expected %d", sock,
          p->sender_sock);
    return;
  }

  debug("DCC Connection failed");
  p->sender_status &= ~(DCC_SENDER_CREATED);
  p->dead = 1;
}

/* Called when the sendee has been accepted */
void dccchat_accepted(struct dccproxy *p) {
  net_hook(p->sender_sock, SOCK_NORMAL, (void *)p,
           ACTIVITY_FUNCTION(dccchat_data),
           ERROR_FUNCTION(dccchat_error));

  if (p->sender_status != DCC_SENDER_ACTIVE) {
    net_send(p->sendee_sock, "--(%s)-- Connecting to remote peer\n", PACKAGE);
  } else {
    net_send(p->sender_sock, "--(%s)-- Remote peer connected\n", PACKAGE);
  }
}

/* Called when we get data over a DCC link */
void dccchat_data(struct dccproxy *p, int sock) {
  char *str, *dir;
  int to;
 
  if (sock == p->sender_sock) {
    dir = "}}";
    to = p->sendee_sock;
  } else if (sock == p->sendee_sock) {
    dir = "{{";
    to = p->sender_sock;
  } else {
    error("Unexpected socket %d in dccchat_data, expected %d or %d", sock,
          p->sender_sock, p->sendee_sock);
    return;
  }

  str = 0;
  while (net_gets(sock, &str, "\n") > 0) {
    debug("%s '%s'", dir, str);
    net_send(to, "%s\n", str);
    free(str);
  }
}

/* Called on DCC disconnection or error */
void dccchat_error(struct dccproxy *p, int sock, int bad) {
  char *who;

  if (sock == p->sender_sock) {
    who = "Sender";
  } else if (sock == p->sendee_sock) {
    who = "Sendee";
  } else {
    error("Unexpected socket %d in dccchat_error, expected %d or %d", sock,
          p->sender_sock, p->sendee_sock);
    return;
  }

  if (bad) {
    debug("Socket error with %s", who);
  } else {
    debug("%s disconnected", who);
  }

  p->dead = 1;
}
