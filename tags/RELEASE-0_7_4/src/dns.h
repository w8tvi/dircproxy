/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * dns.h
 * --
 * @(#) $Id: dns.h,v 1.1 2000/05/13 02:13:42 keybuk Exp $
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

/* functions */
extern int dns_addrfromhost(const char *, struct in_addr *, char **);
extern char *dns_hostfromaddr(struct in_addr);
extern short dns_portfromserv(const char *);
extern char *dns_servfromport(short);
extern int dns_filladdr(const char *, const char *, int, struct sockaddr_in *,
                        char **);

#endif /* __DIRCPROXY_DNS_H */
