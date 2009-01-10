/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * net.h
 * --
 * @(#) $Id: net.h,v 1.9 2002/12/29 21:30:12 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_NET_H
#define __DIRCPROXY_NET_H

#if HAVE_CONFIG_H
#include "config.h"
#endif

#if HAVE_STRUCT_SOCKADDR_STORAGE_SS_FAMILY
#  define HAVE_IPV6 1
#  define SOCKADDR struct sockaddr_storage
#  define SOCKADDR_LEN(x) ((x)->ss_family == AF_INET ? \
                           sizeof(struct sockaddr_in) : \
                           sizeof(struct sockaddr_in6))
#else
#  define SOCKADDR struct sockaddr_in
#  define SOCKADDR_LEN(x) sizeof(struct sockaddr_in)
#endif
/* these are in the same place in both _in and _in6 versions */
#define SOCKADDR_FAMILY(x) ((struct sockaddr_in *)(x))->sin_family
#define SOCKADDR_PORT(x) ((struct sockaddr_in *)(x))->sin_port

/* Socket types */
#define SOCK_NORMAL     0x00
#define SOCK_CONNECTING 0x01
#define SOCK_LISTENING  0x02

/* handy defines */
#define ACTIVITY_FUNCTION(_FUNC) ((void (*)(void *, int)) (_FUNC))
#define ERROR_FUNCTION(_FUNC) ((void (*)(void *, int, int)) (_FUNC))

/* functions */
extern int net_socket(int);
extern void net_create(int *);
extern void net_keepalive(int);
extern int net_close(int *);
extern int net_closeall(void);
extern int net_flush(void);
extern int net_hook(int, int, void *,
                    void(*)(void *, int), void(*)(void *, int, int));
extern int net_throttle(int, long, long);
extern int net_send(int, const char *, ...);
extern int net_sendurgent(int, const char *, ...);
extern int net_queue(int, void *, int);
extern int net_gets(int, char **, const char *);
extern int net_read(int, void *, int);
extern int net_poll(void);

extern const char *net_ntop(SOCKADDR *, char *, int);
extern int net_pton(int af, const char *, void *);
extern int net_filladdr(SOCKADDR *, const char *, unsigned short);

#endif /* __DIRCPROXY_NET_H */
