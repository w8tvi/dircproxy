/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * dns.h
 * --
 * @(#) $Id: dns.h,v 1.2 2000/10/12 16:00:39 keybuk Exp $
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

#include "irc_net.h"


/* functions */
extern int dns_endrequest(pid_t, int);
extern int dns_delall(struct ircproxy *);
extern void dns_flush(void);
extern int dns_addrfromhost(struct ircproxy *, void *, const char *,
                         void (*)(struct ircproxy *, void *,
                                  struct in_addr *, const char *));
extern int dns_hostfromaddr(struct ircproxy *, void *, struct in_addr,
                         void (*)(struct ircproxy *, void *,
                                  struct in_addr *, const char *));
extern int dns_filladdr(struct ircproxy *, const char *, const char *, int,
                        struct sockaddr_in *,
                        void (*)(struct ircproxy *, void *,
                                 struct in_addr *, const char *),
                        void *);
extern short dns_portfromserv(const char *);
extern char *dns_servfromport(short);

#endif /* __DIRCPROXY_DNS_H */
