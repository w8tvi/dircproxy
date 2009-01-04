/* dircproxy
 * Copyright (C) 2000,2001,2002,2003 Scott James Remnant <scott@netsplit.com>.
 * Copyright (C) 2005 Francois Harvey <fharvey at securiweb dot net>
 *
 * irc_server.c
 *  - Handling of servers connected to the proxy
 *  - Reconnection to servers
 *  - Functions to send data to servers in the correct protocol format
 * --
 * @(#) $Id: irc_server.c,v 1.68 2004/04/02 21:34:11 fharvey Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <pwd.h>
#include <stdio.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>

#include <dircproxy.h>
#include "sprintf.h"
#include "net.h"
#include "dns.h"
#include "timers.h"
#include "dcc_net.h"
#include "irc_log.h"
#include "irc_net.h"
#include "irc_prot.h"
#include "irc_string.h"
#include "irc_client.h"
#include "irc_server.h"

/* forward declarations */
static void _ircserver_reconnect(struct ircproxy *, void *);
static void _ircserver_connect2(struct ircproxy *, void *, const char *,
                                const char *);
static void _ircserver_connect3(struct ircproxy *, void *, const char *,
                                const char *);
static void _ircserver_connected(struct ircproxy *, int);
static void _ircserver_connected2(struct ircproxy *, void *, const char *,
                                  const char *);
static void _ircserver_connectfailed(struct ircproxy *, int, int);
static void _ircserver_data(struct ircproxy *, int);
static void _ircserver_error(struct ircproxy *, int, int);
static int _ircserver_gotmsg(struct ircproxy *, const char *);
static int _ircserver_close(struct ircproxy *);
static int _ircserver_lost(struct ircproxy *);
static void _ircserver_ping(struct ircproxy *, void *);
static void _ircserver_stoned(struct ircproxy *, void *);
static void _ircserver_antiidle(struct ircproxy *, void *);
static int _ircserver_forclient(struct ircproxy *, struct ircmessage *);
static int _ircserver_send_dccreject(struct ircproxy *, const char *, const char *);
static int _ircserver_dccresume_timeout(struct ircproxy *, struct dcc_resume *);

struct dcc_resume *dcc_resume_list=NULL;


/* Time/date format for strftime(3) */
#define CTCP_TIMEDATE_FORMAT "%a, %d %b %Y %H:%M:%S %z"

/* hook for timer code to reconnect to a server */
static void _ircserver_reconnect(struct ircproxy *p, void *data) {
  debug("Reconnecting to server");

  /* Choose the next server.  If we have no more, choose the first again and
     increment attempts */
  p->conn_class->next_server = p->conn_class->next_server->next;
  if (!p->conn_class->next_server) {
    p->conn_class->next_server = p->conn_class->servers;
    p->server_attempts++;
  }

  debug("%sAttempt %d", (p->server_status & IRC_SERVER_SEEN ? "" : "Initial "),
        p->server_attempts + 1);

  if (p->conn_class->server_maxattempts
      && (p->server_attempts >= p->conn_class->server_maxattempts)) {
    /* If we go over maximum reattempts, then give up */
    if (IS_CLIENT_READY(p))
      ircclient_send_notice(p, "Giving up on servers.  Time to quit");

    p->conn_class = 0;
    if (p->client_status & IRC_CLIENT_CONNECTED) {
      ircclient_send_error(p, "Maximum connection attempts exceeded");
      ircclient_close(p);
    }
    debug("Giving up on servers, reattempted too much");
    p->dead = 1;
  } else if (!(p->server_status & IRC_SERVER_SEEN)
             && p->conn_class->server_maxinitattempts
             && (p->server_attempts >= p->conn_class->server_maxinitattempts)) {
    /* Otherwise check initial attempts if its our first time */
    if (IS_CLIENT_READY(p))
      ircclient_send_notice(p, "Giving up on servers.  Time to quit");
    
    p->conn_class = 0;
    if (p->client_status & IRC_CLIENT_CONNECTED) {
      ircclient_send_error(p, "Maximum initial connection attempts exceeded");
      ircclient_close(p);
    }
    debug("Giving up on servers, can't get initial connection");
    p->dead = 1;
  } else {
    /* Attempt a new connection */
    ircserver_connect(p);
  }
}

/* Called to initiate a connection to a server */
int ircserver_connect(struct ircproxy *p) {
  char *server;

  debug("Connecting to server (stage 1)");

  if (timer_exists((void *)p, "server_recon")) {
    debug("Connection already in progress");
    if (IS_CLIENT_READY(p))
      ircclient_send_notice(p, "Connection already in progress...");
    return 0;
  }

  server = x_strdup(p->conn_class->next_server->str);
  if (strchr(server, ':') != strrchr(server, ':')) {
    /* More than one :, second denotes password */
    char *pass;

    pass = strrchr(server, ':');
    *(pass++) = 0;

    if (strlen(pass))
      p->serverpassword = x_strdup(pass);
  }
 
  if (IS_CLIENT_READY(p))
    ircclient_send_notice(p, "Looking up %s...", server);

  /* DNS lookup the server */
  dns_filladdr((void *)p, server, p->conn_class->server_port,
               &(p->server_addr), (dns_fun_t) _ircserver_connect2);
  free(server);
  return 0;
}

/* Called to initiate a connection to a server once its been looked up */
static void _ircserver_connect2(struct ircproxy *p, void *data,
                                const char *ip, const char *host) {
  if (!host || !ip) {
    debug("DNS failure, retrying");
    timer_new((void *)p, "server_recon", p->conn_class->server_retry,
              TIMER_FUNCTION(_ircserver_reconnect), (void *)0);
    free(p->serverpassword);
    p->serverpassword = 0;
    return;
  }

  debug("Resolved server");

  /* Copy the found information into p */
  free(p->servername);
  p->servername = x_strdup(host);
  net_filladdr(&p->server_addr, ip, (unsigned short)data);
  
  if (p->conn_class->local_address) {
    dns_addrfromhost((void *)p, 0, p->conn_class->local_address,
                     (dns_fun_t)_ircserver_connect3);
  } else {
    _ircserver_connect3(p, 0, 0, 0);
  }
}

/* Called to initiate a connection to a server once its been looked up
   and the local_host has been looked up */
