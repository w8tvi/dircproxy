/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * sock.h
 * --
 * @(#) $Id: sock.h,v 1.1 2000/05/13 02:13:56 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_SOCK_H
#define __DIRCPROXY_SOCK_H

/* size to increase the recv() buffer by until we find the delimeter */
#define RECVBUFFDELTA 128

/* return values from sock_peek() and sock_recv() */
#define SOCK_EMPTY  0
#define SOCK_CLOSED -1
#define SOCK_ERROR  -2

/* functions */
extern int sock_make(void);
extern int sock_close(int);
extern int sock_send(int, const char *, ...);
extern int sock_peek(int);
extern int sock_recv(int, char **, const char *);

#endif /* __DIRCPROXY_SOCK_H */
