/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * net.c
 *  - handy functions to make/close sockets
 *  - handy send() wrapper that uses printf() like format
 *  - socket data buffering
 *  - non-blocking sends
 *  - functions to retrieve data from buffers up to delimiters (newlines?)
 *  - main poll()/select() function
 * --
 * @(#) $Id: net.c,v 1.1 2000/10/18 13:26:10 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <dircproxy.h>
#include "sprintf.h"
#include "sock.h"

/* Structure to hold a socket buffer */
struct sockbuff {
  char *data;
  size_t len;
  int mode;

  struct sockbuff *next;
};

/* Structure to hold the data we keep on sockets */
struct sockinfo {
  int sock;
 
  struct sockbuff *in_buff, *in_buff_last;
  struct sockbuff *out_buff, *out_buff_last;
  struct sockbuff *pri_buff, *pri_buff_last;

  long throtbytes;
  long throtperiod;
  time_t throtlast;
  long throtamt;

  struct sockinfo *next;
};

/* forward declarations */

/* Types of buffer */
#define SB_IN  0x01
#define SB_OUT 0x02
#define SB_PRI 0x03

/* Modes of buffer */
#define SM_RAW  0x01
#define SM_PACK 0x02

/* Sockets */
static struct sockinfo *sockets = 0;

/* Make a non-blocking socket */
int net_socket(void) {
  struct sockinfo *sockinfo;
  int sock, param, flags;

  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
    syscall_fail("socket", 0, 0);
    return -1;
  }

  param = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&param, sizeof(int))) {
    syscall_fail("setsockopt", "SO_REUSEADDR", 0);
    close(sock);
    return -1;
  }

  if ((flags = fcntl(sock, F_GETFL)) == -1) {
    syscall_fail("fcntl", "F_GETFL", 0);
    close(sock);
    return -1;
  }

  flags |= O_NONBLOCK;
  if (fcntl(sock, F_SETFL, flags)) {
    syscall_fail("fcntl", "F_SETFL", 0);
    close(sock);
    return -1;
  }

  sockinfo = (struct sockinfo *)malloc(sizeof(struct sockinfo));
  memset(sockinfo, 0, sizeof(struct sockinfo));
  sockinfo->sock = sock;
  sockinfo->next = sockets;
  sockets = sockinfo;

  return sock;
}

/* Close a socket and free its data */
int net_close(int sock) {
  struct sockinfo *sockinfo;

  sockinfo = _net_fetch(sock);
  if (sockinfo) {
    int ret;
  return (sockinfo ? _net_free(sockinfo) : -1);
}

/* Close all the sockets */
int net_flush(void) {
  struct sockinfo *i;

  i = sockets;
  while (i) {
    struct sockinfo *n;

    n = i->next;
    _net_free(i);
    i = i->next;
  }

  sockets = 0;

  return 0;
}

/* Add lined data to the output socket (using formatting) */
int net_send(int sock, const char *message, ...) {
  struct sockinfo *sockinfo;

  sockinfo = _net_fetch(sock);
  if (sockinfo) {
    int ret = 0;
    va_list ap;
    char *msg;

    va_start(ap, message);
    msg = x_vsprintf(message, ap);
    va_end(ap);

    ret = _net_buffer(sockinfo, SB_OUT, SM_PACK, msg, strlen(msg));

    free(msg);
    return ret;
  } else {
    syscall_fail("net_send", 0, "bad socket provided");
    return -1;
  }
}

/* Add lined data to the priority output socket (using formatting) */
int net_sendurgent(int sock, const char *message, ...) {
  struct sockinfo *sockinfo;

  sockinfo = _net_fetch(sock);
  if (sockinfo) {
    int ret = 0;
    va_list ap;
    char *msg;

    va_start(ap, message);
    msg = x_vsprintf(message, ap);
    va_end(ap);

    ret = _net_buffer(sockinfo, SB_PRI, SM_PACK, msg, strlen(msg));

    free(msg);
    return ret;
  } else {
    syscall_fail("net_sendurgent", 0, "bad socket provided");
    return -1;
  }
}

/* Add raw data to the output socket */
int net_queue(int sock, const char *data, int len) {
  return _net_buffer(sock, SB_OUT, SM_RAW, data, len);
}

/* Amend a socket's throttle attributes */
int net_throttle(int sock, long bytes, long period) {
  struct sockinfo *sockinfo;

  sockinfo = _net_fetch(sock);
  if (sockinfo) {
    sockinfo->throtbytes = bytes;
    sockinfo->throtperiod = period;
    sockinfo->throtlast = time(0);
    sockinfo->throtamt = 0;
    return 0;
  } else {
    syscall_fail("net_throttle", 0, "bad socket provided");
    return -1;
  }
}

/* Get data from a socket up unto a delimiter */
int net_gets(int sock, char **dest, const char *delim) {
  struct sockinfo *sockinfo;

  sockinfo = _net_fetch(sock);
  if (sockinfo) {
    if (sockinfo->in_buff) {
      int bufflen, retlen, getlen;
      char *buff, *get;

      /* Convert it into a string to make things easier */
      bufflen = sockinfo->in_buff->len;
      buff = (char *)malloc(bufflen + 1);
      memcpy(buff, sockinfo->in_buff->data, bufflen);
      buff[bufflen] = 0;

      /* Find out how many characters to get and how many to return */
      retlen = strcspn(tmp, delim);
      getlen = retlen + strspn(buff + retlen, delim);
      free(buff);

      /* Make sure there was a delimiter, then get the data */
      if (retlen < bufflen) {
        get = _net_unbuffer(sockinfo, SB_IN, getlen);
        if (get) {
          *dest = (char *)malloc(retlen + 1);
          memcpy(*dest, get, retlen);
          (*dest)[retlen] = 0;
          free(get);

          return retlen;
        }
      }
    }

    return 0;
  } else {
    syscall_fail("net_gets", 0, "bad socket provided");
    return -1;
  }
}

/* Get an amount of data from a socket */
int net_read(int sock, char **dest, int len) {
  struct sockinfo *sockinfo;

  sockinfo = _net_fetch(sock);
  if (sockinfo) {
    if (sockinfo->in_buff) {
      char *get;

      if (!len)
        len = sockinfo->in_buff_len;

      *dest = _net_unbuffer(sockinfo, SB_IN, len);
      if (*dest)
        return len;
    }

    return 0;
  } else {
    syscall_fail("net_gets", 0, "bad socket provided");
    return -1;
  }
}
