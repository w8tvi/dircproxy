/* dircproxy
 * Copyright (C) 2000,2001,2002,2003 Scott James Remnant <scott@netsplit.com>.
 *
 * dns.h
 * --
 * @(#) $Id: dns.h,v 1.6.4.1 2002/12/29 21:33:37 scott Exp $
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
extern int dns_portfromserv(const char *);
extern char *dns_servfromport(int);

#endif /* __DIRCPROXY_DNS_H */
