/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * net.h.h
 * --
 * @(#) $Id: net.h,v 1.1 2000/10/18 13:26:10 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_NET_H
#define __DIRCPROXY_NET_H

/* functions */
extern int net_socket(void);
extern int net_close(int);
extern int net_flush(void);
extern int net_send(int, const char *, ...);
extern int net_sendurgent(int, const char *, ...);
extern int net_queue(int, const char *, int);
extern int net_throttle(int, long, long);
extern int net_gets(int, char **, const char *);

#endif /* __DIRCPROXY_SOCK_H */
