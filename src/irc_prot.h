/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_prot.h
 * --
 * @(#) $Id: irc_prot.h,v 1.2 2000/08/21 14:53:00 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_IRC_PROT_H
#define __DIRCPROXY_IRC_PROT_H

/* structure defining where a irc message came from */
struct ircsource {
  char *name;
  char *username;  /* Not server */
  char *hostname;  /* Not server */
  char *fullname;

  int type;
};

/* an irc message */
struct ircmessage {
  struct ircsource src;
  char *cmd;
  char **params;
  int numparams;

  char *orig;
};

/* types of ircsource */
#define IRC_PEER   0x0
#define IRC_SERVER 0x1
#define IRC_USER   0x2
#define IRC_EITHER 0x3

/* functions */
extern int ircprot_parsemsg(const char *, struct ircmessage *);
extern void ircprot_freemsg(struct ircmessage *);
extern void ircprot_stripctcp(char *);
extern char *ircprot_sanitize_username(const char *);

#endif /* __DIRCPROXY_IRC_PROT_H */
