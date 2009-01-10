/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * net.c
 *  - handy functions to make/close sockets
 *  - handy send() wrapper that uses printf() like format
 *  - socket data buffering
 *  - non-blocking sends
 *  - functions to retrieve data from buffers up to delimiters (newlines?)
 *  - main poll()/select() function
 * --
 * @(#) $Id: net.c,v 1.16 2002/12/29 21:30:12 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <sys/time.h>
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

#ifdef HAVE_POLL_H
# include <poll.h>
#else /* HAVE_POLL_H */
# ifdef HAVE_SYS_POLL_H
#  include <sys/poll.h>
# endif /* HAVE_SYS_POLL_H */
#endif /* HAVE_POLL_H */

#include "sprintf.h"
#include "net.h"

/* Sanity check */
#ifndef HAVE_POLL
# ifndef HAVE_SELECT
#  error "unable to compile, no poll() or select() function"
# endif /* HAVE_SELECT */
#endif /* HAVE_POLL */

/* Structure to hold a socket buffer */
struct sockbuff {
  void *data;
  size_t linelen;
  size_t len;
  int mode;

  struct sockbuff *next;
};

/* Structure to hold the data we keep on sockets */
struct sockinfo {
  int sock;
  int closed;
 
  struct sockbuff *in_buff, *in_buff_last;
  struct sockbuff *out_buff, *out_buff_last;

  int type;
  void *info;
  void (*activity_func)(void *, int);
  void (*error_func)(void *, int, int);

  long throtbytes;
  long throtperiod;
  time_t throtlast;
  long throtamt;

  struct sockinfo *next;
};

/* forward declarations */
static struct sockinfo *_net_fetch(int);
static void _net_free(struct sockinfo *);
static void _net_freebuffers(struct sockbuff *);
static void _net_expunge(void);
static int _net_buffer(struct sockinfo *, int, int, void *, int);
static int _net_unbuffer(struct sockinfo *, int, void *, int);

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
int net_socket(int family) {
  int sock, param;

  /* Make the socket */
  sock = socket(family, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
    syscall_fail("socket", 0, 0);
    return -1;
  }

  /* Allow re-use of address */
  param = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&param, sizeof(int))) {
    syscall_fail("setsockopt", "SO_REUSEADDR", 0);
    close(sock);
    return -1;
  }

  net_create(&sock);
  return sock;
}

/* Make a socket keep_alive */
void net_keepalive(int sock) {
  struct sockinfo *sockinfo;

  sockinfo = _net_fetch(sock);
  if (sockinfo) {
    int param;

    param = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&param, sizeof(int)))
      syscall_fail("setsockopt", "SO_KEEPALIVE", 0);
  } else {
    syscall_fail("net_keepalive", 0, "bad socket provided");
  }
}

/* Create a sockinfo structure */
void net_create(int *sock) {
  struct sockinfo *sockinfo;
  int flags;

  /* Get socket flags */
  if ((flags = fcntl(*sock, F_GETFL)) == -1) {
    syscall_fail("fcntl", "F_GETFL", 0);
    close(*sock);
    *sock = -1;
    return;
  }

  /* Add non-blocking to the flags and set them */
  flags |= O_NONBLOCK;
  if (fcntl(*sock, F_SETFL, flags)) {
    syscall_fail("fcntl", "F_SETFL", 0);
    close(*sock);
    *sock = -1;
    return;
  }

  /* Make an information structure and add it to our lists */
  sockinfo = (struct sockinfo *)malloc(sizeof(struct sockinfo));
  memset(sockinfo, 0, sizeof(struct sockinfo));
  sockinfo->sock = *sock;

  if (sockets) {
    struct sockinfo *ss;

    ss = sockets;
    while (ss->next)
      ss = ss->next;

    ss->next = sockinfo;
  } else {
    sockets = sockinfo;
  }
}

/* Fetch a sockinfo structure for a socket */
static struct sockinfo *_net_fetch(int sock) {
  struct sockinfo *s;

