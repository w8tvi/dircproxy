/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * dns.h
 * --
 * @(#) $Id: dns.h,v 1.3 2000/10/23 12:03:08 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_DNS_H
#define __DIRCPROXY_DNS_H

/* required includes */
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* handy defines */
#define DNS_FUNCTION(_FUNC) ((void (*)(void *, void *, struct in_addr *, \
                                       const char *)) _FUNC)

/* functions */
extern int dns_endrequest(pid_t, int);
extern int dns_delall(void *);
extern void dns_flush(void);
extern int dns_addrfromhost(void *, void *, const char *,
                            void (*)(void *, void *,
                                     struct in_addr *, const char *));
extern int dns_hostfromaddr(void *, void *, struct in_addr,
                            void (*)(void *, void *,
                                     struct in_addr *, const char *));
extern int dns_filladdr(void *, const char *, const char *, int,
                        struct sockaddr_in *,
                        void (*)(void *, void *,
                                 struct in_addr *, const char *),
                        void *);
extern short dns_portfromserv(const char *);
extern char *dns_servfromport(short);

#endif /* __DIRCPROXY_DNS_H */
