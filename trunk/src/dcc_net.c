/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * dcc_net.c
 *  - Creating new DCC connections
 *  - Connecting to DCC Senders
 *  - The list of currently active DCC proxies
 *  - Miscellaneous DCC functions
 * --
 * @(#) $Id: dcc_net.c,v 1.16 2004/04/02 21:34:11 fharvey Exp $
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
#include "dcc_send.h"
#include "dcc_net.h"

/* forward declarations */
static int _dccnet_listen(struct dccproxy *, int *, size_t, int *);
static int _dccnet_connect(struct dccproxy *, struct in_addr, int,
                           int *, size_t, int *);
static int _dccnet_bind(int sock, int *, size_t, int *);
static void _dccnet_timedout(struct dccproxy *, void *);
static void _dccnet_accept(struct dccproxy *, int);
static void _dccnet_free(struct dccproxy *);

/* list of currently proxied connections */
static struct dccproxy *proxies = 0;

/* Create a new DCC connection */
int dccnet_new(int type, long timeout, int *range, size_t range_sz,
               int *lport, struct in_addr addr, int port,
               const char *filename, long maxsize,
               int (*n_f)(void *, const char *, const char *),
               void *n_p, const char *n_msg, uint32_t resume_from) {
  struct dccproxy *p;

  p = (struct dccproxy *)malloc(sizeof(struct dccproxy));
  memset(p, 0, sizeof(struct dccproxy));
  p->type = type;
  p->bytes_rcvd = resume_from;
  /* If we're capturing, we do not need to listen for the client connecting
     because its not going to! */
  if (p->type & DCC_SEND_CAPTURE) {
    /* Unlink first for security */
     if (!resume_from){
	/* Unlink first for security */
	if (unlink(filename) && (errno != ENOENT)) {
	   syscall_fail("unlink", filename, 0);
	   free(p);
	   return -1;
	}
     }

    /* Open for writing */
    p->cap_file = fopen(filename, "a");
    if (!p->cap_file) {
      syscall_fail("fopen", filename, 0);
      free(p);
      return -1;
    }

    p->cap_filename = x_strdup(filename);
    p->bytes_max = maxsize * 1024;

    /* Connect to the sender */
    if (_dccnet_connect(p, addr, port, range, range_sz, lport)) {
      fclose(p->cap_file);
      free(p->cap_filename);
      free(p);
      return -1;
    }

  } else {
    /* Do the connect first, because then that'll hopefully get a port,
       which the listen socket can also use later anyway */
    if (_dccnet_connect(p, addr, port, range, range_sz, lport)) {
      free(p);
      return -1;
    }

    /* Now listen, if this fails a bind() then thats fatal */
    if (_dccnet_listen(p, range, range_sz, lport)) {
      net_close(&(p->sender_sock));
      free(p);
      return -1;
    }
  }

  p->notify_func = n_f;
  p->notify_data = n_p;
  if (n_msg)
    p->notify_msg = x_strdup(n_msg);

  p->next = proxies;
  proxies = p;

  timer_new((void *)p, "timeout", timeout, TIMER_FUNCTION(_dccnet_timedout), 0);

  return 0;
}

/* Create socket to listen on */
static int _dccnet_listen(struct dccproxy *p, int *range, size_t range_sz,
                          int *port) {
  int theport;

  p->sendee_sock = net_socket(AF_INET);
  if (p->sendee_sock == -1)
    return -1;

  if (_dccnet_bind(p->sendee_sock, range, range_sz, &theport)) {
    net_close(&(p->sendee_sock));
    return -1;
  }

  if (listen(p->sendee_sock, SOMAXCONN)) {
    syscall_fail("listen", 0, 0);
    net_close(&(p->sendee_sock));
    return -1;
  }

  if (port)
    *port = theport;
  debug("Listening for DCC Sendees on port %d", theport);
  p->sendee_status |= DCC_SENDEE_LISTENING;
  net_hook(p->sendee_sock, SOCK_LISTENING, (void *)p,
           ACTIVITY_FUNCTION(_dccnet_accept), 0);

  return 0;
}

/* Connect to remote user */
static int _dccnet_connect(struct dccproxy *p, struct in_addr addr, int port,
                           int *range, size_t range_sz, int *bindport) {
  int theport;

  p->sender_addr.sin_family = AF_INET;
  p->sender_addr.sin_addr.s_addr = htonl(addr.s_addr);
  p->sender_addr.sin_port = htons(port);

  debug("Connecting to DCC Sender %s:%d", inet_ntoa(p->sender_addr.sin_addr),
        ntohs(p->sender_addr.sin_port));

  p->sender_sock = net_socket(AF_INET);
  if (p->sender_sock == -1)
    return -1;

  if (_dccnet_bind(p->sender_sock, range, range_sz, &theport)) {
    debug("Connecting to DCC Sender from random port");
  } else {
    debug("Connecting to DCC Sender from port %d", theport);
    if (bindport)
      *bindport = theport;
  }
  
  if (connect(p->sender_sock, (struct sockaddr *)&(p->sender_addr),
              sizeof(struct sockaddr_in)) && (errno != EINPROGRESS)) {
    syscall_fail("connect", inet_ntoa(p->sender_addr.sin_addr), 0);
    net_close(&(p->sender_sock));
    return -1;
  }

  p->sender_status |= DCC_SENDER_CREATED;

  if (p->type & DCC_SEND) {
    net_hook(p->sender_sock, SOCK_CONNECTING, (void *)p,
             ACTIVITY_FUNCTION(dccsend_connected),
             ERROR_FUNCTION(dccsend_connectfailed));
  } else if (p->type & DCC_CHAT) {
    net_hook(p->sender_sock, SOCK_CONNECTING, (void *)p,
             ACTIVITY_FUNCTION(dccchat_connected),
             ERROR_FUNCTION(dccchat_connectfailed));
  }

  return 0;
}

/* Bind a dcc socket to one from the allowed range */
static int _dccnet_bind(int sock, int *range, size_t range_sz, int *port) {
  struct sockaddr_in local_addr;
  int len;

  local_addr.sin_family = AF_INET;
  local_addr.sin_addr.s_addr = INADDR_ANY;

  if (range) {
    int bound = 0;
    size_t i;
    int j;

    for (i = 0; i < range_sz; i += 2) {
      for (j = range[i]; j <= range[i + 1]; j++) {
        debug("Trying to bind DCC to port %d", j);
        local_addr.sin_port = htons(j);

        if (!bind(sock, (struct sockaddr *)&local_addr,
                  sizeof(struct sockaddr_in))) {
          bound = 1;
          break;
        }
      }

      if (bound)
        break;
    }

    if (!bound) {
      debug("No free ports to bind DCC to");
      return -1;
    }
 
  } else {
    debug("Binding DCC to random port");
    local_addr.sin_port = 0;

    if (bind(sock, (struct sockaddr *)&local_addr,
             sizeof(struct sockaddr_in))) {
      syscall_fail("bind", "dcc_listen", 0);
      return -1;
    }
  }

  len = sizeof(struct sockaddr_in);
  if (getsockname(sock, (struct sockaddr *)&local_addr, &len)) {
    syscall_fail("getsockname", 0, 0);
    return -1;
  }
                  
  if (port)
    *port = ntohs(local_addr.sin_port);

  return 0;
}

/* Timer hook to check if we've timed out */
static void _dccnet_timedout(struct dccproxy *p, void *data) {
  if ((p->sender_status == DCC_SENDER_ACTIVE) && (p->type & DCC_SEND_CAPTURE)) {
    debug("Capturing, and we've connected to sender");
    return;
  }

  if (p->sendee_status != DCC_SENDEE_ACTIVE) {
    if (p->type & DCC_CHAT) {
      net_send(p->sender_sock, "--(%s)-- Timed out awaiting connection from "
                               "remote peer\n", PACKAGE);
    } else if (p->type & DCC_SEND) {
      if (p->notify_func)
        p->notify_func(p->notify_data, p->notify_msg,
                       "Timed out awaiting connection from peer");
    }

  } else if (p->sender_status != DCC_SENDER_ACTIVE) {
    if (p->type & DCC_CHAT) {
      net_send(p->sendee_sock, "--(%s)-- Connection to remote peer timed out\n",
               PACKAGE);

    } else if (p->type & DCC_SEND) {
      if (p->sender_status & DCC_SENDER_GONE) {
        debug("Sender has come and gone, but sendee is connected");
        return;
      } else {
        if (p->notify_func)
          p->notify_func(p->notify_data, p->notify_msg,
                         "Connection to peer timed out");
      }
    }

  } else {
    debug("They are talking");
    return;
  }

  p->dead = 1;
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
  net_close(&(p->sendee_sock));
  p->sendee_status &= ~(DCC_SENDEE_LISTENING);
  p->sendee_sock = newsock;
  net_create(&(p->sendee_sock));
  
  if (p->sendee_sock != -1) {
    p->sendee_status |= DCC_SENDEE_CONNECTED;

    if (p->type & DCC_SEND) {
      dccsend_accepted(p);
    } else if (p->type & DCC_CHAT) {
      dccchat_accepted(p);
    }

    debug("DCC Sendee connected from %s:%d",
          inet_ntoa(p->sendee_addr.sin_addr),
          ntohs(p->sendee_addr.sin_port));
  }
}

/* Free a DCC proxy */
static void _dccnet_free(struct dccproxy *p) {
  debug("Freeing DCC proxy");

  if (p->sender_status & DCC_SENDER_CREATED)
    net_close(&(p->sender_sock));
  if (p->sendee_status & DCC_SENDEE_CREATED)
    net_close(&(p->sendee_sock));

  if (p->cap_filename) {
    unlink(p->cap_filename);
    free(p->cap_filename);
  }
  if (p->cap_file)
    fclose(p->cap_file);
  free(p->notify_msg);
  free(p->buf);

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

      if (l) l->next = n; else proxies = n;
      p = n;
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