static void _ircserver_connect3(struct ircproxy *p, void *data,
                                const char *ip, const char *host) {
  int ret;
#ifdef HAVE_SETEUID
  int switched = 0;
  pid_t old_euid;
#endif /* HAVE_SETEUID */

  debug("Connecting to %s port %d", p->servername,
        ntohs(SOCKADDR_PORT(&p->server_addr)));

  if (IS_CLIENT_READY(p))
    ircclient_send_notice(p, "Connecting to %s port %d",
                          p->servername, ntohs(SOCKADDR_PORT(&p->server_addr)));

#ifdef HAVE_SETEUID
  old_euid = geteuid();

  /* Switch to a user */
  if (p->conn_class->switch_user) {
    struct passwd *pwd;

    /* switch_user can be a username or a user id */
    pwd = getpwnam(p->conn_class->switch_user);
    if (pwd) {
      debug("Switching to user '%s'", p->conn_class->switch_user);
    } else {
      uid_t uid;

      /* Make sure that its "0" not an invalid user if atoi returns 0 */
      uid = atoi(p->conn_class->switch_user);
      if (uid || !strcmp(p->conn_class->switch_user, "0")) {
        pwd = getpwuid(uid);
        if (pwd)
          debug("Switching to user #%d", uid);
      }
    }

    /* Set the effective user id if we're root */
    if (pwd && (getuid() == 0)) {
      if (!seteuid(pwd->pw_uid)) {
        switched = 1;
      } else {
        syscall_fail("seteuid", 0, 0);
      }
    }

    /* Warn if we didn't */
    if (!switched) {
      if (IS_CLIENT_READY(p))
        ircclient_send_notice(p, "(warning) Couldn't switch to username %s",
                              p->conn_class->switch_user);
    }
  }
#endif /* HAVE_SETEUID */

  p->server_sock = net_socket(SOCKADDR_FAMILY(&p->server_addr));

#ifdef HAVE_SETEUID
  /* Switch back to our original euid */
  if (switched) {
    if (seteuid(old_euid)) {
      /* Oh, Fuck! */
      syscall_fail("seteuid", 0, 0);
      abort();
    }
  }
#endif /* HAVE_SETEUID */

  if (p->server_sock == -1) {
    ret = -1;

  } else {
    if (p->conn_class->server_keepalive)
      net_keepalive(p->server_sock);

    if (p->conn_class->local_address) {
      if (ip) {
        SOCKADDR local_addr;

        net_filladdr(&local_addr, ip, 0);
        if (bind(p->server_sock, (struct sockaddr *)&local_addr, SOCKADDR_LEN(&local_addr))) {
          if (IS_CLIENT_READY(p))
            ircclient_send_notice(p, "(warning) Couldn't use local address %s",
                                  host);
        } else {
          free(p->hostname);
          p->hostname = (host ? x_strdup(host) : p->conn_class->local_address);
        }
      } else {
        if (IS_CLIENT_READY(p))
          ircclient_send_notice(p, "(warning) Couldn't find address for %s",
                                p->conn_class->local_address);
      }
    }

    if (connect(p->server_sock, (struct sockaddr *)&(p->server_addr),
                SOCKADDR_LEN(&p->server_addr))
        && (errno != EINPROGRESS)) {
      syscall_fail("connect", p->servername, 0);
      net_close(&(p->server_sock));
      ret = -1;
    } else {
      ret = 0;
    }
  }

  if (ret) {
    if (IS_CLIENT_READY(p))
      ircclient_send_notice(p, "Connection failed: %s", strerror(errno));
    debug("Connection failed: %s", strerror(errno));

    net_close(&(p->server_sock));
    timer_new((void *)p, "server_recon", p->conn_class->server_retry,
              TIMER_FUNCTION(_ircserver_reconnect), (void *)0);

    free(p->serverpassword);
    p->serverpassword = 0;

  } else {
    p->server_status |= IRC_SERVER_CREATED;
    net_hook(p->server_sock, SOCK_CONNECTING, (void *)p,
             ACTIVITY_FUNCTION(_ircserver_connected),
             ERROR_FUNCTION(_ircserver_connectfailed));
    debug("Connection in progress");
  }
}

/* Called when a new server has connected */
static void _ircserver_connected(struct ircproxy *p, int sock) {
  if (sock != p->server_sock) {
    error("Unexpected socket %d in _ircserver_connected, expected %d", sock,
          p->server_sock);
    net_close(&sock);
    return;
  }

  debug("Connection succeeded");
  p->server_status |= IRC_SERVER_CONNECTED;
  net_hook(p->server_sock, SOCK_NORMAL, (void *)p,
           ACTIVITY_FUNCTION(_ircserver_data),
           ERROR_FUNCTION(_ircserver_error));
  if (p->conn_class->server_throttle)
    net_throttle(p->server_sock, p->conn_class->server_throttle[0], 
                 p->conn_class->server_throttle[1]);

  if (IS_CLIENT_READY(p))
    ircclient_send_notice(p, "Connected to server");

  irclog_log(p, IRC_LOG_SERVER, IRC_LOGFILE_SERVER, PACKAGE,
             "Connected to server: %s", p->servername);

  /* Need to try and look up our local hostname now we have a socket to
     somewhere that will tell us */
  if (!p->hostname) {
    SOCKADDR sock_addr;
    char ip[40];
    int len;

    len = sizeof(struct sockaddr_in);
    if (!getsockname(p->server_sock, (struct sockaddr *)&sock_addr, &len)) {
      net_ntop(&sock_addr, ip, sizeof(ip));
      dns_hostfromaddr((void *)p, 0, ip,
                       (dns_fun_t) _ircserver_connected2);
      return;
    } else {
      syscall_fail("getsockname", "", 0);
      _ircserver_connected2(p, 0, 0, 0);
    }
  } else {
    _ircserver_connected2(p, 0, 0, 0);
  }
}

/* Called when a new server has connected and we have a local hostname */
static void _ircserver_connected2(struct ircproxy *p, void *data,
                                  const char *ip, const char *name) {
  char *username;

  if (!p->hostname && name)
    p->hostname = x_strdup(name);

  username = ircprot_sanitize_username(p->username);
  if (p->serverpassword) {
    ircserver_send_command(p, "PASS", ":%s", p->serverpassword);
    free(p->serverpassword);
    p->serverpassword = 0;
  }
  
  /* When connecting to a server, may as well try to start fresh, eh? */
  if (p->setnickname && strcmp(p->nickname, p->setnickname)) {
    free(p->nickname);
    p->nickname = x_strdup(p->setnickname);
  }
  ircserver_send_command(p, "NICK", ":%s", p->nickname);
  ircserver_send_command(p, "USER", "%s 0 * :%s", username, p->realname);
  p->server_status |= IRC_SERVER_INTRODUCED;
  free(username);

  /* Begin stoned server checking */
  if (p->conn_class->server_pingtimeout) {
    timer_new((void *)p, "server_ping",
              (int)(p->conn_class->server_pingtimeout / 2),
              TIMER_FUNCTION(_ircserver_ping), (void *)0);
    timer_new((void *)p, "server_stoned", p->conn_class->server_pingtimeout,
              TIMER_FUNCTION(_ircserver_stoned), (void *)0);
  }

  /* Begin anti-idle */
  if (p->conn_class->idle_maxtime)
    timer_new((void *)p, "server_antiidle", p->conn_class->idle_maxtime,
              TIMER_FUNCTION(_ircserver_antiidle), (void *)0);
}

/* Called when a connection fails */
static void _ircserver_connectfailed(struct ircproxy *p, int sock, int bad) {
  if (sock != p->server_sock) {
    error("Unexpected socket %d in _ircserver_connectfailed, expected %d", sock,
          p->server_sock);
    net_close(&sock);
    return;
  }

  debug("Connection failed");

  if (IS_CLIENT_READY(p))
    ircclient_send_notice(p, "Connection failed: %s", strerror(errno));

  net_close(&(p->server_sock));
  p->server_status &= ~(IRC_SERVER_CREATED);

  timer_new((void *)p, "server_recon", p->conn_class->server_retry,
            TIMER_FUNCTION(_ircserver_reconnect), (void *)0);
}

/* Called when a server sends us stuff. */
static void _ircserver_data(struct ircproxy *p, int sock) {
  char *str;
  
  if (sock != p->server_sock) {
    error("Unexpected socket %d in _ircserver_data, expected %d", sock,
          p->server_sock);
    net_close(&sock);
    return;
  }

  str = 0;
  while (!p->dead && (p->server_status & IRC_SERVER_CONNECTED)
         && net_gets(p->server_sock, &str, "\r\n") > 0) {
    debug("<< '%s'", str);
    _ircserver_gotmsg(p, str);
    free(str);
  }
}

/* Called on server disconnection or error */
static void _ircserver_error(struct ircproxy *p, int sock, int bad) {
  if (sock != p->server_sock) {
    error("Unexpected socket %d in _ircserver_error, expected %d", sock,
          p->server_sock);
    net_close(&sock);
    return;
  }

  if (bad) {
    debug("Socket error");
  } else {
    debug("Server disconnected");
  }

  _ircserver_close(p);
}

