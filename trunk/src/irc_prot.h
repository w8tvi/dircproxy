/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * irc_prot.h
 * --
 * @(#) $Id: irc_prot.h,v 1.8 2002/12/29 21:30:12 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_IRC_PROT_H
#define __DIRCPROXY_IRC_PROT_H

/* required includes */
#include "stringex.h"

/* structure defining where a irc message came from */
struct ircsource {
  char *name;
  char *username;  /* Not server */
  char *hostname;  /* Not server */
  char *fullname;
  char *orig;

  int type;
};

/* an irc message */
struct ircmessage {
  struct ircsource src;
  char *cmd;
  char **params;
  int numparams;

  char *orig;
  char **paramstarts;
};

/* a ctcp message */
struct ctcpmessage {
  char *cmd;
  char **params;
  int numparams;

  char *orig;
  char **paramstarts;
};

/* types of ircsource */
#define IRC_PEER   0x0
#define IRC_SERVER 0x1
#define IRC_USER   0x2
#define IRC_EITHER 0x3

/* functions */
extern int ircprot_parsemsg(const char *, struct ircmessage *);
extern void ircprot_freemsg(struct ircmessage *);
extern void ircprot_stripctcp(const char *, char **, struct strlist **);
extern int ircprot_parsectcp(const char *, struct ctcpmessage *);
extern void ircprot_freectcp(struct ctcpmessage *);
extern char *ircprot_sanitize_username(const char *);

#endif /* __DIRCPROXY_IRC_PROT_H */
