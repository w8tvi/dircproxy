/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * dcc_chat.c
 *  - DCC chat protocol
 * --
 * @(#) $Id: dcc_chat.c,v 1.13 2002/12/29 21:30:11 scott Exp $
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

/* forward declarations */
static void _dccchat_data(struct dccproxy *, int);
static void _dccchat_error(struct dccproxy *, int, int);

/* Called when we've connected to the sender */
void dccchat_connected(struct dccproxy *p, int sock) {
  if (sock != p->sender_sock) {
    error("Unexpected socket %d in dccchat_connected, expected %d", sock,
          p->sender_sock);
    net_close(&sock);
    return;
  }

  debug("DCC Connection succeeded");
  p->sender_status |= DCC_SENDER_CONNECTED;
  net_hook(p->sender_sock, SOCK_NORMAL, (void *)p,
           ACTIVITY_FUNCTION(_dccchat_data),
           ERROR_FUNCTION(_dccchat_error));

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
    net_close(&sock);
    return;
  }

  if (p->sendee_status == DCC_SENDEE_ACTIVE)
    net_send(p->sendee_sock, "--(%s)-- Connection to remote peer failed\n",
             PACKAGE);

  if (p->notify_func)
    p->notify_func(p->notify_data, p->notify_msg,
                   "Connection to remote peer failed");

  debug("DCC Connection failed");
  p->sender_status &= ~(DCC_SENDER_CREATED);
  net_close(&(p->sender_sock));
  p->dead = 1;
}

/* Called when the sendee has been accepted */
void dccchat_accepted(struct dccproxy *p) {
  net_hook(p->sendee_sock, SOCK_NORMAL, (void *)p,
           ACTIVITY_FUNCTION(_dccchat_data),
           ERROR_FUNCTION(_dccchat_error));

  if (p->sender_status != DCC_SENDER_ACTIVE) {
    net_send(p->sendee_sock, "--(%s)-- Connecting to remote peer\n", PACKAGE);
  } else {
    net_send(p->sender_sock, "--(%s)-- Remote peer connected\n", PACKAGE);
  }
}

/* Called when we get data over a DCC link */
static void _dccchat_data(struct dccproxy *p, int sock) {
  char *str, *dir;
  int to;
 
  if (sock == p->sender_sock) {
    dir = "}}";
    to = p->sendee_sock;

    if (p->sendee_status != DCC_SENDEE_ACTIVE)
      return;
  } else if (sock == p->sendee_sock) {
    dir = "{{";
    to = p->sender_sock;

    if (p->sender_status != DCC_SENDER_ACTIVE)
      return;
  } else {
    error("Unexpected socket %d in dccchat_data, expected %d or %d", sock,
          p->sender_sock, p->sendee_sock);
    net_close(&sock);
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
static void _dccchat_error(struct dccproxy *p, int sock, int bad) {
  char *who;

  if (sock == p->sender_sock) {
    who = "Sender";
    p->sender_status &= ~(DCC_SENDER_CREATED);
    net_close(&(p->sender_sock));
  } else if (sock == p->sendee_sock) {
    who = "Sendee";
    p->sendee_status &= ~(DCC_SENDEE_CREATED);
    net_close(&(p->sendee_sock));
  } else {
    error("Unexpected socket %d in dccchat_error, expected %d or %d", sock,
          p->sender_sock, p->sendee_sock);
    net_close(&sock);
    return;
  }

  if (bad) {
    debug("Socket error with %s", who);
  } else {
    debug("%s disconnected", who);
  }

  p->dead = 1;
}