/* Called when we get an irc protocol data from a server */
static int _ircserver_gotmsg(struct ircproxy *p, const char *str) {
  struct ircmessage msg;
  int squelch = 1;
  int important = 0;

  if (ircprot_parsemsg(str, &msg) == -1)
    return -1;

  /* Check source */
  if (!msg.src.orig) {
    msg.src.orig = x_strdup(p->servername);
    msg.src.fullname = x_strdup(p->servername);
    msg.src.name = x_strdup(p->servername);
  }
  
  /* 437 is bizarre, it either means Nickname is juped or Channel is juped */
  if (!irc_strcasecmp(msg.cmd, "437")) {
    if (msg.numparams >= 2) {
      if (!irc_strcasecmp(p->nickname, msg.params[1])) {
        /* Our nickname is Juped - make it a 433 */
        free(msg.cmd);
        msg.cmd = x_strdup("433");
      } else {
        /* Channel is juped - make it a 471 */
        free(msg.cmd);
        msg.cmd = x_strdup("471");
      }
    }
  }

  if (!irc_strcasecmp(msg.cmd, "001")) {
    /* Use 001 to get the servername */
    if (msg.src.type & IRC_SERVER) {
      free(p->servername);
      p->servername = x_strdup(msg.src.name);
    }

  } else if (!irc_strcasecmp(msg.cmd, "002")) {
    /* Ignore 002 */
  } else if (!irc_strcasecmp(msg.cmd, "003")) {
    /* Ignore 003 */
  } else if (!irc_strcasecmp(msg.cmd, "004")) {
    /* 004 contains all the juicy info, use it */
    if (msg.numparams >= 5) {
      free(p->servername);
      free(p->serverver);
      free(p->serverumodes);
      free(p->servercmodes);

      p->servername = x_strdup(msg.params[1]);
      p->serverver = x_strdup(msg.params[2]);
      p->serverumodes = x_strdup(msg.params[3]);
      p->servercmodes = x_strdup(msg.params[4]);

      p->server_status |= IRC_SERVER_GOTWELCOME | IRC_SERVER_SEEN;
      p->server_attempts = 0;

      if (IS_CLIENT_READY(p) && !(p->client_status & IRC_CLIENT_SENTWELCOME))
        ircclient_welcome(p);
    }

    /* Also use this numeric to send everything state-related to the client.
       From this moment on, we assume the server is happy. */

    /* Restore the user mode */
    if (p->modes)
      ircserver_send_command(p, "MODE", "%s +%s", p->nickname, p->modes);

    /* Restore the away message */
    if (p->awaymessage) {
      ircserver_send_command(p, "AWAY", ":%s", p->awaymessage);
    } else if (!(p->client_status & IRC_CLIENT_AUTHED)
               && p->conn_class->away_message) {
      ircserver_send_command(p, "AWAY", ":%s", p->conn_class->away_message);
    }

    /* Restore the channel list */
    if (p->channels) {
      struct ircchannel *c;

      c = p->channels;
      while (c) {
        if (!c->unjoined) {
          if (c->key) {
            ircserver_send_command(p, "JOIN", "%s :%s", c->name, c->key);
          } else {
            ircserver_send_command(p, "JOIN", ":%s", c->name);
          }
        }
        c = c->next;
      }
    }

  } else if (!irc_strcasecmp(msg.cmd, "005")) {
    char *c0 = x_strdup(msg.params[1]), *c1, *c2;
    int i = 0;

    squelch = 0;

    c1 = strchr(c0, ',');
    if (!c1) {
      i = 1;
    } else {
      *(c1++) = 0;
      c2 = strrchr(c1, ' ');
      if (!c2) {
	i = 1;
      } else {
	*(c2++) = 0;
	c1 = strrchr(c0, ' ');
	if (!c1) {
	  i = 1;
	} else {
	  *(c1++) = 0;
	}
      }
    }
    free(c0);
     
    if (i) {
      // Store for future clients
      struct strlist *s = (struct strlist *)malloc(sizeof(struct strlist));
      s->str = x_strdup(msg.paramstarts[1]);
      s->next = 0;
      if (p->serversupported) {
        struct strlist *ss;
        for (ss = p->serversupported; ss->next && strcmp(ss->str,s->str); ss = ss->next)
        ;
        if (strcmp(ss->str,s->str))  // this line is not already present
          ss->next = s;
        else {	      
	  free(s->str);
          free(s);
	}	 
      } else {
        p->serversupported = s;
      }
    } else {
      struct strlist *s;
      char *server;

      server = (char *)malloc(strlen(c1) + strlen(c2) + 2);
      server = x_sprintf("%s:%s", c1, c2);

      for (s = p->conn_class->servers; s; s = s->next) {
	if (!irc_strcasecmp(server, s->str)) {
	  break;
	}
      }

      if (!s && p->conn_class->allow_jump_new) {
	debug("New server because of a 005");

	s = (struct strlist *)malloc(sizeof(struct strlist));
	s->str = x_strdup(server);
	s->next = 0;

	if (p->conn_class->servers) {
	  struct strlist *ss;

	  for (ss = p->conn_class->servers; ss->next; ss = ss->next)
	    ;

	  ss->next = s;
	} else {
	  p->conn_class->servers = s;
	}
      }

      if (s && p->conn_class->allow_jump) {
	debug("Jumping to %s because of a 005", s->str);

	if (IS_CLIENT_READY(p)) {
	  ircclient_send_notice(p, "Got redirected to server %s", s->str);
	}
	irclog_log(p, IRC_LOG_SERVER, IRC_LOGFILE_SERVER, PACKAGE,
		   "Got redirected to server %s by %s", s->str, msg.src.name);

	p->conn_class->next_server = s;
	ircserver_connectagain(p);
      }
    }
  } else if (!irc_strcasecmp(msg.cmd, "375")) {
    /* Ignore 375 unless allow_motd */
    if (p->allow_motd)
      squelch = 0;

  } else if (!irc_strcasecmp(msg.cmd, "372")) {
    /* Ignore 372 unless allow_motd */
    if (p->allow_motd)
      squelch = 0;

  } else if (!irc_strcasecmp(msg.cmd, "376")) {
    /* Ignore 376 unless allow_motd */
    if (p->allow_motd) {
      squelch = 0;
      p->allow_motd = 0;
    }

  } else if (!irc_strcasecmp(msg.cmd, "422")) {
    /* Ignore 422 unless allow_motd */
    if (p->allow_motd) {
      squelch = 0;
      p->allow_motd = 0;
    }
    
  } else if (!irc_strcasecmp(msg.cmd, "431") || !irc_strcasecmp(msg.cmd, "432")
             || !irc_strcasecmp(msg.cmd, "433")
             || !irc_strcasecmp(msg.cmd, "436")
             || !irc_strcasecmp(msg.cmd, "438")) {
    /* Our nickname got rejected.  Don't update setnickname! */
    if (msg.numparams >= 2) {
      /* Fall back on our original if we can */
      if (strlen(msg.params[0]) && strcmp(msg.params[0], "*")) {
        if (p->client_status == IRC_CLIENT_ACTIVE)
          ircclient_send_selfcmd(p, "NICK", ":%s", msg.params[0]);
        ircclient_nick_changed(p, msg.params[0]);
        ircclient_checknickname(p);
        squelch = 0;
      } else {
        /* We don't have a nickname anymore.  Don't free it, so we've
           still really got the old one lying around. */
        p->client_status &= ~(IRC_CLIENT_GOTNICK);

        /* If we don't have a client connected, then we have to regenerate
           a new nickname ourselves... Otherwise we can just let the client
           do it */
        if (!(p->client_status & IRC_CLIENT_CONNECTED)) {
          ircclient_generate_nick(p, msg.params[1]);
        } else {
          /* Have to anti-squelch this manually */
          net_send(p->client_sock, "%s\r\n", msg.orig);
        }
      }
    } else {
      squelch = 0;
    }

  } else if (!irc_strcasecmp(msg.cmd, "471") || !irc_strcasecmp(msg.cmd, "473")
             || !irc_strcasecmp(msg.cmd, "474")) {
    if (msg.numparams >= 2) {
      /* Can't join a channel */

      /* No client connected?  Lets rejoin it for it */
      if (p->client_status != IRC_CLIENT_ACTIVE) {
        struct ircchannel *chan;

        chan = ircnet_fetchchannel(p, msg.params[1]);
        if (chan) {
          chan->inactive = 1;
          ircnet_rejoin(p, chan->name);
        }
      } else {
        /* Let it handle it */
        ircnet_delchannel(p, msg.params[1]);
      }

      squelch = 0;
    }

  } else if (!irc_strcasecmp(msg.cmd, "403") || !irc_strcasecmp(msg.cmd, "475")
             || !irc_strcasecmp(msg.cmd, "476")
             || !irc_strcasecmp(msg.cmd, "405")) {
    if (msg.numparams >= 2) {
      struct ircchannel *c;

      /* Can't join a channel, permanent error */
      c = ircnet_fetchchannel(p, msg.params[1]);
      if (c) {
        /* No client connected?  Better notify it */
        if (p->client_status != IRC_CLIENT_ACTIVE) {
          if (msg.numparams >= 3) {
            irclog_log(p, IRC_LOG_ERROR, IRC_LOGFILE_SERVER, PACKAGE,
                       "Couldn't rejoin %s: %s (%s)",
                       msg.params[1], msg.params[2], msg.cmd);
          } else {
            irclog_log(p, IRC_LOG_ERROR, IRC_LOGFILE_SERVER, PACKAGE,
                       "Couldn't rejoin %s (%s)",
                       msg.params[1], msg.cmd);
          }

          /* Set it to an unjoined channel until the client comes back */
          c->unjoined = 1;
        } else {
          /* Client connected, so we really can't join it - delete it */
          ircnet_delchannel(p, msg.params[1]);
        }
      }

      squelch = 0;
    }

  } else if (!irc_strcasecmp(msg.cmd, "411")) {
    /* Ignore 411 if squelch_411 */
    if (p->squelch_411) {
      p->squelch_411 = 0;
    } else {
      squelch = 0;
    }

  } else if (!irc_strcasecmp(msg.cmd, "324")) {
    /* Here be channel modes */
    if (msg.numparams >= 2) {
      struct ircchannel *c;

      /* Set this to 1 in a minute if we need to */
      squelch = 0;

      c = ircnet_fetchchannel(p, msg.params[1]);
      if (c) {
        if (msg.numparams >= 3) {
          ircnet_channel_mode(p, c, &msg, 2);
        } else {
          free(c->key);
        }

        /* Look for the channel in the squelch_modes list */
        if (p->squelch_modes) {
          struct strlist *s, *l;

          l = 0;
          s = p->squelch_modes;

          while (s) {
            if (!irc_strcasecmp(msg.params[1], s->str)) {
              struct strlist *n;

              n = s->next;
              free(s->str);
              free(s);

              /* Was in the squelch list, so remove it and stop looking */
	      if (l) l->next = n; else p->squelch_modes = n;
	      s = n;
              squelch = 1;
              break;
            } else {
              l = s;
              s = s->next;
            }
          }
        }
      }
    }
 
  } else if (!irc_strcasecmp(msg.cmd, "477")) {
    /* No channel modes for this channel */
    if (msg.numparams >= 2) {
      struct ircchannel *c;

      /* Set this to 1 in a minute if we need to */
      squelch = 0;

      c = ircnet_fetchchannel(p, msg.params[1]);
      if (c) {
        debug("No channel modes for %s", c->name);
        free(c->key);

        /* Look for the channel in the squelch_modes list */
        if (p->squelch_modes) {
          struct strlist *s, *l;

          l = 0;
          s = p->squelch_modes;

          while (s) {
            if (!irc_strcasecmp(msg.params[1], s->str)) {
              struct strlist *n;

              n = s->next;
              free(s->str);
              free(s);

              /* Was in the squelch list, so remove it and stop looking */
	      if (l) l->next = n; else p->squelch_modes = n;
	      s = n;
              squelch = 1;
              break;
            } else {
              l = s;
              s = s->next;
            }
          }
        }
      }
    }
 
  } else if (!irc_strcasecmp(msg.cmd, "PING")) {
    /* Reply to pings for the client */
    if (msg.numparams == 1) {
      net_sendurgent(p->server_sock, "PONG :%s\r\n", msg.params[0]);
      debug("=> 'PONG :%s'", msg.params[0]);
    } else if (msg.numparams >= 2) {
      net_sendurgent(p->server_sock, "PONG %s :%s\r\n",
                     msg.params[0], msg.params[1]);
      debug("=> 'PONG %s :%s'", msg.params[0], msg.params[1]);
    }

    /* but let it see them */
    squelch = 0;

  } else if (!irc_strcasecmp(msg.cmd, "PONG")) {
    /* Use pongs to reset the server_stoned timer */
    if (p->allow_pong)
      squelch = 0;

    if (p->conn_class->server_pingtimeout) {
      timer_del((void *)p, "server_stoned");
      timer_new((void *)p, "server_stoned", p->conn_class->server_pingtimeout,
                TIMER_FUNCTION(_ircserver_stoned), (void *)0);
      p->allow_pong = 0;
    }

  } else if (!irc_strcasecmp(msg.cmd, "NICK")) {
    if (_ircserver_forclient(p, &msg)) {
      /* Server telling us our nickname */
      if (msg.numparams >= 1) {
        if (strcmp(p->nickname, msg.params[0])) {
          if (IS_CLIENT_READY(p))
            ircclient_send_selfcmd(p, "NICK", ":%s", msg.params[0]);

          ircclient_nick_changed(p, msg.params[0]);
          irclog_log(p, IRC_LOG_NICK, IRC_LOGFILE_SERVER, p->servername,
                     "You changed your nickname to %s", msg.params[0]);
        }

        /* Is this as a result of a client NICK command? */
        if (p->expecting_nick) {
          ircclient_setnickname(p);
          p->expecting_nick = 0;
        }

        ircclient_checknickname(p);
      }
    } else {
      /* Someone changing their nickname */
      if (msg.numparams >= 1) {
        irclog_log(p, IRC_LOG_NICK, IRC_LOGFILE_SERVER, p->servername,
                   "%s changed nickname to %s",
                   msg.src.fullname, msg.params[0]);
      }
      squelch = 0;
    }

  } else if (!irc_strcasecmp(msg.cmd, "MODE")) {
    if (msg.numparams >= 2) {
      struct ircchannel *c;

      if (!irc_strcasecmp(p->nickname, msg.params[0])) {
        /* Personal mode change */
        int param;

        irclog_log(p, IRC_LOG_MODE, IRC_LOGFILE_SERVER, p->servername,
                   "Your mode was changed: %s", msg.paramstarts[1]);

        for (param = 1; param < msg.numparams; param++)
          ircclient_change_mode(p, msg.params[param]);

        /* Check for refuse modes */
        if (p->modes && p->conn_class->refuse_modes &&
            (strcspn(p->modes, p->conn_class->refuse_modes)
             != strlen(p->modes))) {
          char *mode;

          debug("Got refusal mode from server");
          ircserver_send_command(p, "QUIT", ":Don't like this server - %s %s",
                                 PACKAGE, VERSION);

          mode = x_sprintf("-%s", p->conn_class->refuse_modes);
          debug("Auto-mode-change '%s'", mode);
          ircclient_change_mode(p, mode);
          free(mode);

          _ircserver_close(p);
        }
      } else if ((c = ircnet_fetchchannel(p, msg.params[0]))) {
        /* Channel mode change */
        ircnet_channel_mode(p, c, &msg, 1);

        irclog_log(p, IRC_LOG_MODE, c->name, p->servername,
                   "%s changed mode: %s", msg.src.fullname, msg.paramstarts[1]);
      }

      squelch = 0;
    }

  } else if (!irc_strcasecmp(msg.cmd, "TOPIC")) {
    if (msg.numparams >= 2) {
      struct ircchannel *c;

      /* Channel topic change */
      c = ircnet_fetchchannel(p, msg.params[0]);
      irclog_log(p, IRC_LOG_TOPIC, c->name, p->servername,
                 "%s changed topic: %s", msg.src.fullname, msg.paramstarts[1]);

      squelch = 0;
    }

  } else if (!irc_strcasecmp(msg.cmd, "JOIN")) {
    if (_ircserver_forclient(p, &msg)) {
      /* Server telling us we joined a channel */
      if (msg.numparams >= 1) {
        struct ircchannel *c;

        c = ircnet_fetchchannel(p, msg.params[0]);
        if (c && c->inactive) {
          /* Must have got KICK'd or something ... */
          c->inactive = 0;

          /* If a client is connected, tell it we just joined and give it
             what they missed */
          if (p->client_status == IRC_CLIENT_ACTIVE) {
            net_send(p->client_sock, "%s\r\n", msg.orig);
            if (p->conn_class->chan_log_enabled)
              irclog_autorecall(p, msg.params[0]);
          }
        } else if (c && c->unjoined) {
          /* Ah, rejoined a channel we left */
          c->unjoined = 0;
          squelch = 0;
        } else if (!c) {
          struct strlist *s;

          /* Orginary join */
          ircnet_addchannel(p, msg.params[0]);

          /* Ask for the channel modes */
          s = (struct strlist *)malloc(sizeof(struct strlist));
          s->str = x_strdup(msg.params[0]);
          s->next = p->squelch_modes;
          p->squelch_modes = s;

          ircserver_send_command(p, "MODE", ":%s", msg.params[0]);
          squelch = 0;
        } else {
          /* Bizarre, joined a channel we thought we were already on */
          squelch = 0;
        }

        if ((p->client_status != IRC_CLIENT_ACTIVE)
            && (p->conn_class->detach_message)) {
          int slashme;
          char *msg;

          msg = p->conn_class->detach_message;
          if ((strlen(msg) >= 5) && !strncasecmp(msg, "/me ", 4)) {
            /* Starts with /me */
            slashme = 1;
            msg += 4;
          } else {
            slashme = 0;
          }

          if (slashme) {
            ircserver_send_command(p, "PRIVMSG", "%s :\001ACTION %s\001",
                                   c->name, msg);
          } else {
            ircserver_send_command(p, "PRIVMSG", "%s :%s", c->name, msg);
          }
        }

        irclog_log(p, IRC_LOG_JOIN, msg.params[0], p->servername,
                   "You joined the channel");
      }
    } else {
      if (msg.numparams >= 1) {
        irclog_log(p, IRC_LOG_JOIN, msg.params[0], p->servername,
                   "%s joined the channel", msg.src.fullname);
      }
      squelch = 0;
    }

  } else if (!irc_strcasecmp(msg.cmd, "PART")) {
    if (_ircserver_forclient(p, &msg)) {
      /* Server telling us we left a channel */
      if (msg.numparams >= 1) {
        struct ircchannel *c;

        irclog_log(p, IRC_LOG_PART, msg.params[0], p->servername,
                   "You left the channel");

        c = ircnet_fetchchannel(p, msg.params[0]);
        /* Ignore server PARTs for unjoined channels */
        if (c && !c->unjoined)
          ircnet_delchannel(p, msg.params[0]);
        squelch = 0;
      }
    } else {
      if (msg.numparams >= 1) {
        irclog_log(p, IRC_LOG_PART, msg.params[0], p->servername,
                   "%s left the channel", msg.src.fullname);
      }
      squelch = 0;
    }

  } else if (!irc_strcasecmp(msg.cmd, "KICK")) {
    if (msg.numparams >= 2) {
      if (!irc_strcasecmp(p->nickname, msg.params[1])) {
        /* We got kicked off a channel */

        if (msg.numparams >= 3) {
          irclog_log(p, IRC_LOG_KICK, msg.params[0], p->servername,
                     "Kicked off by %s: %s", msg.src.fullname, msg.params[2]);
        } else {
          irclog_log(p, IRC_LOG_KICK, msg.params[0], p->servername,
                     "Kicked off by %s", msg.src.fullname);
        }

        /* No client connected?  Lets rejoin it for it */
        if (p->client_status != IRC_CLIENT_ACTIVE) {
          struct ircchannel *chan;

          chan = ircnet_fetchchannel(p, msg.params[0]);
          if (chan) {
            chan->inactive = 1;
            ircnet_rejoin(p, chan->name);
          }
        } else {
          /* Let it handle it */
          ircnet_delchannel(p, msg.params[0]);
        }

        squelch = 0;
      } else {
        squelch = 0;

        if (msg.numparams >= 3) {
          irclog_log(p, IRC_LOG_KICK, msg.params[0], p->servername,
                     "%s kicked off by %s: %s", msg.params[1],
                     msg.src.fullname, msg.params[2]);
        } else {
          irclog_log(p, IRC_LOG_KICK, msg.params[0], p->servername,
                     "%s kicked off by %s", msg.params[1], msg.src.fullname);
        }
      }
    }

  } else if (!irc_strcasecmp(msg.cmd, "QUIT")) {
    /* Somebody left IRC */
    if (msg.numparams >= 1) {
      irclog_log(p, IRC_LOG_QUIT, IRC_LOGFILE_SERVER, p->servername,
                 "%s quit from IRC: %s", msg.src.fullname, msg.params[0]);
    } else {
      irclog_log(p, IRC_LOG_QUIT, IRC_LOGFILE_SERVER, p->servername,
                 "%s quit from IRC", msg.src.fullname);
    }

    squelch = 0;

  } else if (!irc_strcasecmp(msg.cmd, "ERROR")) {
    /* Errors are important enough to always forward to the client */
    important = 1;
    squelch = 0;

  } else if (!irc_strcasecmp(msg.cmd, "PRIVMSG")) {
    /* All PRIVMSGs go to the client unless we fiddle */
    squelch = 0;

    if (msg.numparams >= 2) {
      struct ircchannel *c;
      struct strlist *list, *s;
      char *str, *logdest;

      ircprot_stripctcp(msg.params[1], &str, &list);

      /* Channel text has to go to the log of the destination, but private
       * messages go to the log of the source */
      c = ircnet_fetchchannel(p, msg.params[0]);
      logdest = (c ? msg.params[0] : msg.src.name);

      /* Privmsgs get logged */
      if (str && strlen(str))
        irclog_log(p, IRC_LOG_MSG, logdest, msg.src.orig, "%s", str);
      free(str);

      /* Handle CTCP */
      str = x_strdup(msg.params[1]);
      s = list;
      while (s) {
        struct ctcpmessage cmsg;
        struct strlist *n;
        char *unquoted;
        int r;
	struct dcc_resume *currptr;
	 
	 
        n = s->next;
        r = ircprot_parsectcp(s->str, &cmsg);
        unquoted = s->str;
        free(s);
        s = n;
        if (r == -1) {
          free(unquoted);
          continue;
        }
      
        if (!strcmp(cmsg.cmd, "ACTION")) {
          irclog_log(p, IRC_LOG_ACTION, logdest, msg.src.orig,
                     "%s", (cmsg.paramstarts != NULL) ?  cmsg.paramstarts[0]: "");

        } else if (!strcmp(cmsg.cmd, "DCC")
                   && p->conn_class->dcc_proxy_incoming) {
          struct sockaddr_in vis_addr;
          int len;

          /* We need our local address to do anything DCC related */
          len = sizeof(struct sockaddr_in);
          if ((p->client_status == IRC_CLIENT_ACTIVE) &&
              getsockname(p->client_sock, (struct sockaddr *)&vis_addr, &len)) {
	     syscall_fail("getsockname", "", 0);
	     
	  } else if ((cmsg.numparams >= 4)
		     && (!irc_strcasecmp(cmsg.params[0], "ACCEPT"))) {
	     
	     /* This means someone has accepted our RESUME request */
	     char *id;
	     struct dcc_resume *prevptr=NULL;
	     
	     id = malloc(strlen(msg.src.name)+strlen(cmsg.params[2])+2);
	     sprintf(id, "%s:%s", msg.src.name, cmsg.params[2]);
	     debug("Recieved ACCEPT message with id %s", id);
	     
	     for (currptr = dcc_resume_list; currptr; currptr = currptr->next) {
		if (!strcmp(currptr->id, id)) {
		   
		   /* Remove timer */
		   timer_del((void *)p, currptr->id);
		   
		   /* Make connection */
		   if (!dccnet_new(DCC_SEND_CAPTURE, p->conn_class->dcc_proxy_timeout,
				   p->conn_class->dcc_proxy_ports, p->conn_class->dcc_proxy_ports_sz,
				   &currptr->l_port, currptr->r_addr, currptr->r_port,
				   currptr->capfile, p->conn_class->dcc_capture_maxsize,
				   DCCN_FUNCTION(_ircserver_send_dccreject),
				   p, currptr->rejmsg, currptr->size)) {
		      if (p->conn_class->log_events & IRC_LOG_CTCP)
			irclog_log(p, IRC_LOG_NOTICE, p->servername, msg.src.fullname,
				      "Captured DCC SEND from %s into %s",
				      msg.src.fullname, currptr->capfile);
		   } else
		     _ircserver_send_dccreject(p, currptr->rejmsg, "");
		   /* Remove entry from list */
		   if (prevptr)
		     prevptr->next = currptr->next;
		   else
		     dcc_resume_list = NULL;
		   free(currptr->id);
		   free(currptr->capfile);
		   free(currptr->rejmsg);
		   free(currptr->fullname);
		   free(currptr);
		   
		   break;
		   
		}
		prevptr = currptr;
	     }
	     free(id);
	     
          } else if ((cmsg.numparams >= 4)
                     && (!irc_strcasecmp(cmsg.params[0], "CHAT")
                        || !irc_strcasecmp(cmsg.params[0], "SEND"))) {
            char *tmp, *ptr, *dccmsg, *rejmsg;
            struct in_addr l_addr, r_addr;
            int l_port, r_port, t_port;
            char *capfile = 0;
            char *rest = 0;
	    int type = 0;
	    unsigned short resume = 0;
	    struct stat file_stat; 
            
	     /* Find out what type of DCC request this is */
            if (!irc_strcasecmp(cmsg.params[0], "CHAT")) {
              /* Can only proxy chats if we have a client */
              if (p->client_status == IRC_CLIENT_ACTIVE)
                type = DCC_CHAT;

            } else if (!irc_strcasecmp(cmsg.params[0], "SEND")) {
              /* Check if we're capturing it, instead of proxying */
              if (p->conn_class->dcc_capture_directory
                  && ((p->client_status != IRC_CLIENT_ACTIVE)
                      || p->conn_class->dcc_capture_always))
              {
                char *file;

                /* Filename is after / or \ characters, this fixes any
                   security issues we might have with it */
                debug("Filename given '%s'", cmsg.params[1]);
                file = strrchr(cmsg.params[1], '/');
                if (file) {
                  char *ptr;

                  file++;
                  ptr = strrchr(file, '\\');
                  if (ptr)
                    file = ptr + 1;
                } else {
                  file = strrchr(cmsg.params[1], '\\');
                  if (file) {
                    file++;
                  } else {
                    file = cmsg.params[1];
                  }
                }
                debug("Filtered to '%s'", file);

                /* Assuming we got a filename ... */
                if (file && strlen(file)) {
                  type = DCC_SEND_CAPTURE;

                  if (p->conn_class->dcc_capture_withnick) {
                    capfile = x_sprintf("%s/%s.%s",
                                        p->conn_class->dcc_capture_directory,
                                        msg.src.name, file);
                  } else {
                    capfile = x_sprintf("%s/%s",
                                        p->conn_class->dcc_capture_directory,
                                        file);
                  }
                  debug("Capture to '%s'", capfile);
                }

              } else if (p->client_status == IRC_CLIENT_ACTIVE) {
                /* Proxying - so client must be active.  See whether to
                   send it fast or normally */
                if (p->conn_class->dcc_send_fast) {
                  type = DCC_SEND_FAST;
                } else {
                  type = DCC_SEND_SIMPLE;
                }
              }
            }

            /* Check whether there's a tunnel port */
            t_port = 0;
            if (p->conn_class->dcc_tunnel_incoming)
              t_port = dns_portfromserv(p->conn_class->dcc_tunnel_incoming);

            /* Eww, host order, how the hell does this even work
               between machines of a different byte order? */
            if (!t_port) {
              r_addr.s_addr = strtoul(cmsg.params[2], (char **)NULL, 10);
              r_port = atoi(cmsg.params[3]);
            } else {
              r_addr.s_addr = INADDR_LOOPBACK;
              r_port = ntohs(t_port);
            }
            l_addr.s_addr = ntohl(vis_addr.sin_addr.s_addr);
            if (cmsg.numparams >= 5)
              rest = cmsg.paramstarts[4];

            /* Strip out this CTCP from the message, replacing it in
               a moment with dccmsg */
            tmp = x_sprintf("\001%s\001", unquoted);
            ptr = strstr(str, tmp);
            dccmsg = 0;

            /* Save this in case we need it later */
            rejmsg = x_sprintf(":%s NOTICE %s :\001DCC REJECT %s %s",
                               p->nickname, msg.src.name,
                               cmsg.params[0], cmsg.params[1]);

	    if (capfile) {
	       if (!stat(capfile, &file_stat)) {
		  resume = 1;
		  
		  debug("File exists resuming at %d", file_stat.st_size);
		  
		  /* Store parameters in linked list */
		  if (dcc_resume_list) {
		     for (currptr = dcc_resume_list; currptr->next; currptr = currptr->next);

		     currptr->next = malloc(sizeof(struct dcc_resume));
		     currptr = currptr->next;
		  } else {
		     dcc_resume_list = malloc(sizeof(struct dcc_resume));
		     currptr = dcc_resume_list;
		  }
		  
		  /* The Unique ID is Nick:Port */
		  currptr->id = malloc(strlen(msg.src.name)+strlen(cmsg.params[3])+2);
		  sprintf(currptr->id, "%s:%s", msg.src.name, cmsg.params[3]);
		  currptr->capfile = malloc(strlen(capfile)+1);
		  strcpy(currptr->capfile, capfile);
		  currptr->rejmsg = malloc(strlen(rejmsg)+1);
		  strcpy(currptr->rejmsg, rejmsg);
		  currptr->fullname = malloc(strlen(msg.src.fullname)+1);
		  strcpy(currptr->fullname, msg.src.fullname);
		  currptr->l_port = l_port;
		  currptr->r_port = r_port;
		  currptr->r_addr = r_addr;
		  currptr->size = file_stat.st_size;
		  currptr->next = NULL;
		  
		  /* Send RESUME request
		   net_send(p->server_sock, "PRIVMSG %s :\001DCC RESUME %s %s %d\001", msg.src.name,
		   cmsg.params[1], cmsg.params[3], file_stat.st_size); */
		  ircserver_send_command(p, "PRIVMSG", "%s :\001DCC RESUME %s %s %d\001", msg.src.name,
					 cmsg.params[1], cmsg.params[3], file_stat.st_size);
		  
		  /* Set timer */
		  timer_new((void *)p, currptr->id, p->conn_class->server_retry,
			    TIMER_FUNCTION(_ircserver_dccresume_timeout), currptr);
	       }
	    }
	     
	    if (!resume) {
		
		/* Set up a dcc proxy, note: type is 0 if there isn't a client
		 * active and we're not capturing it.  This will send a reject
		 * back which is exactly what we want to do. */
		if (ptr && type
		    && !dccnet_new(type, p->conn_class->dcc_proxy_timeout,
				   p->conn_class->dcc_proxy_ports,
				   p->conn_class->dcc_proxy_ports_sz,
				   &l_port, r_addr, r_port,
				   capfile, p->conn_class->dcc_capture_maxsize,
				   DCCN_FUNCTION(_ircserver_send_dccreject),
				   p, rejmsg, 0)) {		   
		   if (capfile) {		      
		      if (p->conn_class->log_events & IRC_LOG_CTCP)
			irclog_log(p, IRC_LOG_NOTICE, msg.params[0], p->servername,
				      "Captured DCC %s from %s into %s",
				      cmsg.params[0], msg.src.fullname, capfile);
		   } else { 
		      dccmsg = x_sprintf("\001DCC %s %s %lu %u%s%s\001",
					 cmsg.params[0], cmsg.params[1],
					 l_addr.s_addr, l_port,
					 (rest ? " " : ""), (rest ? rest : ""));		      
		      if (p->conn_class->log_events & IRC_LOG_CTCP)
			irclog_log(p, IRC_LOG_NOTICE, msg.params[0], p->servername,
				      "DCC %s Request from %s", cmsg.params[0],
				      msg.src.fullname);		      
		   }
		} else if (ptr) {
		   dccmsg = x_strdup("");
		   _ircserver_send_dccreject(p, rejmsg, "");
		}
	     }
	     
	     if (capfile)
	       dccmsg = x_strdup("");
	     
	     /* Don't need this anymore */
            free(rejmsg);

            /* Cut out the old CTCP and replace with dccmsg */
            if (ptr) {
              char *oldstr;

              *ptr = 0;
              ptr += strlen(tmp);

              oldstr = str;
              str = x_sprintf("%s%s%s", oldstr, dccmsg, ptr);

              free(oldstr);
              free(dccmsg);
            }

            free(tmp);
            if (capfile)
              free(capfile);

          } else {
            /* Unknown DCC */
            debug("Unknown or Unimplemented DCC request - %s",
                  cmsg.params[0]);
          }

        } else if (!strcmp(cmsg.cmd, "PING")
                  && p->conn_class->ctcp_replies
                  && (p->client_status != IRC_CLIENT_ACTIVE)) {
          if (cmsg.numparams >= 1) {
            ircserver_send_command(p, "NOTICE", "%s :\001PING %s\001",
                                   msg.src.name, cmsg.paramstarts[0]);
          } else {
            ircserver_send_command(p, "NOTICE", "%s :\001PING\001",
                                   msg.src.name);
          }

        } else if (!strcmp(cmsg.cmd, "ECHO")
                  && p->conn_class->ctcp_replies
                  && (p->client_status != IRC_CLIENT_ACTIVE)) {
          if (cmsg.numparams >= 1)
            ircserver_send_command(p, "NOTICE", "%s :\001ECHO %s\001",
                                   msg.src.name, cmsg.paramstarts[0]);

        } else if (!strcmp(cmsg.cmd, "TIME")
                  && p->conn_class->ctcp_replies
                  && (p->client_status != IRC_CLIENT_ACTIVE)) {
          char tbuf[40];
          time_t now;

          time(&now);
          strftime(tbuf, sizeof(tbuf), CTCP_TIMEDATE_FORMAT, localtime(&now));
          ircserver_send_command(p, "NOTICE", "%s :\001TIME %s\001",
                                 msg.src.name, tbuf);

        } else if (!strcmp(cmsg.cmd, "CLIENTINFO")
                  && p->conn_class->ctcp_replies
                  && (p->client_status != IRC_CLIENT_ACTIVE)) {
          ircserver_send_command(p, "NOTICE", "%s :\001CLIENTINFO %s\001",
                                 msg.src.name,
                                 "ACTION DCC VERSION CLIENTINFO USERINFO "
                                 "FINGER PING TIME ECHO");

        } else if (!strcmp(cmsg.cmd, "VERSION")
                  && p->conn_class->ctcp_replies
                  && (p->client_status != IRC_CLIENT_ACTIVE)) {
          ircserver_send_command(p, "NOTICE", "%s :\001VERSION %s %s - %s\001",
                                 msg.src.name, PACKAGE, VERSION,
                                 "http://dircproxy.securiweb.net/");

        } else if (!strcmp(cmsg.cmd, "USERINFO")
                  && p->conn_class->ctcp_replies
                  && (p->client_status != IRC_CLIENT_ACTIVE)) {
          ircserver_send_command(p, "NOTICE", "%s :\001USERINFO %s -- %s\001",
                                 msg.src.name, PACKAGE, "Saving the world from "
                                 "mutant carrots since 1899!");

        } else if (!strcmp(cmsg.cmd, "FINGER")
                  && p->conn_class->ctcp_replies
                  && (p->client_status != IRC_CLIENT_ACTIVE)) {
          ircserver_send_command(p, "NOTICE", "%s :\001FINGER %s %s\001",
                                 msg.src.name, PACKAGE,
                                 "proxying for unconnected client");
        }

        /* Don't log DCC or ACTION twice :) */
        if (strcmp(cmsg.cmd, "DCC") && strcmp(cmsg.cmd, "ACTION")) {
          irclog_log(p, IRC_LOG_CTCP, logdest, msg.src.orig,
                     "Received CTCP %s", cmsg.cmd);
        }

        ircprot_freectcp(&cmsg);
        free(unquoted);
      }

      /* Send str */
      if (strlen(str) && (p->client_status == IRC_CLIENT_ACTIVE))
        net_send(p->client_sock, ":%s PRIVMSG %s :%s\r\n",
                 msg.src.orig, msg.params[0], str);
      squelch = 1;
      free(str);
    }

  } else if (!irc_strcasecmp(msg.cmd, "NOTICE")) {
    if (msg.numparams >= 1) {
      struct ircchannel *c;
      struct strlist *list;
      char *str, *logdest;

      ircprot_stripctcp(msg.params[1], &str, &list);
      
      /* Channel text has to go to the log of the destination, but private
       * messages go to the log of the source */
      c = ircnet_fetchchannel(p, msg.params[0]);
      logdest = (c ? msg.params[0] : msg.src.name);

      if (str && strlen(str))
        irclog_log(p, IRC_LOG_NOTICE, logdest, msg.src.orig, "%s", str);
      free(str);

      if (list) {
        struct strlist *s;

        s = list;
        while (s) {
          struct ctcpmessage cmsg;
          struct strlist *n;
          int r;

          n = s->next;
          r = ircprot_parsectcp(s->str, &cmsg);
          free(s->str);
          free(s);
          s = n;
          if (r == -1)
            continue;
         
          if (cmsg.numparams >= 1) {
            irclog_log(p, IRC_LOG_CTCP, logdest, msg.src.orig,
                       "Received CTCP %s Reply: %s",
                       cmsg.cmd, cmsg.paramstarts[0]);
          } else {
            irclog_log(p, IRC_LOG_CTCP, logdest, msg.src.orig,
                       "Received CTCP %s Reply",
                       cmsg.cmd);
          }

          ircprot_freectcp(&cmsg);
        }
      }
    }

    /* All NOTICEs go to the client */
    squelch = 0;

  } else {
    squelch = 0;
  }

  if (!squelch 
      && ((p->client_status == IRC_CLIENT_ACTIVE)
          || (important && (p->client_status & IRC_CLIENT_CONNECTED)))) {
    net_send(p->client_sock, "%s\r\n", msg.orig);
  }

  ircprot_freemsg(&msg);
  return 0;
}

