/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * dns.c
 *  - Some simple functions to do DNS lookups etc
 * --
 * @(#) $Id: dns.c,v 1.3 2000/05/13 05:25:04 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

#include <dircproxy.h>
#include "sprintf.h"
#include "dns.h"

/* Returns the IP address of a hostname */
int dns_addrfromhost(const char *name, struct in_addr *result, char **canon) {
  struct hostent *info;

  info = gethostbyname(name);
  if (info) {
    result->s_addr = *((unsigned long *) info->h_addr);
    if (canon)
      *canon = x_strdup(info->h_name);
  } else {
    return -1;
  }

  return 0;
}

/* Returns the hostname of an IP address */
char *dns_hostfromaddr(struct in_addr addr) {
  struct hostent *info;
  char *str;

  info = gethostbyaddr((char *)&addr, sizeof(struct in_addr), AF_INET);
  if (info) {
    str = x_strdup(info->h_name);
  } else {
    str = x_strdup(inet_ntoa(addr));
  }

  return str;
}

/* Returns a network port number for a port as a string */
short dns_portfromserv(const char *serv) {
  struct servent *entry;

  entry = getservbyname(serv, "tcp");
  return (entry ? entry->s_port : htons(atoi(serv)));
}

/* Returns a service name for a network port number */
char *dns_servfromport(short port) {
  struct servent *entry;
  char *str;

  entry = getservbyport(port, "tcp");
  if (entry) {
    str = x_strdup(entry->s_name);
  } else {
    str = x_sprintf("%d", port);
  }

  return str;
}

/* Fill a sockaddr_in from a hostname or hostname:port combo thing */
int dns_filladdr(const char *name, const char *defaultport, int allowcolon,
                 struct sockaddr_in *result, char **canon) {
  char *addr, *port;
  int ret = 0;

  memset(result, 0, sizeof(struct sockaddr_in));
  result->sin_family = AF_INET;
  if (defaultport)
    result->sin_port = dns_portfromserv(defaultport);

  addr = x_strdup(name);

  if (allowcolon) {
    port = strchr(addr, ':');

    if (port) {
      *(port++) = 0;

      result->sin_port = dns_portfromserv(port);
    }
  }

  if (dns_addrfromhost(addr, &(result->sin_addr), canon))
    ret = -1;

  free(addr);
  return ret;
}