  s = sockets;
  while (s) {
    if (s->sock == sock)
      return s;

    s = s->next;
  }

  return 0;
}

/* Close a socket and free its data */
int net_close(int *sock) {
  struct sockinfo *sockinfo;

  sockinfo = _net_fetch(*sock);
  if (sockinfo) {
    sockinfo->closed = 1;
    *sock = -1;
    return 0;
  } else {
    syscall_fail("net_close", 0, "bad socket provided");
    return -1;
  }
}

/* Free a sockinfo structure and close its socket */
static void _net_free(struct sockinfo *s) {
  if (s->in_buff)
    _net_freebuffers(s->in_buff);
  if (s->out_buff)
    _net_freebuffers(s->out_buff);

  close(s->sock);
  free(s);
}

/* Free a socket buffer chain */
static void _net_freebuffers(struct sockbuff *b) {
  while (b) {
    struct sockbuff *n;

    n = b->next;
    free(b->data);
    free(b);
    b = n;
  }
}

/* Close all the sockets and allow them a short time to send their data */
int net_closeall(void) {
  struct sockinfo *i;
  time_t until;
  int ns;

  debug("Shutting down all sockets");

  /* Don't take any longer than this to do this work */
  until = time(0) + NET_LINGER_TIME;

  /* Indicate all sockets as closed, release whatever throttle is upon them
     (to speed it up) and prevent any events from doing anything except
     closing the socket */
  i = sockets;
  while (i) {
    i->closed = 1;
    i->throtbytes = i->throtamt = i->throtperiod = 0;
    i->throtlast = 0;
    i->activity_func = 0;
    i->error_func = 0;
    i = i->next;
  }

  /* Poll sockets */
  ns = -1;
  while (time(0) < until)
    if (!(ns = net_poll()))
      break;

  if (ns > 0) {
    debug("%d sockets didn't send their data in time");
  } else if (ns < 0) {
    debug("Unexpected error occurred, oh well");
  } else {
    debug("All sockets cleaned up");
  }

  return ns;
}

/* Free all the sockets */
int net_flush(void) {
  struct sockinfo *i;

  i = sockets;
  while (i) {
    struct sockinfo *n;

    n = i->next;
    if (!i->closed)
      debug("Flushing undead %01x socket %d", i->type, i->sock);
    _net_free(i);
    i = n;
  }
  sockets = 0;

  /* Free up the ufds buffer */
  net_poll();

  return 0;
}

/* Expunge closed sockets */
static void _net_expunge(void) {
  struct sockinfo *s, *l;

  l = 0;
  s = sockets;
  while (s) {
    if (s->closed && ((s->type != SOCK_NORMAL) || !s->out_buff)) {
      struct sockinfo *n;

      n = s->next;
      _net_free(s);

      if (l) {
        s = l->next = n;
      } else {
        s = sockets = n;
      }
    } else {
      l = s;
      s = s->next;
    }
  }
}

