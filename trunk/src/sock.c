/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * sock.c
 *  - handy functions to make/close sockets
 *  - handy send() wrapper that uses printf() like format
 *  - socket data buffering
 *  - recv() function that gets data up to delimeters (newlines?)
 * --
 * @(#) $Id: sock.c,v 1.2 2000/05/13 04:07:57 keybuk Exp $
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

#include <dircproxy.h>
#include "sprintf.h"
#include "sock.h"

/* Structure used to hold temporary buffers */
struct ircbuffer {
  int sock;

  char *buff;
  size_t len;

  struct ircbuffer *next;
};

/* forward declarations */
static struct ircbuffer *_sock_fetchbuffer(int);
static int _sock_createbuffer(int, const char *, size_t);
static int _sock_deletebuffer(int);
static int _sock_flag(int, int, int);

/* Buffers */
static struct ircbuffer *sockbuffers = 0;

/* Make a non-blocking socket */
int sock_make(void) {
  int sock, param;

  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
    DEBUG_SYSCALL_FAIL("socket");
    return -1;
  }

  param = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&param, sizeof(int))) {
    DEBUG_SYSCALL_FAIL("setsockopt");
    close(sock);
    return -1;
  }

  if ((param = fcntl(sock, F_GETFL, 0)) == -1) {
    DEBUG_SYSCALL_FAIL("fcntl[nonblock]");
    close(sock);
    return -1;
  }

  if (_sock_flag(sock, O_NONBLOCK, 1)) {
    close(sock);
    return -1;
  }

  return sock;
}

/* Close a socket (and free any buffer for it) */
int sock_close(int sock) {
  _sock_deletebuffer(sock);
  close(sock);
  return 0;
}

/* Send data out to a socket (using formatting and stuff) */
int sock_send(int sock, const char *message, ...) {
  va_list ap;
  char *msg;
  int ret;

  ret = 0;

  va_start(ap, message);
  msg = x_vsprintf(message, ap);
  va_end(ap);

  /* Make the socket non-blocking for this */
  if (_sock_flag(sock, O_NONBLOCK, 0))
    return -1;

  if (send(sock, msg, strlen(msg), 0) == -1) {
    if (errno != EPIPE)
      DEBUG_SYSCALL_FAIL("send");
    ret = -1;
  }

  /* Make the socket blocking again.  If this don't work, we're in trouble */
  if (_sock_flag(sock, O_NONBLOCK, 1)) {
    close(sock);
    sock = -1;
    return -1;
  }

  free(msg);
  return ret;
}

/* Peek at the queue looking for error conditions */
int sock_peek(int sock) {
  char tmpdest[RECVBUFFDELTA];
  struct ircbuffer *b;
  int numread;

  numread = recv(sock, tmpdest, RECVBUFFDELTA, MSG_PEEK);

  /* Received 0 bytes, This means no data is in the queue. However, because
     we use a select() to test this, and that said there was, it must mean
     that the socket has been closed. */
  if (numread == 0)
    return SOCK_CLOSED;

  /* Error condition. */
  if (numread == -1) {
    if ((errno != EWOULDBLOCK) && (errno != EAGAIN)) {
      if (errno != EPIPE)
        DEBUG_SYSCALL_FAIL("recv[peek]");
      return SOCK_ERROR;
    } else {
      return SOCK_EMPTY;
    }
  }

  /* Check if any data is buffered */
  b = _sock_fetchbuffer(sock);
  if (b)
    numread += b->len;

  /* Return number of bytes read (up to RECVBUFFDELTA of course) */
  return numread;
}

