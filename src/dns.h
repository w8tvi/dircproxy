/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * dns.h
 * --
 * @(#) $Id: dns.h,v 1.7 2002/12/29 21:30:11 scott Exp $
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

#include "net.h"

/* handy defines */
#define DNS_MAX_HOSTLEN 256

typedef void (*dns_fun_t)(void *, void *, const char *, const char *);

/* functions */
extern int dns_endrequest(pid_t, int);
extern int dns_delall(void *);
extern void dns_flush(void);
extern int dns_addrfromhost(void *, void *, const char *, dns_fun_t);
extern int dns_hostfromaddr(void *, void *, const char *, dns_fun_t);
extern int dns_filladdr(void *, const char *, const char *,
                        SOCKADDR *, dns_fun_t);
extern int dns_portfromserv(const char *);
extern char *dns_servfromport(int);

extern int dns_getip(const char *, char *);
extern int dns_getname(const char *, char *, int);
#endif /* __DIRCPROXY_DNS_H */
