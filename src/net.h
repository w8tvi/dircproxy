/* dircproxy
 * Copyright (C) 2000,2001,2002,2003 Scott James Remnant <scott@netsplit.com>.
 *
 * net.h.h
 * --
 * @(#) $Id: net.h,v 1.9 2002/12/29 21:30:12 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_NET_H
#define __DIRCPROXY_NET_H

#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

/* Socket types */
#define SOCK_NORMAL     0x01
#define SOCK_CONNECTING 0x02
#define SOCK_LISTENING  0x04
#define SOCK_SSL	0x80

/* handy defines */
#define ACTIVITY_FUNCTION(_FUNC) ((void (*)(void *, int)) (_FUNC))
#define ERROR_FUNCTION(_FUNC) ((void (*)(void *, int, int)) (_FUNC))

/* functions */
extern int net_socket(void);
extern void net_create(int *, SSL *);
extern void net_keepalive(int);
extern int net_close(int *);
extern int net_closeall(void);
extern int net_flush(void);
extern int net_hook(int, int, SSL *ssl, void *,
                    void(*)(void *, int), void(*)(void *, int, int));
extern int net_throttle(int, long, long);
extern int net_send(int, const char *, ...);
extern int net_sendurgent(int, const char *, ...);
extern int net_queue(int, void *, int);
extern int net_gets(int, char **, const char *);
extern int net_read(int, void *, int);
extern int net_poll(void);

#endif /* __DIRCPROXY_NET_H */
