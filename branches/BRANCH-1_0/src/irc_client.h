/* dircproxy
 * Copyright (C) 2000,2001,2002,2003 Scott James Remnant <scott@netsplit.com>.
 *
 * irc_client.h
 * --
 * @(#) $Id: irc_client.h,v 1.6.4.1 2002/12/29 21:33:38 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_IRC_CLIENT_H
#define __DIRCPROXY_IRC_CLIENT_H

/* required includes */
#include "irc_net.h"

/* functions */
extern int ircclient_connected(struct ircproxy *);
extern int ircclient_data(struct ircproxy *);
extern int ircclient_change_nick(struct ircproxy *, const char *);
extern int ircclient_nick_changed(struct ircproxy *, const char *);
extern int ircclient_setnickname(struct ircproxy *);
extern int ircclient_checknickname(struct ircproxy *);
extern int ircclient_generate_nick(struct ircproxy *, const char *);
extern int ircclient_change_mode(struct ircproxy *, const char *);
extern int ircclient_close(struct ircproxy *);
extern int ircclient_welcome(struct ircproxy *);
extern int ircclient_send_numeric(struct ircproxy *, short, const char *, ...);
extern int ircclient_send_notice(struct ircproxy *, const char *, ...);
extern int ircclient_send_channotice(struct ircproxy *, const char *,
                                     const char *, ...);
extern int ircclient_send_command(struct ircproxy *, const char *, const char *,
                                  ...);
extern int ircclient_send_selfcmd(struct ircproxy *, const char *, const char *,
                                  ...);
extern int ircclient_send_error(struct ircproxy *, const char *, ...);

#endif /* __DIRCPROXY_IRC_CLIENT_H */