/* Close the server socket itself */
int ircserver_close_sock(struct ircproxy *p) {
  net_close(&(p->server_sock));
  p->server_status &= ~(IRC_SERVER_CREATED | IRC_SERVER_CONNECTED
                        | IRC_SERVER_INTRODUCED | IRC_SERVER_GOTWELCOME);

  /* Make sure these don't get triggered */
  timer_del((void *)p, "server_ping");
  timer_del((void *)p, "server_stoned");
  timer_del((void *)p, "server_antiidle");
  timer_del((void *)p, "server_recon");

  return 0;
}

/* Close the connection to the server */
static int _ircserver_close(struct ircproxy *p) {
  ircserver_close_sock(p);

  if (IS_CLIENT_READY(p)) {
    ircclient_send_notice(p, "Lost connection to server");
    _ircserver_lost(p);
  }

  irclog_log(p, IRC_LOG_SERVER, IRC_LOGFILE_SERVER, PACKAGE,
             "Lost connection to server: %s", p->servername);

  timer_new((void *)p, "server_recon", p->conn_class->server_retry,
            TIMER_FUNCTION(_ircserver_reconnect), (void *)0);

  return 0;
}

/* Lost the connection to the server while client is active, so send it
   PARTs so it doesn't get confused by later JOINs */
