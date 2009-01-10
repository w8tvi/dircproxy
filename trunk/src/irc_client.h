/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * irc_client.h
 * --
 * @(#) $Id: irc_client.h,v 1.8 2004/02/15 00:56:18 bear Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_IRC_CLIENT_H
#define __DIRCPROXY_IRC_CLIENT_H

/* required includes */
#include "irc_net.h"
#include "help.h"

static char * client_commands[] = {
  "DIE",
  "DETACH",
  "HELP",
  "HOST",
  "JUMP",
  "KILL",
  "RECALL",
  "RELOAD",
  "MOTD",
  "PERSIST",
  "QUIT",
  "USERS",
  "SERVERS",
  "STATUS",
  "NOTIFY",
  "GET",
  "SET"
};

#define I_HELP_INDEX     0
#define I_HELP_INDEX_END 1
#define I_HELP_DIE       2
#define I_HELP_DETACH    3
#define I_HELP_HOST      4
#define I_HELP_JUMP      5
#define I_HELP_JUMP_NEW  6
#define I_HELP_KILL      7
#define I_HELP_HELP      8
#define I_HELP_RECALL    9
#define I_HELP_RELOAD    10
#define I_HELP_MOTD      11
#define I_HELP_PERSIST   12
#define I_HELP_QUIT      13
#define I_HELP_USERS     14
#define I_HELP_SERVERS   15
#define I_HELP_STATUS    16
#define I_HELP_NOTIFY    17
#define I_HELP_GET       18
#define I_HELP_SET       19

static char ** command_help[] = {
  help_index,
  help_index_end,
  help_die,
  help_detach,
  help_host,
  help_jump,
  help_jump_new,
  help_kill,
  help_help,
  help_recall,
  help_reload,
  help_motd,
  help_persist,
  help_quit,
  help_users,
  help_servers,
  help_status,
  help_notify,
  help_get,
  help_set
};

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
