/* dircproxy
 * Copyright (C) 2002 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_client.h
 * --
 * @(#) $Id: irc_client.h,v 1.6 2001/12/21 20:15:55 keybuk Exp $
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
