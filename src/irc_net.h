/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_net.h
 * --
 * @(#) $Id: irc_net.h,v 1.15 2000/08/30 10:48:40 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_IRC_NET_H
#define __DIRCPROXY_IRC_NET_H

/* required includes */
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "stringex.h"

/* a log file - there are good reasons why this isn't defined in irc_log.h */
struct logfile {
  int open, keep;
  char *filename;
  FILE *file;

  unsigned long nlines;
};

/* a description of an authorised connction */
struct ircconnclass {
  char *server_port;
  long server_retry;
  long server_dnsretry;
  long server_maxattempts;
  long server_maxinitattempts;
  long channel_rejoin;
  int disconnect_existing;
  char *drop_modes;
  char *local_address;
  char *away_message;
  char *chan_log_dir;
  int chan_log_always;
  int chan_log_timestamp;
  long chan_log_maxsize;
  long chan_log_recall;
  char *other_log_dir;
  int other_log_always;
  int other_log_timestamp;
  long other_log_maxsize;
  long other_log_recall;
  int motd_logo;
  int motd_stats;

  char *password;
  struct strlist *servers, *next_server;
  struct strlist *masklist;

  struct ircconnclass *next;
};

/* a channel someone is on */
struct ircchannel {
  char *name;
  int inactive;
  struct logfile log;

  struct ircchannel *next;
};

/* a proxied connection */
struct ircproxy {
  int dead;
  struct ircconnclass *conn_class;
  int die_on_close;

  int client_sock;
  int client_status;
  struct sockaddr_in client_addr;
  char *client_host;

  int server_sock;
  int server_status;
  struct sockaddr_in server_addr;
  long server_attempts;

  char *nickname;
  char *username;
  char *hostname;
  char *realname;
  char *servername;
  char *serverver;
  char *serverumodes;
  char *servercmodes;
  char *serverpassword;

  char *awaymessage;
  char *modes;
  struct ircchannel *channels;

  char *temp_logdir;
  struct logfile other_log;

  struct ircproxy *next;
};

/* states a client can be in */
#define IRC_CLIENT_NONE        0x00
#define IRC_CLIENT_CONNECTED   0x01
#define IRC_CLIENT_AUTHED      0x02
#define IRC_CLIENT_GOTNICK     0x10
#define IRC_CLIENT_GOTUSER     0x20
#define IRC_CLIENT_SENTWELCOME 0x40
#define IRC_CLIENT_ACTIVE      0x73

/* Can we send data to the client? */
#define IS_CLIENT_READY(_c) (((_c)->client_status & 0x33) == 0x33)

/* states a server can be in */
#define IRC_SERVER_NONE        0x00
#define IRC_SERVER_CREATED     0x01
#define IRC_SERVER_SEEN        0x02
#define IRC_SERVER_CONNECTED   0x04
#define IRC_SERVER_GOTWELCOME  0x08
#define IRC_SERVER_ACTIVE      0x0f

/* Can we send data to the server? */
#define IS_SERVER_READY(_c) (((_c)->server_status & 0x07) == 0x07)

/* global variables */
extern struct ircconnclass *connclasses;

/* functions */
extern int ircnet_listen(const char *);
extern int ircnet_poll(void);
extern void ircnet_flush(void);
extern void ircnet_flush_proxies(struct ircproxy **);
extern void ircnet_flush_connclasses(struct ircconnclass **);
extern void ircnet_freeconnclass(struct ircconnclass *);
extern int ircnet_hooksocket(int);
extern struct ircproxy *ircnet_fetchclass(struct ircconnclass *);
extern struct ircchannel *ircnet_fetchchannel(struct ircproxy *, const char *);
extern int ircnet_addchannel(struct ircproxy *, const char *);
extern int ircnet_delchannel(struct ircproxy *, const char *);
extern struct ircchannel *ircnet_freechannel(struct ircchannel *);
extern int ircnet_rejoin(struct ircproxy *, const char *);
extern int ircnet_announce_dedicated(struct ircproxy *);
extern int ircnet_announce_nolisten(struct ircproxy *);
extern int ircnet_announce_status(struct ircproxy *);

#endif /* __DIRCPROXY_IRC_NET_H */
