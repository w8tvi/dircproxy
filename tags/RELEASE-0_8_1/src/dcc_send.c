/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * dcc_send.c
 *  - DCC send protocol
 * --
 * @(#) $Id: dcc_send.c,v 1.3 2000/11/15 16:10:25 keybuk Exp $
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
#include "dcc_send.h"

/* forward declarations */
static void _dccsend_data(struct dccproxy *, int);
static void _dccsend_error(struct dccproxy *, int, int);

/* Called when we've connected to the sender */
void dccsend_connected(struct dccproxy *p, int sock) {
  if (sock != p->sender_sock) {
    error("Unexpected socket %d in dccsend_connected, expected %d", sock,
          p->sender_sock);
    return;
  }

  debug("DCC Connection succeeded");
  p->sender_status |= DCC_SENDER_CONNECTED;
  net_hook(p->sender_sock, SOCK_NORMAL, (void *)p,
           ACTIVITY_FUNCTION(_dccsend_data),
           ERROR_FUNCTION(_dccsend_error));
}

/* Called when a connection fails */
void dccsend_connectfailed(struct dccproxy *p, int sock, int bad) {
  if (sock != p->sender_sock) {
    error("Unexpected socket %d in dccsend_connectfailed, expected %d", sock,
          p->sender_sock);
    return;
  }

  debug("DCC Connection failed");
  p->sender_status &= ~(DCC_SENDER_CREATED);
  p->dead = 1;
}

/* Called when the sendee has been accepted */
void dccsend_accepted(struct dccproxy *p) {
  net_hook(p->sendee_sock, SOCK_NORMAL, (void *)p,
           ACTIVITY_FUNCTION(_dccsend_data),
           ERROR_FUNCTION(_dccsend_error));
}

/* Called when we get data over a DCC link */
static void _dccsend_data(struct dccproxy *p, int sock) {
  if (sock == p->sender_sock) {
    if ((p->sendee_status != DCC_SENDEE_ACTIVE)
        && !(p->type & DCC_SEND_CAPTURE))
      return;

  } else if (sock == p->sendee_sock) {
    uint32_t ack;
    int len;

    if (p->sender_status != DCC_SENDER_ACTIVE)
      return;

    if (p->type & DCC_SEND_SIMPLE) {
      len = net_read(p->sendee_sock, (void *)&ack, sizeof(uint32_t));
      if (len == sizeof(uint32_t))
        p->bytes_ackd = ntohl(ack);
    }
  } else {
    error("Unexpected socket %d in dccsend_data, expected %d or %d", sock,
          p->sender_sock, p->sendee_sock);
    return;
  }

  if ((p->type & DCC_SEND_FAST) || (p->type & DCC_SEND_CAPTURE) || 
      (p->bytes_ackd >= p->bytes_sent)) {
    int buflen;

    buflen = net_read(p->sender_sock, 0, 0);
    if (buflen) {
      char *buf;
      int nr;

      buf = (char *)malloc(buflen);
      nr = net_read(p->sender_sock, (void *)buf, buflen);
      if (nr > 0) {
        uint32_t na;

        p->bytes_sent += nr;

        if (p->type & DCC_SEND_CAPTURE) {
          /* Write it to the file */
          fwrite((void *)buf, 1, nr, p->cap_file);

          /* Check we haven't exceeded the maximum size */
          if (p->bytes_max && (p->bytes_sent >= p->bytes_max)) {
            /* We have, kill it.  It'll automatically get unlinked */
            debug("Too big for my boots!");
            p->dead = 1;
          }
        } else {
          /* Send it to the sendee */
          net_queue(p->sendee_sock, (void *)buf, nr);
        }

        /* Report how many bytes we've ackd so far */
        na = htonl(p->bytes_sent);
        net_queue(p->sender_sock, (void *)&na, sizeof(uint32_t));
      }
      free(buf);
    }
  }
}

/* Called on DCC disconnection or error */
static void _dccsend_error(struct dccproxy *p, int sock, int bad) {
  char *who;

  if (sock == p->sender_sock) {
    who = "Sender";
  } else if (sock == p->sendee_sock) {
    who = "Sendee";
  } else {
    error("Unexpected socket %d in dccsend_error, expected %d or %d", sock,
          p->sender_sock, p->sendee_sock);
    return;
  }

  if (bad) {
    debug("Socket error with %s", who);
  } else {
    debug("%s disconnected", who);

    /* Close the file nicely if we're capturing, so it doesn't get unlinked */
    if (p->type & DCC_SEND_CAPTURE) {
      debug("%s closed", p->cap_filename);
      free(p->cap_filename);
      fclose(p->cap_file);
      p->cap_filename = 0;
      p->cap_file = 0;
    }
  }

  p->dead = 1;
}
