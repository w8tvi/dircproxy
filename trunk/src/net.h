/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * net.h.h
 * --
 * @(#) $Id: net.h,v 1.3 2000/10/23 12:39:08 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_NET_H
#define __DIRCPROXY_NET_H

/* Socket types */
#define SOCK_NORMAL     0x00
#define SOCK_CONNECTING 0x01
#define SOCK_LISTENING  0x02

/* functions */
extern int net_socket(void);
extern void net_create(int *);
extern int net_close(int);
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

#endif /* __DIRCPROXY_NET_H */