static int _ircserver_lost(struct ircproxy *p) {
  struct ircchannel *c;

  if (IS_CLIENT_READY(p)) {
    c = p->channels;
    while (c) {
      ircclient_send_selfcmd(p, "PART", ":%s", c->name);
      c = c->next;
    }
  }

  return 0;
}

/* Drop the connection to the server and reconnect */
int ircserver_connectagain(struct ircproxy *p) {
  if (IS_SERVER_READY(p)) {
    if (IS_CLIENT_READY(p)) {
      ircclient_send_notice(p, "Dropped connnection to server");
      irclog_log(p, IRC_LOG_SERVER, IRC_LOGFILE_SERVER, PACKAGE,
                 "Dropped connection to server: %s", p->servername);
      _ircserver_lost(p);
    }

    ircserver_send_command(p, "QUIT", ":Reconnecting to server - %s %s",
                           PACKAGE, VERSION);
  }
  if (p->server_status & IRC_SERVER_CREATED)
    ircserver_close_sock(p);

  /* Reset seen so that we start with initattempts again */
  p->server_status &= ~(IRC_SERVER_SEEN);
  p->server_attempts = 0;

  debug("Connecting again");
  ircserver_connect(p);

  return 0;
}

/* hook for timer code to ping server */
static void _ircserver_ping(struct ircproxy *p, void *data) {
  /* Server might not be ready yet 8*/
  if (IS_SERVER_READY(p)) {
    debug("Pinging the server");
    net_sendurgent(p->server_sock, "PING :%s\r\n", p->servername);
    debug("=> 'PING :%s'", p->servername);
  }

  timer_new((void *)p, "server_ping",
            (int)(p->conn_class->server_pingtimeout / 2),
            TIMER_FUNCTION(_ircserver_ping), (void *)0);
}