/* Amend a socket's hooks */
int net_hook(int sock, int type, void *info, void (*activity_func)(void *, int),
             void (*error_func)(void *, int, int)) {
  struct sockinfo *sockinfo;

  sockinfo = _net_fetch(sock);
  if (sockinfo) {
    sockinfo->type = type;
    sockinfo->info = info;
    sockinfo->activity_func = activity_func;
    sockinfo->error_func = error_func;
    return 0;
  } else {
    syscall_fail("net_hook", 0, "bad socket provided");
    return -1;
  }
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
int net_queue(int sock, void *data, int len) {
  struct sockinfo *sockinfo;

  sockinfo = _net_fetch(sock);
  if (sockinfo) {
    return _net_buffer(sockinfo, SB_OUT, SM_RAW, data, len);
  } else {
    syscall_fail("net_queue", 0, "bad socket provided");
    return -1;
  }
}

/* Add data to a socket's buffer */
static int _net_buffer(struct sockinfo *s, int buff, int mode,
                       void *data, int len) {
  struct sockbuff **l;

  /* Priority stuff just gets stuck on the front */
  if (buff == SB_PRI) {
    struct sockbuff *b;

    b = (struct sockbuff *)malloc(sizeof(struct sockbuff));
    if (!b)
      return -1;
    memset(b, 0, sizeof(struct sockbuff));
    b->mode = SB_OUT;
    b->data = malloc(len);
    if (!b->data) {
      free(b);
      return -1;
    }
    memcpy(b->data, data, len);
    b->len = len;

    /* We can't put it directly on the front if there's an incomplete line
       buffer on the front */
    if (s->out_buff && (s->out_buff->mode == SM_PACK) &&
        (s->out_buff->linelen > s->out_buff->len)) {
      b->next = s->out_buff->next;
      s->out_buff->next = b;
      if (!b->next)
        s->out_buff_last = b;
      
    } else {
      b->next = s->out_buff;
      s->out_buff = b;
      if (!s->out_buff_last)
        s->out_buff_last = b;
    }

    return 0;
  }
  
  l = (buff == SB_IN) ? &s->in_buff_last : &s->out_buff_last;
  /* Check whether we can just add to the existing buffer */
  if ((mode == SM_RAW) && *l && ((*l)->mode == mode)) {
    (*l)->data = realloc((*l)->data, (*l)->len + len);
    if (!(*l)->data)
      return -1;
    memcpy((*l)->data + (*l)->len, data, len);
    (*l)->len += len;
    (*l)->linelen += len;

  } else {
    struct sockbuff *b;

    /* Allocate new buffer */
    b = (struct sockbuff *)malloc(sizeof(struct sockbuff));
    if (!b)
      return 1;
    memset(b, 0, sizeof(struct sockbuff));
    b->mode = mode;
    b->data = malloc(len);
    if (!b->data) {
      free(b);
      return -1;
    }
    memcpy(b->data, data, len);
    b->len = len;
    b->linelen = len;

    if (buff == SB_IN) {
      if (s->in_buff) {
        s->in_buff_last->next = b;
      } else {
        s->in_buff = b;
      }
      s->in_buff_last = b;
    } else {
      if (s->out_buff) {
        s->out_buff_last->next = b;
      } else {
        s->out_buff = b;
      }
      s->out_buff_last = b;
    }
  }

  return 0;
}

/* Get data from a socket up unto a delimiter */
int net_gets(int sock, char **dest, const char *delim) {
  struct sockinfo *sockinfo;

  sockinfo = _net_fetch(sock);
  if (sockinfo) {
    if (sockinfo->in_buff) {
      int bufflen, retlen, getlen;
      char *buff;

      /* Convert it into a string to make things easier */
      bufflen = sockinfo->in_buff->len;
      buff = (char *)malloc(bufflen + 1);
      memcpy(buff, sockinfo->in_buff->data, bufflen);
      buff[bufflen] = 0;

      /* Find out how many characters to get and how many to return */
      retlen = strcspn(buff, delim);
      getlen = retlen + strspn(buff + retlen, delim);
      free(buff);

      /* Make sure there was a delimiter, then get the data */
      if (retlen < bufflen) {
        void *get;

        get = malloc(getlen);
        if (!_net_unbuffer(sockinfo, SB_IN, get, getlen)) {
          if (retlen) {
            *dest = (char *)malloc(retlen + 1);
            memcpy(*dest, get, retlen);
            (*dest)[retlen] = 0;
          }
          free(get);

          return retlen;
        }

        free(get);
      }
    }

    return 0;
  } else {
    syscall_fail("net_gets", 0, "bad socket provided");
    return -1;
  }
}

/* Get an amount of data from a socket */
int net_read(int sock, void *dest, int len) {
  struct sockinfo *sockinfo;

  sockinfo = _net_fetch(sock);
  if (sockinfo) {
    if (sockinfo->in_buff) {
      void *get;

      /* Omitting len means we want to know how much data is in the buffer */
      if (!len)
        return sockinfo->in_buff->len;

      get = malloc(len);
      if (!_net_unbuffer(sockinfo, SB_IN, get, len)) {
        memcpy(dest, get, len);
        free(get);
        return len;
      }

      free(get);
    }

    return 0;
  } else {
    syscall_fail("net_gets", 0, "bad socket provided");
    return -1;
  }
}

/* Remove data from the front of a buffer */
static int _net_unbuffer(struct sockinfo *s, int buff, void *data, int len) {
  struct sockbuff *b;

  b = (buff == SB_IN ? s->in_buff : s->out_buff);

  /* Check there's enough data to unbuffer */
  if (b->len < len)
    return -1;

  /* Store data if we are given a pointer to somewhere to put it */
  if (data)
    memcpy(data, b->data, len);

  /* Check whether there's any data left */
  b->len -= len;
  if (b->len) {
    void *tmp;

    /* Yes, shift it all up */
    tmp = malloc(b->len);
    if (!tmp)
      return -1;
    memcpy(tmp, b->data + len, b->len);
    free(b->data);
    b->data = tmp;

  } else {
    struct sockbuff *n;

    /* No, free up this buffer and position the next one */
    n = b->next;
    free(b->data);
    free(b);

    if (buff == SB_IN) {
      s->in_buff = n;
      if (!s->in_buff)
        s->in_buff_last = 0;
    } else {
      s->out_buff = n;
      if (!s->out_buff)
        s->out_buff_last = 0;
    }
  }

  return 0;
}

/* Poll sockets for activity, return number of sockets or -1 if error */
int net_poll(void) {
#ifdef HAVE_POLL
  static struct pollfd *ufds = 0;
  static int m_ns = 0;
#else /* HAVE_POLL */
# ifdef HAVE_SELECT
  fd_set readset, writeset;
  struct timeval timeout;
  int hs;
# endif /* HAVE_SELECT */
#endif /* HAVE_POLL */
  struct sockinfo *s;
  int ns, nr, sn;
  time_t now;
  char *func;

#ifndef HAVE_POLL
# ifdef HAVE_SELECT
  FD_ZERO(&readset);
  FD_ZERO(&writeset);
  hs = 0;
# endif /* HAVE_SELECT */
#endif /* HAVE_POLL */
  nr = ns = 0;
  now = time(0);

  /* Really close closed sockets */
  _net_expunge();

  /* Count the number of sockets */
  s = sockets;
  while (s) {
    ns++;
    s = s->next;
  }

#ifdef HAVE_POLL
  /* See if its changed */
  if (ns != m_ns) {
    if (ns) {
      ufds = (struct pollfd *)realloc(ufds, sizeof(struct pollfd) * (ns + 1));
    } else {
      free(ufds);
      ufds = 0;
    }
    m_ns = ns;
  }
#endif

  /* No sockets to poll */
  if (!ns)
    return 0;

  /* Fill the structures */
  sn = 0;
  s = sockets;
  while (s) {
    /* If its been throtperiod since we last reset the counter, then reset
       it again. */
    if (s->throtperiod && ((now - s->throtlast) >= s->throtperiod)) {
      s->throtlast = now;
      s->throtamt = 0;
    }

#ifdef HAVE_POLL
    ufds[sn].fd = s->sock;
    ufds[sn].events = POLLIN;
    ufds[sn].revents = 0;
#else /* HAVE_POLL */
# ifdef HAVE_SELECT
    hs = (hs < s->sock ? s->sock : hs);
    FD_SET(s->sock, &readset);
# endif /* HAVE_SELECT */
#endif /* HAVE_POLL */

    /* Only poll for writing if we're connecting or we're not listening and
       there's data to write and we're either not throttling this socket or
       we've sent less then the throttle (period stuff is done above) */
#ifdef HAVE_POLL
    if (s->type == SOCK_CONNECTING) {
      ufds[sn].events |= POLLOUT;
    } else if ((s->type != SOCK_LISTENING) && s->out_buff
               && (!s->throtbytes || (s->throtamt < s->throtbytes))) {
      ufds[sn].events |= POLLOUT;
    }
#else /* HAVE_POLL */
# ifdef HAVE_SELECT
    if (s->type == SOCK_CONNECTING) {
      FD_SET(s->sock, &writeset);
    } else if ((s->type != SOCK_LISTENING) && s->out_buff
               && (!s->throtbytes || (s->throtamt < s->throtbytes))) {
      FD_SET(s->sock, &writeset);
    }
# endif /* HAVE_SELECT */
#endif /* HAVE_POLL */

    sn++;
    s = s->next;
  }

#ifdef HAVE_POLL
  /* Do the poll itself */
  nr = poll(ufds, ns, 1000);
  func = "poll";
#else /* HAVE_POLL */
# ifdef HAVE_SELECT
  /* Do the select itself */
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  nr = select(hs + 1, &readset, &writeset, 0, &timeout);
  func = "select";
# endif /* HAVE_SELECT */
#endif /* HAVE_POLL */

  /* Check for errors or non-activity */
  if (nr == -1) {
    if ((errno != EINTR) && (errno != EAGAIN)) {
#ifdef HAVE_POLL
      free(ufds);
      ufds = 0;
      m_ns = 0;
#endif /* HAVE_POLL */
      syscall_fail(func, 0, 0);
      return -1;
    }
  }

  /* Check for activity */
  sn = 0;
  s = sockets;
  while (s) {
    /* Make sure we don't check new sockets yet */
    if (sn >= ns)
      break;

    if (!s->closed || ((s->type == SOCK_NORMAL) && s->out_buff)) {
      int can_read, can_write;

#ifdef HAVE_POLL
      /* Read = any revent that isn't POLLOUT */
      can_read = (ufds[sn].revents & ~POLLOUT ? 1 : 0);
      can_write = (ufds[sn].revents & POLLOUT ? 1 : 0);
#else /* HAVE_POLL */
# ifdef HAVE_SELECT
      can_read = (FD_ISSET(s->sock, &readset) ? 1 : 0);
      can_write = (FD_ISSET(s->sock, &writeset) ? 1 : 0);
# endif /* HAVE_SELECT */
#endif /* HAVE_POLL */

      if (s->type == SOCK_CONNECTING) {
        if (can_read || can_write) {
          int error, len;

          /* If there's an error condition on the socket then the connect()
             failed, otherwise it worked */
          len = sizeof(int);
          if (getsockopt(s->sock, SOL_SOCKET, SO_ERROR,
                         (void *)&error, &len) < 0) {
            if (s->error_func) {
              s->error_func(s->info, s->sock, 1);
            } else {
              s->closed = 1;
            }
          } else if (error) {
            if (s->error_func) {
              s->error_func(s->info, s->sock, 1);
            } else {
              s->closed = 1;
            }
          } else {
            if (s->activity_func) {
              s->activity_func(s->info, s->sock);
            } else {
              s->closed = 1;
            }
          }
        }

      } else if (s->type == SOCK_LISTENING) {
        /* No error conditions for listening sockets */
        if (can_read) {
          debug("Got new connection");
          if (s->activity_func)
            s->activity_func(s->info, s->sock);
        }

      } else {
        /* If we can read from the socket, suck in all the data there is to
           keep the buffer size on the IRC server down.
           This can result in the call of the error function. */
        if (can_read) {
          char buff[NET_BLOCK_SIZE];
          int br, rr;

          br = 0;
          while ((rr = read(s->sock, buff, NET_BLOCK_SIZE)) > 0) {
            _net_buffer(s, SB_IN, SM_RAW, buff, rr);
            br += rr;
          }

          /* Some kind of error :( */
          if (rr == -1) {
            if ((errno != EINTR) && (errno != EAGAIN)) {
              int baderror;

              if (errno != ECONNRESET) {
                syscall_fail("read", 0, 0);
                baderror = 1;
              } else {
                baderror = 0;
              }
              
              /* Make sure that it really closes */
              _net_freebuffers(s->out_buff);
              s->out_buff = 0;

              if (!s->closed && s->error_func) {
                s->error_func(s->info, s->sock, baderror);
              } else {
                s->closed = 1;
              }
            }
          }
          
          /* Didn't read any bytes (socket closed) */
          if (!br && (rr != -1)) {
            /* Make sure that it really closes */
            _net_freebuffers(s->out_buff);
            s->out_buff = 0;

            if (!s->closed && s->error_func) {
              s->error_func(s->info, s->sock, 0);
            } else {
              s->closed = 1;
            }
          }
        }

        /* If we can write data to the socket write any that we have lying
           around, keeping in mind throttling of course */
        if ((!s->closed || s->out_buff) && can_write) {
          while (s->out_buff) {
            int bl, wl;

            bl = (s->out_buff->len > NET_BLOCK_SIZE
                  ? NET_BLOCK_SIZE : s->out_buff->len);
            if (s->throtbytes) {
              int tl;

              if (s->throtamt >= s->throtbytes)
                break;

              tl = s->throtbytes - s->throtamt;
              bl = (bl > (s->throtbytes - s->throtamt)
                    ? (s->throtbytes - s->throtamt) : bl);
            }

            wl = write(s->sock, s->out_buff->data, bl);
            if (wl == -1) {
              /* Don't actually detect errors or closure using write, it'll
                 poll for HUP or IN if that happens */
              if ((errno != EAGAIN) && (errno != EINTR) && (errno != EPIPE))
                syscall_fail("write", 0, 0);
              break;
            } else if (!wl) {
              /* Wrote nothing, socket is full */
              break;
            } else {
              /* Get rid of that data from the buffer */
              _net_unbuffer(s, SB_OUT, 0, wl);
              if (s->throtbytes)
                s->throtamt += wl;
            }
          }
        }

        /* If there's incoming data, call the activity function */
        if (!s->closed && s->in_buff && s->activity_func)
          s->activity_func(s->info, s->sock);
      }
    }

    sn++;
    s = s->next;
  }

  return ns;
}

const char *net_ntop(SOCKADDR *sa, char *buf, int len) {
#ifdef HAVE_IPV6
  if (sa->ss_family == AF_INET6)
    return inet_ntop(AF_INET6, &((struct sockaddr_in6 *)sa)->sin6_addr, buf, len);
  else
    return inet_ntop(AF_INET, &((struct sockaddr_in *)sa)->sin_addr, buf, len);
#else
  char *ret;
  
  ret = inet_ntoa(((struct sockaddr_in *)sa)->sin_addr);
  if (ret)
    strncpy(buf, ret, len);
  
  return buf;
#endif
}

/* wrapper for inet_pton or inet_aton */
int net_pton(int af, const char *ip, void *addr)
{
#ifdef HAVE_IPV6
    return inet_pton(af, ip, addr);
#else
    return inet_aton(ip, (struct in_addr *)addr);
#endif
}

/* fill in sockaddr struct. If IP is null, bind to anyv4 */
int net_filladdr(SOCKADDR *sa, const char *ip, unsigned short port)
{
  memset(sa, 0, sizeof(SOCKADDR));

  SOCKADDR_FAMILY(sa) = AF_INET;
  SOCKADDR_PORT(sa) = port;

  /* default is conservative: v4 where possible */
  if (!ip) {
    ((struct sockaddr_in*)sa)->sin_addr.s_addr = INADDR_ANY;
    return 1;
  }

#ifdef HAVE_IPV6
  if (inet_pton(AF_INET, ip, &((struct sockaddr_in*)sa)->sin_addr) > 0)
    return 1;
  if (inet_pton(AF_INET6, ip, &((struct sockaddr_in6*)sa)->sin6_addr) > 0) {
    SOCKADDR_FAMILY(sa) = AF_INET6;
    return 1;
  }
#else
  if (inet_aton(ip, &sa->sin_addr))
    return 1;
#endif
  
  return 0;
}