/* Receive a data from a socket up until a delimeter */
int sock_recv(int sock, char **dest, const char *delim) {
  size_t destlen, numread, retlen;
  char *tmpdest, *ret;
  struct ircbuffer *b;

  destlen = RECVBUFFDELTA;
  tmpdest = (char *)malloc(destlen + 1);

  while (1) {
    numread = recv(sock, tmpdest, destlen, MSG_PEEK);

    /* Received 0 bytes, This means no data is in the queue. However, because
       we use a select() to test this, and that said there was, it must mean
       that the socket has been closed. */
    if (numread == 0) {
      free(tmpdest);
      return SOCK_CLOSED;
    }

    /* Error condition. */
    if (numread == -1) {
      if ((errno != EWOULDBLOCK) && (errno != EAGAIN)) {
        if (errno != EPIPE)
          DEBUG_SYSCALL_FAIL("recv[peek]");
        free(tmpdest);
        return SOCK_ERROR;
      } else {
        free(tmpdest);
        return SOCK_EMPTY;
      }
    }

    /* Received the number of bytes we asked for.  Need more space */
    if (numread >= destlen) {
      destlen += RECVBUFFDELTA;
      tmpdest = (char *)realloc(tmpdest, destlen + 1);
      continue;
    }

    /* Locate the delimeter */
    tmpdest[numread] = 0;
    retlen = strcspn(tmpdest, delim);

    /* Find how much we actually want to read */
    destlen = retlen;
    while ((destlen < numread) && strchr(delim, tmpdest[destlen]))
      destlen++;

    /* Read in that ammount */
    tmpdest = (char *)realloc(tmpdest, destlen);
    numread = recv(sock, tmpdest, destlen, 0);

    /* bah! socket close on us */
    if (numread == 0) {
      free(tmpdest);
      return SOCK_CLOSED;
    }

    /* Error condition. */
    if (numread == -1) {
      DEBUG_SYSCALL_FAIL("recv");
      free(tmpdest);
      return SOCK_ERROR;
    }

    /* We got less data then was peeked.
       (this really really can't happen I hope) */
    if (numread < retlen) {
      if (errno != EPIPE)
        DEBUG_SYSCALL_DOH("recv", "lost data", 0);
      if (numread)
        _sock_createbuffer(sock, tmpdest, numread);
      free(tmpdest);
      return SOCK_EMPTY;
    }

    /* We asked to get the same amount we want to return. This means there
       was no delimeter in the data */
    if (destlen == retlen)  {
      _sock_createbuffer(sock, tmpdest, numread);
      free(tmpdest);
      return SOCK_EMPTY;
    }

    b = _sock_fetchbuffer(sock);
    if (b) {
      *dest = (char *)malloc(b->len + retlen + 1);
      memcpy(*dest, b->buff, b->len);
      ret = *dest + b->len;
      _sock_deletebuffer(sock);
    } else {
      ret = *dest = (char *)malloc(retlen + 1);
    }

    memcpy(ret, tmpdest, retlen); 
    ret[retlen] = 0;

    free(tmpdest);
    return numread;
  }
}

/* Fetch a buffer */
static struct ircbuffer *_sock_fetchbuffer(int sock) {
  struct ircbuffer *b;

  b = sockbuffers;
  while (b) {
    if (b->sock == sock)
      return b;

    b = b->next;
  }

  return 0;
}

/* Create a buffer.  1 = overwritten buffer, 0 = new buffer made */
static int _sock_createbuffer(int sock, const char *data, size_t len) {
  struct ircbuffer *b;
  char *ptr;
  int ret;

  ret = 0;

  b = _sock_fetchbuffer(sock);
  if (b) {
    char *tmp;

    tmp = b->buff;
    b->buff = (char *)malloc(b->len + len);
    memcpy(b->buff, tmp, b->len);
    ptr = b->buff + b->len;
    free(tmp);

    ret = 1;
  } else {
    b = (struct ircbuffer *)malloc(sizeof(struct ircbuffer));
    ptr = b->buff = (char *)malloc(len);
    b->len = 0;
    b->sock = sock;
    b->next = sockbuffers;
    sockbuffers = b;
  }

  memcpy(ptr, data, len);
  b->len += len;

  return ret;
}

/* Delete a buffer. 1 = no buffer deleted, 0 = buffer deleted */
static int _sock_deletebuffer(int sock) {
  struct ircbuffer *b, *l;
  int ret;

  ret = 1;

  l = 0;
  b = sockbuffers;
  while (b) {
    if (b->sock == sock) {
      struct ircbuffer *n;

      n = b->next;
      free(b->buff);
      free(b);
      ret = 0;

      if (l) {
        b = l->next = n;
      } else {
        b = sockbuffers = n;
      }
    } else {
      l = b;
      b = b->next;
    }
  }

  return ret;
}

/* Set a flag on a socket on/off */
static int _sock_flag(int sock, int flag, int on) {
  int flags;

  if ((flags = fcntl(sock, F_GETFL)) == -1) {
    DEBUG_SYSCALL_FAIL("fcntl[GETFL]");
    return -1;
  }

  if (on) {
    flags |= flag;
  } else {
    flags &= ~(flag);
  }

  if (fcntl(sock, F_SETFL, flags)) {
    DEBUG_SYSCALL_FAIL("fcntl[SETFL]");
    return -1;
  }

  return 0;
}