/* hook for timer code to close a stoned server */
static void _ircserver_stoned(struct ircproxy *p, void *data) {
  /* Server is, like, stoned.  Yeah man! */
  if (IS_SERVER_READY(p)) {
    debug("Server is stoned, reconnecting");
    ircserver_send_command(p, "QUIT", ":Getting off stoned server - %s %s",
                           PACKAGE, VERSION);
    _ircserver_close(p);
  }
}

/* hook for timer code to send empty privmsg to prevent idling */
static void _ircserver_antiidle(struct ircproxy *p, void *data) {
  if (IS_SERVER_READY(p)) {
    debug("Sending anti-idle");
    p->squelch_411 = 1;
    ircserver_send_command(p, "PRIVMSG", "");
  }

  timer_new((void *)p, "server_antiidle", p->conn_class->idle_maxtime,
            TIMER_FUNCTION(_ircserver_antiidle), (void *)0);
}

/* Reset idle timer */
void ircserver_resetidle(struct ircproxy *p) {
  timer_del((void *)p, "server_antiidle");
  timer_new((void *)p, "server_antiidle", p->conn_class->idle_maxtime,
            TIMER_FUNCTION(_ircserver_antiidle), (void *)0);
}

/* Check if a message is bound for us, and if so check our username and
   hostname vars are the same */
