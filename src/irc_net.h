/* dircproxy
 * Copyright (C) 2000,2001,2002,2003 Scott James Remnant <scott@netsplit.com>.
 *
 * irc_net.h
 * --
 * @(#) $Id: irc_net.h,v 1.46.4.1 2002/12/29 21:33:38 scott Exp $
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
#include <time.h>

#include "irc_prot.h"
#include "stringex.h"

/* a log file - there are good reasons why this isn't defined in irc_log.h */
struct logfile {
  int open, made;
  char *filename;
  FILE *file;
  FILE *copy;
  char *program;

  unsigned long nlines, maxlines;

  int always;
  int timestamp;
  int relativetime;
};

/* a description of an authorised connction */
struct ircconnclass {
  char *server_port;
  long server_retry;
  long server_dnsretry;
  long server_maxattempts;
  long server_maxinitattempts;
  int server_keepalive;
  long server_pingtimeout;
  long *server_throttle;
  int server_autoconnect;

  long channel_rejoin;
  int channel_leave_on_detach;
  int channel_rejoin_on_attach;
  
  long idle_maxtime;

  int disconnect_existing;
  int disconnect_on_detach;

  char *initial_modes;
  char *drop_modes;
  char *refuse_modes;
  
  char *local_address;

  char *away_message;
  char *quit_message;
  char *attach_message;
  char *detach_message;
  char *detach_nickname;

  int nick_keep;

  int ctcp_replies;

  int chan_log_enabled;
  int chan_log_always;
  long chan_log_maxsize;
  long chan_log_recall;
  int chan_log_timestamp;
  int chan_log_relativetime;
  char *chan_log_copydir;
  char *chan_log_program;

  int other_log_enabled;
  int other_log_always;
  long other_log_maxsize;
  long other_log_recall;
  int other_log_timestamp;
  int other_log_relativetime;
  char *other_log_copydir;
  char *other_log_program;

  long log_timeoffset;
  int log_events;

  int dcc_proxy_incoming;
  int dcc_proxy_outgoing;
  int *dcc_proxy_ports;
  size_t dcc_proxy_ports_sz;
  long dcc_proxy_timeout;
  int dcc_proxy_sendreject;

  int dcc_send_fast;

  char *dcc_capture_directory;
  int dcc_capture_always;
  int dcc_capture_withnick;
  long dcc_capture_maxsize;

  char *dcc_tunnel_incoming;
  char *dcc_tunnel_outgoing;

  char *switch_user;

  int motd_logo;
  char *motd_file;
  int motd_stats;

  int allow_persist;
  int allow_jump;
  int allow_jump_new;
  int allow_host;
  int allow_die;
  int allow_users;
  int allow_kill;

  char *password;
  struct strlist *servers, *next_server;
  struct strlist *masklist;
  struct strlist *channels;

  /* Most config file options can be changed by editing the config file and
     HUP'ing dircproxy.  One or two can be done from the /DIRCPROXY command
     though.  Always keep the originals. */
  char *orig_local_address;

  struct ircconnclass *next;
};

/* a channel someone is on */
struct ircchannel {
  char *name;
  char *key;
  int inactive;
  int unjoined;
  struct logfile log;

  struct ircchannel *next;
};

/* a proxied connection */
struct ircproxy {
  int dead;
  struct ircconnclass *conn_class;
  int die_on_close;
  time_t start;

  int client_sock;
  int client_status;
  struct sockaddr_in client_addr;
  char *client_host;

  int server_sock;
  int server_status;
  struct sockaddr_in server_addr;
  long server_attempts;

  char *nickname;
  char *setnickname;
  char *oldnickname;

  char *username;
  char *hostname;
  char *realname;
  char *servername;
  char *serverver;
  char *serverumodes;
  char *servercmodes;
  char *serverpassword;

  char *password;

  int allow_motd;
  int allow_pong;
  int squelch_411;
  int expecting_nick;
  struct strlist *squelch_modes;

  char *ctcp_userinfo;
  char *ctcp_finger;
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
#define IRC_CLIENT_GOTPASS     0x02
#define IRC_CLIENT_GOTNICK     0x04
#define IRC_CLIENT_GOTUSER     0x08
#define IRC_CLIENT_AUTHED      0x10
#define IRC_CLIENT_SENTWELCOME 0x20
#define IRC_CLIENT_ACTIVE      0x3d

/* Can we send data to the client? */
#define IS_CLIENT_READY(_c) (((_c)->client_status & 0x1d) == 0x1d)

/* states a server can be in */
#define IRC_SERVER_NONE        0x00
#define IRC_SERVER_CREATED     0x01
#define IRC_SERVER_SEEN        0x02
#define IRC_SERVER_CONNECTED   0x04
#define IRC_SERVER_INTRODUCED  0x08
#define IRC_SERVER_GOTWELCOME  0x10
#define IRC_SERVER_ACTIVE      0x1f

/* Can we send data to the server? */
#define IS_SERVER_READY(_c) (((_c)->server_status & 0x0c) == 0x0c)

/* types of event we can log */
#define IRC_LOG_TEXT   0x0001
#define IRC_LOG_ACTION 0x0002
#define IRC_LOG_CTCP   0x0004
#define IRC_LOG_JOIN   0x0008
#define IRC_LOG_PART   0x0010
#define IRC_LOG_KICK   0x0020
#define IRC_LOG_QUIT   0x0040
#define IRC_LOG_NICK   0x0080
#define IRC_LOG_MODE   0x0100
#define IRC_LOG_TOPIC  0x0200
#define IRC_LOG_CLIENT 0x0400
#define IRC_LOG_SERVER 0x0800
#define IRC_LOG_ERROR  0x1000

/* global variables */
extern struct ircconnclass *connclasses;

/* functions */
extern int ircnet_listen(const char *);
extern int ircnet_expunge_proxies(void);
extern void ircnet_flush(void);
extern void ircnet_flush_proxies(struct ircproxy **);
extern void ircnet_flush_connclasses(struct ircconnclass **);
extern void ircnet_freeconnclass(struct ircconnclass *);
extern int ircnet_hooksocket(int);
extern struct ircproxy *ircnet_fetchclass(struct ircconnclass *);
extern struct ircchannel *ircnet_fetchchannel(struct ircproxy *, const char *);
extern int ircnet_addchannel(struct ircproxy *, const char *);
extern int ircnet_delchannel(struct ircproxy *, const char *);
extern int ircnet_channel_mode(struct ircproxy *, struct ircchannel *,
                               struct ircmessage *, int);
extern struct ircchannel *ircnet_freechannel(struct ircchannel *);
extern int ircnet_rejoin(struct ircproxy *, const char *);
extern int ircnet_dedicate(struct ircproxy *);
extern int ircnet_announce_dedicated(struct ircproxy *);
extern int ircnet_announce_nolisten(struct ircproxy *);
extern int ircnet_announce_status(struct ircproxy *);

#endif /* __DIRCPROXY_IRC_NET_H */
