/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * irc_net.h
 * --
 * @(#) $Id: irc_net.h,v 1.55 2004/02/26 20:06:15 fharvey Exp $
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>

#include "irc_prot.h"
#include "stringex.h"
#include "net.h"

/* a log file - there are good reasons why this isn't defined in irc_log.h */
typedef struct logfile {
  int open, made;
  char *filename;
  FILE *file;

  unsigned long nlines, maxlines;

  int always;
} LogFile;

/* a description of an authorised connction */
typedef struct ircconnclass {
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
  
  char *nickserv_password;
   
  int ctcp_replies;

  long log_timeoffset;
  int log_events;
  int log_timestamp;
  int log_relativetime;
  char *log_dir;
  char *log_program;

  int chan_log_enabled;
  int chan_log_always;
  long chan_log_maxsize;
  long chan_log_recall;

  int private_log_enabled;
  int private_log_always;
  long private_log_maxsize;
  long private_log_recall;

  int server_log_enabled;
  int server_log_always;
  long server_log_maxsize;
  long server_log_recall;

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
  int allow_notify;
   
  char *password;
  struct strlist *servers, *next_server;
  struct strlist *masklist;
  struct strlist *channels;

  /* Most config file options can be changed by editing the config file and
     HUP'ing dircproxy.  One or two can be done from the /DIRCPROXY command
     though.  Always keep the originals. */
  
   /* EXPERIMENTAL
    * allow dynamic enable GET and SET command to get and set configuration
    * option in runtime. usefull if you want to put some configuration option
    * inside your irc client. 
    */
  int allow_dynamic;
   
  char *orig_local_address;

  struct ircconnclass *next;
} IRCConnClass;

/* a channel someone is on */
typedef struct ircchannel {
  char *name;
  char *key;
  int inactive;
  int unjoined;
  struct logfile log;

  struct ircchannel *next;
} IRCChannel;

/* a proxied connection */
typedef struct ircproxy {
  int dead;
  struct ircconnclass *conn_class;
  int die_on_close;
  time_t start;

  int client_sock;
  int client_status;
  SOCKADDR client_addr;
  char *client_host;

  int server_sock;
  int server_status;
  SOCKADDR server_addr;
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
  struct strlist *serversupported;

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
  struct logfile private_log, server_log;

  struct ircproxy *next;
} IRCProxy;

/* a dcc resume */
struct dcc_resume {
  int l_port;
  int r_port;
  char *id;
  char *capfile;
  char *rejmsg;
  char *fullname;
#ifdef __APPLE__   
   u_int32_t size;
#else
   uint32_t size;
#endif   
  struct in_addr r_addr;
  struct dcc_resume *next; 
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