static int _ircserver_forclient(struct ircproxy *p, struct ircmessage *msg) {
  if (!(msg->src.type & IRC_USER))
    return 0;

  if (irc_strcasecmp(p->nickname, msg->src.name))
    return 0;

  if (msg->src.username) {
    free(p->username);

    p->username = x_strdup(msg->src.username);
  }

  if (msg->src.hostname) {
    free(p->hostname);

    p->hostname = x_strdup(msg->src.hostname);
  }

  return 1;
}

/* send a command to the server with no prefix */
int ircserver_send_command(struct ircproxy *p, const char *command, 
                                   const char *format, ...) {
  va_list ap;
  char *msg;
  int ret;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  ret = net_send(p->server_sock, "%s %s\r\n", command, msg);
  debug("-> '%s %s'", command, msg);

  free(msg);
  return ret;
}

/* Send a DCC reject message */
static int _ircserver_send_dccreject(struct ircproxy *p, const char *msg,
                                     const char *reason) {
  int ret = 1;

  if (p && p->conn_class && p->conn_class->dcc_proxy_sendreject &&
      (p->server_status == IRC_SERVER_ACTIVE)) {
    if (reason) {
      ret = net_send(p->server_sock, "%s (%s: %s)\001\r\n", msg,
                     PACKAGE, reason);
      debug("-> '%s (%s: %s)\001'", msg, PACKAGE, reason);
    } else {
      ret = net_send(p->server_sock, "%s\001\r\n", msg);
      debug("-> '%s\001'", msg);
    }
  }

  return ret;
}

static int _ircserver_dccresume_timeout(struct ircproxy *p, struct dcc_resume *node)
{
   struct dcc_resume *prevptr;
   struct stat file_stat;
   unsigned short counter = 1;
   char *newfile;
   int new, old, bytesread;
   char buffer[1024];
   
   timer_del((void *)p, node->id);
   
   debug("DCC Resume ID %s timed out", node->id);
   
   /* Save file to a new name */
   newfile = malloc(strlen(node->capfile)+7);
   do {   
      sprintf(newfile, "%s.%d", node->capfile, counter);
      if (stat(newfile, &file_stat)) {
	 /* File doesn't exist save as this */
	 debug("Saving %s to %s", node->capfile, newfile);
	 strcpy(node->capfile, newfile);
	 break;	 
      }
      counter++;     
   } while (1);
   
   free(newfile);
   
   /* Make connection anyway (Just means we can't resume) */
   if (!dccnet_new(DCC_SEND_CAPTURE, p->conn_class->dcc_proxy_timeout,
		   p->conn_class->dcc_proxy_ports, p->conn_class->dcc_proxy_ports_sz,
		   &node->l_port, node->r_addr, node->r_port,
		   node->capfile, p->conn_class->dcc_capture_maxsize,
		   DCCN_FUNCTION(_ircserver_send_dccreject), p, node->rejmsg, 0)) {  
      if (p->conn_class->log_events & IRC_LOG_CTCP)
	irclog_log(p, IRC_LOG_NOTICE, p->servername, node->fullname, 
		      "Captured DCC SEND from %s into %s", node->fullname, node->capfile);      
   } else
     _ircserver_send_dccreject(p, node->rejmsg, "");
   /* Remove entry from list */
   if (node != dcc_resume_list) {
      for (prevptr = dcc_resume_list; prevptr->next != node; prevptr = prevptr->next);
      prevptr->next = node->next;
   } else
     dcc_resume_list = NULL;
   free(node->id);
   free(node->capfile);
   free(node->rejmsg);
   free(node->fullname);
   free(node);
   
   return 0;
}
