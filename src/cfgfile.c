/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 *
 * cfgfile.c
 *  - reading of configuration file
 * --
 * @(#) $Id: cfgfile.c,v 1.47 2003/12/10 18:55:34 fharvey Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dircproxy.h>
#include "sprintf.h"
#include "cfgfile.h"
#include "irc_log.h"

/* forward declaration */
static int _cfg_read_bool(char **, int *);
static int _cfg_read_numeric(char **, long *);
static int _cfg_read_string(char **, char **);
static int _cfg_read_pair(char **, long **);

/* Whitespace */
#define WS " \t\r\n"

/* Quick and easy "Unmatched Quote" define */
#define UNMATCHED_QUOTE { error("Unmatched quote for key '%s' " \
                                "at line %ld of %s", key, line, \
                                filename); valid = 0; break; }

/* Read a config file */
int cfg_read(const char *filename, char **listen_port, char **pid_file,
             struct globalvars *globals) {
  struct ircconnclass defaults, *def, *class;
  int valid;
  long line;
  FILE *fd;

  def = &defaults;
  memset(globals, 0, sizeof(struct globalvars));
  memset(def, 0, sizeof(struct ircconnclass));
  class = 0;
  line = 0;
  valid = 1;
  fd = fopen(filename, "r");
  if (!fd)
    return -1;

  /* Initialise globals */
  globals->client_timeout = DEFAULT_CLIENT_TIMEOUT;
  globals->connect_timeout = DEFAULT_CONNECT_TIMEOUT;
  globals->dns_timeout = DEFAULT_DNS_TIMEOUT;

  /* Initialise using defaults */
  def->server_port = x_strdup(DEFAULT_SERVER_PORT ? DEFAULT_SERVER_PORT : "0");
  def->server_retry = DEFAULT_SERVER_RETRY;
  def->server_maxattempts = DEFAULT_SERVER_MAXATTEMPTS;
  def->server_maxinitattempts = DEFAULT_SERVER_MAXINITATTEMPTS;
  def->server_keepalive = DEFAULT_SERVER_KEEPALIVE;
  def->server_pingtimeout = DEFAULT_SERVER_PINGTIMEOUT;
  if (DEFAULT_SERVER_THROTTLE_BYTES || DEFAULT_SERVER_THROTTLE_PERIOD) {
    def->server_throttle = (long *)malloc(sizeof(long) * 2);
    def->server_throttle[0] = DEFAULT_SERVER_THROTTLE_BYTES;
    def->server_throttle[1] = DEFAULT_SERVER_THROTTLE_PERIOD;
  }
  def->server_autoconnect = DEFAULT_SERVER_AUTOCONNECT;
  def->channel_rejoin = DEFAULT_CHANNEL_REJOIN;
  def->channel_leave_on_detach = DEFAULT_CHANNEL_LEAVE_ON_DETACH;
  def->channel_rejoin_on_attach = DEFAULT_CHANNEL_REJOIN_ON_ATTACH;
  def->idle_maxtime = DEFAULT_IDLE_MAXTIME;
  def->disconnect_existing = DEFAULT_DISCONNECT_EXISTING;
  def->disconnect_on_detach = DEFAULT_DISCONNECT_ON_DETACH;
  def->initial_modes = (DEFAULT_INITIAL_MODES
                        ? x_strdup(DEFAULT_INITIAL_MODES) : 0);
  def->drop_modes = (DEFAULT_DROP_MODES ? x_strdup(DEFAULT_DROP_MODES) : 0);
  def->refuse_modes = (DEFAULT_REFUSE_MODES
                       ? x_strdup(DEFAULT_REFUSE_MODES) : 0);
  def->local_address = (DEFAULT_LOCAL_ADDRESS
                        ? x_strdup(DEFAULT_LOCAL_ADDRESS) : 0);
  def->away_message = (DEFAULT_AWAY_MESSAGE
                       ? x_strdup(DEFAULT_AWAY_MESSAGE) : 0);
  def->quit_message = (DEFAULT_QUIT_MESSAGE
                       ? x_strdup(DEFAULT_QUIT_MESSAGE) : 0);
  def->attach_message = (DEFAULT_ATTACH_MESSAGE
                         ? x_strdup(DEFAULT_ATTACH_MESSAGE) : 0);
  def->detach_message = (DEFAULT_DETACH_MESSAGE
                         ? x_strdup(DEFAULT_DETACH_MESSAGE) : 0);
  def->detach_nickname = (DEFAULT_DETACH_NICKNAME
                          ? x_strdup(DEFAULT_DETACH_NICKNAME) : 0);
  def->nick_keep = DEFAULT_NICK_KEEP;
  def->nickserv_password = NULL;
  def->ctcp_replies = DEFAULT_CTCP_REPLIES;
  def->log_timestamp = DEFAULT_LOG_TIMESTAMP;
  def->log_relativetime = DEFAULT_LOG_RELATIVETIME;
  def->log_timeoffset = DEFAULT_LOG_TIMEOFFSET;
  def->log_events = DEFAULT_LOG_EVENTS;
  def->log_dir = (DEFAULT_LOG_DIR ? x_strdup(DEFAULT_LOG_DIR) : 0);
  def->log_program = (DEFAULT_LOG_PROGRAM ? x_strdup(DEFAULT_LOG_PROGRAM) : 0);
  def->chan_log_enabled = DEFAULT_CHAN_LOG_ENABLED;
  def->chan_log_always = DEFAULT_CHAN_LOG_ALWAYS;
  def->chan_log_maxsize = DEFAULT_CHAN_LOG_MAXSIZE;
  def->chan_log_recall = DEFAULT_CHAN_LOG_RECALL;
  def->private_log_enabled = DEFAULT_PRIVATE_LOG_ENABLED;
  def->private_log_always = DEFAULT_PRIVATE_LOG_ALWAYS;
  def->private_log_maxsize = DEFAULT_PRIVATE_LOG_MAXSIZE;
  def->private_log_recall = DEFAULT_PRIVATE_LOG_RECALL;
  def->server_log_enabled = DEFAULT_SERVER_LOG_ENABLED;
  def->server_log_always = DEFAULT_SERVER_LOG_ALWAYS;
  def->server_log_maxsize = DEFAULT_SERVER_LOG_MAXSIZE;
  def->server_log_recall = DEFAULT_SERVER_LOG_RECALL;
  def->dcc_proxy_incoming = DEFAULT_DCC_PROXY_INCOMING;
  def->dcc_proxy_outgoing = DEFAULT_DCC_PROXY_OUTGOING;
  def->dcc_proxy_ports = 0;
  def->dcc_proxy_ports_sz = 0;
  def->dcc_proxy_timeout = DEFAULT_DCC_PROXY_TIMEOUT;
  def->dcc_proxy_sendreject = DEFAULT_DCC_PROXY_SENDREJECT;
  def->dcc_send_fast = DEFAULT_DCC_SEND_FAST;
  def->dcc_capture_directory = (DEFAULT_DCC_CAPTURE_DIRECTORY
                                ? x_strdup(DEFAULT_DCC_CAPTURE_DIRECTORY) : 0);
  def->dcc_capture_always = DEFAULT_DCC_CAPTURE_ALWAYS;
  def->dcc_capture_withnick = DEFAULT_DCC_CAPTURE_WITHNICK;
  def->dcc_capture_maxsize = DEFAULT_DCC_CAPTURE_MAXSIZE;
  def->dcc_tunnel_incoming = (DEFAULT_DCC_TUNNEL_INCOMING
                              ? x_strdup(DEFAULT_DCC_TUNNEL_INCOMING) : 0);
  def->dcc_tunnel_outgoing = (DEFAULT_DCC_TUNNEL_OUTGOING
                              ? x_strdup(DEFAULT_DCC_TUNNEL_OUTGOING) : 0);
  def->switch_user = (DEFAULT_SWITCH_USER ? x_strdup(DEFAULT_SWITCH_USER) : 0);
  def->motd_logo = DEFAULT_MOTD_LOGO;
  def->motd_file = (DEFAULT_MOTD_FILE ? x_strdup(DEFAULT_MOTD_FILE) : 0);
  def->motd_stats = DEFAULT_MOTD_STATS;
  def->allow_persist = DEFAULT_ALLOW_PERSIST;
  def->allow_jump = DEFAULT_ALLOW_JUMP;
  def->allow_jump_new = DEFAULT_ALLOW_JUMP_NEW;
  def->allow_host = DEFAULT_ALLOW_HOST;
  def->allow_die = DEFAULT_ALLOW_DIE;
  def->allow_users = DEFAULT_ALLOW_USERS;
  def->allow_kill = DEFAULT_ALLOW_KILL;
  def->allow_notify = DEFAULT_ALLOW_NOTIFY;
  
  def->allow_dynamic = DEFAULT_ALLOW_DYNAMIC;
     
  while (valid) {
    char buff[512], *buf;

    if (!fgets(buff, 512, fd))
      break;
    line++;

    buf = buff;
    while ((buf < (buff + 512)) && strlen(buf)) {
      char *key;

      /* Skip whitespace, and ignore lines that are comments */
      buf += strspn(buf, WS);
      if (*buf == '#')
        break;

      /* Find the end of the key, and if there isn't one, exit */
      key = buf;
      buf += strcspn(buf, WS);
      if (!strlen(key))
        break;

      /* If there isn't a newline, then we could reach the end of the
         buffer - so be a bit careful and ensure we don't skip past it */
      if (*buf) {
        *(buf++) = 0;

        /* Close brace is only allowed when class is defined
           (we check here, because its a special case and has no value) */
        if (!strcmp(key, "}") && !class) {
          error("Close brace without open at line %ld of %s", line, filename);
          valid = 0;
          break;
        }

        buf += strspn(buf, WS);
      }

      /* If we reached the end of the buffer, or a comment, that means
         this key has no value.  Unless the key is '}' then thats bad */
      if ((!*buf || (*buf == '#')) && strcmp(key, "}")) {
        error("Missing value for key '%s' at line %ld of %s",
              key, line, filename);
        valid = 0;
        break;
      }

      /* Handle the keys */
      if (!class && !strcasecmp(key, "listen_port")) {
        /* listen_port 57000
           listen_port "dircproxy"    # From /etc/services
         
           ( cannot go in a connection {} ) */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        /* Make sure the silly programmer supplied the pointer! */
        if (listen_port) {
          free(*listen_port);
          *listen_port = str;
        }

      } else if (!class && !strcasecmp(key, "pid_file")) {
        /* pid_file none
           pid_file ""   # same as none
           pid_file "/file"
           pid_file "~/file"
         
           ( cannot go in a connection {} ) */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;

        } else if (!strncmp(str, "~/", 2)) {
          char *home;

          home = getenv("HOME");
          if (home) {
            char *tmp;

            tmp = x_sprintf("%s%s", home, str + 1);
            free(str);
            str = tmp;
          } else {
            /* Best we can do */
            *str = '.';
          }
        }

        /* Make sure the silly programmer supplied the pointer! */
        if (pid_file) {
          free(*pid_file);
          *pid_file = str;
        }

      } else if (!class && !strcasecmp(key, "client_timeout")) {
        /* client_timeout 60 */
        _cfg_read_numeric(&buf, &globals->client_timeout);

      } else if (!class && !strcasecmp(key, "connect_timeout")) {
        /* connect_timeout 60 */
        _cfg_read_numeric(&buf, &globals->connect_timeout);

      } else if (!class && !strcasecmp(key, "dns_timeout")) {
        /* dns_timeout 60 */
        _cfg_read_numeric(&buf, &globals->dns_timeout);

      } else if (!strcasecmp(key, "server_port")) {
        /* server_port 6667
           server_port "irc"    # From /etc/services */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        free((class ? class : def)->server_port);
        (class ? class : def)->server_port = str;

      } else if (!strcasecmp(key, "server_retry")) {
        /* server_retry 15 */
        _cfg_read_numeric(&buf, &(class ? class : def)->server_retry);

      } else if (!strcasecmp(key, "server_maxattempts")) {
        /* server_maxattempts 0 */
        _cfg_read_numeric(&buf, &(class ? class : def)->server_maxattempts);

      } else if (!strcasecmp(key, "server_maxinitattempts")) {
        /* server_maxinitattempts 5 */
        _cfg_read_numeric(&buf, &(class ? class : def)->server_maxinitattempts);

      } else if (!strcasecmp(key, "server_keepalive")) {
        /* server_keepalive yes
           server_keepalive no */
        _cfg_read_bool(&buf, &(class ? class : def)->server_keepalive);
        
      } else if (!strcasecmp(key, "server_pingtimeout")) {
        /* server_pingtimeout 600 */
        _cfg_read_numeric(&buf, &(class ? class : def)->server_pingtimeout);

      } else if (!strcasecmp(key, "server_throttle")) {
        /* server_throttle 0
           server_throttle 512
           server_throttle 1024:10 */
        long *pair;

        _cfg_read_pair(&buf, &pair);

        free((class ? class : def)->server_throttle);
        (class ? class : def)->server_throttle = pair;

      } else if (!strcasecmp(key, "server_autoconnect")) {
        /* server_autoconnect yes
           server_autoconnect no */
        _cfg_read_bool(&buf, &(class ? class : def)->server_autoconnect);

      } else if (!strcasecmp(key, "channel_rejoin")) {
        /* channel_rejoin 5 */
        _cfg_read_numeric(&buf, &(class ? class : def)->channel_rejoin);

      } else if (!strcasecmp(key, "channel_leave_on_detach")) {
        /* channel_leave_on_detach yes
           channel_leave_on_detach no */
        _cfg_read_bool(&buf, &(class ? class : def)->channel_leave_on_detach);

      } else if (!strcasecmp(key, "channel_rejoin_on_attach")) {
        /* channel_rejoin_on_attach yes
           channel_rejoin_on_attach no */
        _cfg_read_bool(&buf, &(class ? class : def)->channel_rejoin_on_attach);

      } else if (!strcasecmp(key, "idle_maxtime")) {
        /* idle_maxtime 120 */
        _cfg_read_numeric(&buf, &(class ? class : def)->idle_maxtime);
 
      } else if (!strcasecmp(key, "disconnect_existing_user")) {
        /* disconnect_existing_user yes
           disconnect_existing_user no */
        _cfg_read_bool(&buf, &(class ? class : def)->disconnect_existing);

      } else if (!strcasecmp(key, "disconnect_on_detach")) {
        /* disconnect_on_detach yes
           disconnect_on_detach no */
        _cfg_read_bool(&buf, &(class ? class : def)->disconnect_on_detach);

      } else if (!strcasecmp(key, "initial_modes")) {
        /* initial_modes "ow"
           initial_modes "" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        while ((*str == '+') || (*str == '-')) {
          char *tmp;
          
          tmp = str;
          str = x_strdup(tmp + 1);
          free(tmp);
        }

        if (!strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->initial_modes);
        (class ? class : def)->initial_modes = str;

      } else if (!strcasecmp(key, "drop_modes")) {
        /* drop_modes "ow"
           drop_modes "" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        while ((*str == '+') || (*str == '-')) {
          char *tmp;
          
          tmp = str;
          str = x_strdup(tmp + 1);
          free(tmp);
        }

        if (!strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->drop_modes);
        (class ? class : def)->drop_modes = str;

      } else if (!strcasecmp(key, "refuse_modes")) {
        /* refuse_modes "r"
           refuse_modes "" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        while ((*str == '+') || (*str == '-')) {
          char *tmp;
          
          tmp = str;
          str = x_strdup(tmp + 1);
          free(tmp);
        }

        if (!strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->refuse_modes);
        (class ? class : def)->refuse_modes = str;

      } else if (!strcasecmp(key, "local_address")) {
        /* local_address none
           local_address ""    # same as none
           local_address "i.am.a.virtual.host.com" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->local_address);
        (class ? class : def)->local_address = str;

      } else if (!strcasecmp(key, "away_message")) {
        /* away_message none
           away_message ""    # same as none
           away_message "Not available, messages are logged" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->away_message);
        (class ? class : def)->away_message = str;

      } else if (!strcasecmp(key, "quit_message")) {
        /* quit_message none
           quit_message ""    # same as none
           quit_message "Gotta restart this thing" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->quit_message);
        (class ? class : def)->quit_message = str;

      } else if (!strcasecmp(key, "attach_message")) {
        /* attach_message none
           attach_message ""    # same as none
           attach_message "I'm back!"
           attach_message "/me returns" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->attach_message);
        (class ? class : def)->attach_message = str;

      } else if (!strcasecmp(key, "detach_message")) {
        /* detach_message none
           detach_message ""    # same as none
           detach_message "I'm gone!"
           detach_message "/me vanishes" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->detach_message);
        (class ? class : def)->detach_message = str;

      } else if (!strcasecmp(key, "detach_nickname")) {
        /* detach_nickname none
           detach_nickname ""    # same as none
           detach_nickname "FooAWAY"
           detach_nickname "*AWAY" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->detach_nickname);
        (class ? class : def)->detach_nickname = str;

      } else if  (!strcasecmp(key, "nickserv_password")) {	      
        /* nickserv_password none
	 * nickserv_password ""    # same as none
	 * nickserv_password "identify_me" */
	char *str;
       
	if (_cfg_read_string(&buf, &str))
	  UNMATCHED_QUOTE;
       
	if (!strcasecmp(str, "none") || !strlen(str)) {
	   free(str);
	   str = 0;
	}
              
       free((class ? class : def)->nickserv_password);
       (class ? class : def)->nickserv_password = str;
	 
      }	 else if (!strcasecmp(key, "nick_keep")) {
        /* nick_keep yes
           nick_keep no */
        _cfg_read_bool(&buf, &(class ? class : def)->nick_keep);

      } else if (!strcasecmp(key, "ctcp_replies")) {
        /* ctcp_replies yes
           ctcp_replies no */
        _cfg_read_bool(&buf, &(class ? class : def)->ctcp_replies);

      } else if (!strcasecmp(key, "log_timestamp")) {
        /* log_timestamp yes
           log_timestamp no */
        _cfg_read_bool(&buf, &(class ? class : def)->log_timestamp);

      } else if (!strcasecmp(key, "log_relativetime")) {
        /* log_relativetime yes
           log_relativetime no */
        _cfg_read_bool(&buf, &(class ? class : def)->log_relativetime);

      } else if (!strcasecmp(key, "log_timeoffset")) {
        /* log_timeoffset 0
           log_timeoffset -60
           log_timeoffset +60 */
        _cfg_read_numeric(&buf, &(class ? class : def)->log_timeoffset);

      } else if (!strcasecmp(key, "log_events")) {
        /* log_events none
           log_events all
           log_events none,+text
           log_events all,-quit */
        char *str, *orig;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        orig = str;
        while (str && strlen(str)) {
          char *ptr;

          ptr = strchr(str, ',');
          if (ptr)
            *(ptr++) = 0;
          str += strspn(str, WS);

          if (strlen(str) && !strcasecmp(str, "all")) {
            (class ? class : def)->log_events = IRC_LOG_ALL;
          } else if (strlen(str) && !strcasecmp(str, "none")) {
            (class ? class : def)->log_events = IRC_LOG_NONE;
          } else if (strlen(str)) {
            int add = 1;

            if (*str == '-') {
              add = 0;
              str++;
            } else if (*str == '+') {
              add = 1;
              str++;
            }

            if (strlen(str)) {
              int flag;

              flag = irclog_strtoflag(str);
              if (flag == 0) {
                error("Unknown event name '%s' in 'log_events' "
                      "at line %ld of %s", str, line, filename);
                valid = 0;
                break;
              }

              if (add) {
                (class ? class : def)->log_events |= flag;
              } else {
                (class ? class : def)->log_events &= ~flag;
              }
            } else {
              error("Missing event name in 'log_events' at line %ld of %s",
                    line, filename);
              valid = 0;
              break;
            }
          } else {
            error("Missing event name in 'log_events' at line %ld of %s",
                  line, filename);
            valid = 0;
            break;
          }

          str = ptr;
        }
        free(orig);
        if (!valid)
          break;

      } else if (!strcasecmp(key, "log_dir")) {
        /* log_dir none
           log_dir ""    # same as none
           log_dir "/log"
           log_dir "~/logs" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;

        } else if (!strncmp(str, "~/", 2)) {
          char *home;

          home = getenv("HOME");
          if (home) {
            char *tmp;

            tmp = x_sprintf("%s%s", home, str + 1);
            free(str);
            str = tmp;
          } else {
            /* Best we can do */
            *str = '.';
          }
        }

        free((class ? class : def)->log_dir);
        (class ? class : def)->log_dir = str;

      } else if (!strcasecmp(key, "log_program")) {
        /* log_program none
           log_program ""    # same as none
           log_program "/logprog"
           log_program "~/logprog" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;

        } else if (!strncmp(str, "~/", 2)) {
          char *home;

          home = getenv("HOME");
          if (home) {
            char *tmp;

            tmp = x_sprintf("%s%s", home, str + 1);
            free(str);
            str = tmp;
          } else {
            /* Best we can do */
            *str = '.';
          }
        }

        free((class ? class : def)->log_program);
        (class ? class : def)->log_program = str;

      } else if (!strcasecmp(key, "chan_log_enabled")) {
        /* chan_log_enabled yes
           chan_log_disabled no */
        _cfg_read_bool(&buf, &(class ? class : def)->chan_log_enabled);

      } else if (!strcasecmp(key, "chan_log_always")) {
        /* chan_log_always yes
           chan_log_always no */
        _cfg_read_bool(&buf, &(class ? class : def)->chan_log_always);

      } else if (!strcasecmp(key, "chan_log_maxsize")) {
        /* chan_log_maxsize 128
           chan_log_maxsize 0 */
        _cfg_read_numeric(&buf, &(class ? class : def)->chan_log_maxsize);

      } else if (!strcasecmp(key, "chan_log_recall")) {
        /* chan_log_recall 128
           chan_log_recall 0
           chan_log_recall -1 */
        _cfg_read_numeric(&buf, &(class ? class : def)->chan_log_recall);

      } else if (!strcasecmp(key, "private_log_enabled")) {
        /* private_log_enabled yes
           private_log_disabled no */
        _cfg_read_bool(&buf, &(class ? class : def)->private_log_enabled);

      } else if (!strcasecmp(key, "private_log_always")) {
        /* private_log_always yes
           private_log_always no */
        _cfg_read_bool(&buf, &(class ? class : def)->private_log_always);

      } else if (!strcasecmp(key, "private_log_maxsize")) {
        /* private_log_maxsize 128
           private_log_maxsize 0 */
        _cfg_read_numeric(&buf, &(class ? class : def)->private_log_maxsize);

      } else if (!strcasecmp(key, "private_log_recall")) {
        /* private_log_recall 128
           private_log_recall 0
           private_log_recall -1 */
        _cfg_read_numeric(&buf, &(class ? class : def)->private_log_recall);

      } else if (!strcasecmp(key, "server_log_enabled")) {
        /* server_log_enabled yes
           server_log_disabled no */
        _cfg_read_bool(&buf, &(class ? class : def)->server_log_enabled);

      } else if (!strcasecmp(key, "server_log_always")) {
        /* server_log_always yes
           server_log_always no */
        _cfg_read_bool(&buf, &(class ? class : def)->server_log_always);

      } else if (!strcasecmp(key, "server_log_maxsize")) {
        /* server_log_maxsize 128
           server_log_maxsize 0 */
        _cfg_read_numeric(&buf, &(class ? class : def)->server_log_maxsize);

      } else if (!strcasecmp(key, "server_log_recall")) {
        /* server_log_recall 128
           server_log_recall 0
           server_log_recall -1 */
        _cfg_read_numeric(&buf, &(class ? class : def)->server_log_recall);

      } else if (!strcasecmp(key, "dcc_proxy_incoming")) {
        /* dcc_proxy_incoming yes
           dcc_proxy_incoming no  */
        _cfg_read_bool(&buf, &(class ? class : def)->dcc_proxy_incoming);

      } else if (!strcasecmp(key, "dcc_proxy_outgoing")) {
        /* dcc_proxy_outgoing yes
           dcc_proxy_outgoing no  */
        _cfg_read_bool(&buf, &(class ? class : def)->dcc_proxy_outgoing);

      } else if (!strcasecmp(key, "dcc_proxy_ports")) {
        /* dcc_proxy_ports any
           dcc_proxy_ports 6667,1042-2048 */
        char *str, *orig;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "any") || !strlen(str)) {
          free(str);
          str = 0;

          free((class ? class : def)->dcc_proxy_ports);
          (class ? class : def)->dcc_proxy_ports = 0;
          (class ? class : def)->dcc_proxy_ports_sz = 0;
        }

        orig = str;
        while (str && strlen(str)) {
          char *ptr;

          ptr = strchr(str, ',');
          if (ptr)
            *(ptr++) = 0;
          str += strspn(str, WS);

          if (strlen(str)) {
            int lwr, upr;
            char *dash;

            dash = strchr(str, '-');
            if (dash)
              *(dash++) = 0;

            lwr = atoi(str);
            upr = (dash ? atoi(dash) : lwr);

            if (lwr && upr) {
              int *newlist;
              size_t newsz;

              newsz = (class ? class : def)->dcc_proxy_ports_sz + 2;
              newlist = (int *)malloc(sizeof(int) * newsz);
              memcpy(newlist, (class ? class : def)->dcc_proxy_ports,
                     sizeof(int) * (class ? class : def)->dcc_proxy_ports_sz);
              newlist[(class ? class : def)->dcc_proxy_ports_sz + 0] = lwr;
              newlist[(class ? class : def)->dcc_proxy_ports_sz + 1] = upr;
              free((class ? class : def)->dcc_proxy_ports);
              (class ? class : def)->dcc_proxy_ports = newlist;
              (class ? class : def)->dcc_proxy_ports_sz = newsz;
              
            } else {
              error("Bad port in 'dcc_proxy_ports' at line %ld of %s",
                    line, filename);
              valid = 0;
              free(orig);
              break;
            }
          } else {
            error("Missing port range in 'dcc_proxy_ports' at line %ld of %s",
                  line, filename);
            valid = 0;
            free(orig);
            break;
          }

          str = ptr;
        }
        free(orig);

      } else if (!strcasecmp(key, "dcc_proxy_timeout")) {
        /* dcc_proxy_timeout 60 */
        _cfg_read_numeric(&buf, &(class ? class : def)->dcc_proxy_timeout);

      } else if (!strcasecmp(key, "dcc_proxy_sendreject")) {
        /* dcc_proxy_sendreject yes
           dcc_proxy_sendreject no  */
        _cfg_read_bool(&buf, &(class ? class : def)->dcc_proxy_sendreject);

      } else if (!strcasecmp(key, "dcc_send_fast")) {
        /* dcc_send_fast yes
           dcc_send_fast no  */
        _cfg_read_bool(&buf, &(class ? class : def)->dcc_send_fast);

      } else if (!strcasecmp(key, "dcc_capture_directory")) {
        /* dcc_capture_directory none
           dcc_capture_directory ""    # same as none
           dcc_capture_directory "/tmp"
           dcc_capture_directory "~/caught" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;

        } else if (!strncmp(str, "~/", 2)) {
          char *home;

          home = getenv("HOME");
          if (home) {
            char *tmp;

            tmp = x_sprintf("%s%s", home, str + 1);
            free(str);
            str = tmp;
          } else {
            /* Best we can do */
            *str = '.';
          }
        }

        free((class ? class : def)->dcc_capture_directory);
        (class ? class : def)->dcc_capture_directory = str;

      } else if (!strcasecmp(key, "dcc_capture_always")) {
        /* dcc_capture_always yes
           dcc_capture_always no  */
        _cfg_read_bool(&buf, &(class ? class : def)->dcc_capture_always);

      } else if (!strcasecmp(key, "dcc_capture_withnick")) {
        /* dcc_capture_withnick yes
           dcc_capture_withnick no  */
        _cfg_read_bool(&buf, &(class ? class : def)->dcc_capture_withnick);

      } else if (!class && !strcasecmp(key, "dcc_capture_maxsize")) {
        /* dcc_capture_maxsize 0
           dcc_capture_maxsize 1024 */
        _cfg_read_numeric(&buf, &(class ? class : def)->dcc_capture_maxsize);

      } else if (!strcasecmp(key, "dcc_tunnel_incoming")) {
        /* dcc_tunnel_incoming none
           dcc_tunnel_incoming ""           # same as none
           dcc_tunnel_incoming "6667"
           dcc_tunnel_incoming "irctunnel"  # from /etc/services */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->dcc_tunnel_incoming);
        (class ? class : def)->dcc_tunnel_incoming = str;

      } else if (!strcasecmp(key, "dcc_tunnel_outgoing")) {
        /* dcc_tunnel_outgoing none
           dcc_tunnel_outgoing ""           # same as none
           dcc_tunnel_outgoing "6667"
           dcc_tunnel_outgoing "irctunnel"  # from /etc/services */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;
        }

        free((class ? class : def)->dcc_tunnel_outgoing);
        (class ? class : def)->dcc_tunnel_outgoing = str;

      } else if (!strcasecmp(key, "switch_user")) {
        /* switch_user none
           switch_user ""   # same as none
           switch_user 1001
           switch_user "bob" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

#ifdef HAVE_SETEUID
        if (getuid() == 0) {
          if (!strcasecmp(str, "none") || !strlen(str)) {
            free(str);
            str = 0;
          }

          free((class ? class : def)->switch_user);
          (class ? class : def)->switch_user = str;
        } else {
          error("(warning) must be run as root to use 'switch_user' "
                "at line %ld of %s", line, filename);
          free(str);
        }
#else /* HAVE_SETEUID */
        error("(warning) Your system does not support 'switch_user' "
              "at line %ld of %s", line, filename);
        free(str);
#endif /* HAVE_SETEUID */

      } else if (!strcasecmp(key, "motd_logo")) {
        /* motd_logo yes
           motd_logo no */
        _cfg_read_bool(&buf, &(class ? class : def)->motd_logo);

      } else if (!strcasecmp(key, "motd_file")) {
        /* motd_file none
           motd_file ""   # same as none
           motd_file "/file"
           motd_file "~/file" */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        if (!strcasecmp(str, "none") || !strlen(str)) {
          free(str);
          str = 0;

        } else if (!strncmp(str, "~/", 2)) {
          char *home;

          home = getenv("HOME");
          if (home) {
            char *tmp;

            tmp = x_sprintf("%s%s", home, str + 1);
            free(str);
            str = tmp;
          } else {
            /* Best we can do */
            *str = '.';
          }
        }

        free((class ? class : def)->motd_file);
        (class ? class : def)->motd_file = str;

      } else if (!strcasecmp(key, "motd_stats")) {
        /* motd_stats yes
           motd_stats no */
        _cfg_read_bool(&buf, &(class ? class : def)->motd_stats);

      } else if (!strcasecmp(key, "allow_persist")) {
        /* allow_persist yes
           allow_persist no */
        _cfg_read_bool(&buf, &(class ? class : def)->allow_persist);

      } else if (!strcasecmp(key, "allow_jump")) {
        /* allow_jump yes
           allow_jump no */
        _cfg_read_bool(&buf, &(class ? class : def)->allow_jump);

      } else if (!strcasecmp(key, "allow_jump_new")) {
        /* allow_jump_new yes
           allow_jump_new no */
        _cfg_read_bool(&buf, &(class ? class : def)->allow_jump_new);

      } else if (!strcasecmp(key, "allow_host")) {
        /* allow_host yes
           allow_host no */
        _cfg_read_bool(&buf, &(class ? class : def)->allow_host);

      } else if (!strcasecmp(key, "allow_die")) {
        /* allow_die yes
           allow_die no */
        _cfg_read_bool(&buf, &(class ? class : def)->allow_die);

      } else if (!strcasecmp(key, "allow_users")) {
        /* allow_users yes
           allow_users no */
        _cfg_read_bool(&buf, &(class ? class : def)->allow_users);

      } else if (!strcasecmp(key, "allow_kill")) {
        /* allow_kill yes
           allow_kill no */
        _cfg_read_bool(&buf, &(class ? class : def)->allow_kill);
      } else if (!strcasecmp(key, "allow_notify")) {
	 /* allow_notify yes
	    allow_notify no */
	_cfg_read_bool(&buf, &(class ? class : def)->allow_notify);	 
      } else if (!class && !strcasecmp(key, "connection")) {
        /* connection {
             :
             :
           } */

        if (*buf != '{') {
          /* Connection is a bit special, because the only valid value is '{'
             the internals are handled elsewhere */
          error("Expected open brace for key '%s' at line %ld of %s",
                key, line, filename);
          valid = 0;
          break;
        }
        buf++;

        /* Allocate memory, it'll be filled later */
        class = (struct ircconnclass *)malloc(sizeof(struct ircconnclass));
        memcpy(class, def, sizeof(struct ircconnclass));
        class->server_port = (def->server_port
                              ? x_strdup(def->server_port) : 0);
        if (def->server_throttle) {
          class->server_throttle = (long *)malloc(sizeof(long) * 2);
          memcpy(class->server_throttle, def->server_throttle,
                 sizeof(long) * 2);
        }
        class->initial_modes = (def->initial_modes
                             ? x_strdup(def->initial_modes) : 0);
        class->drop_modes = (def->drop_modes
                             ? x_strdup(def->drop_modes) : 0);
        class->refuse_modes = (def->refuse_modes
                               ? x_strdup(def->refuse_modes) : 0);
        class->local_address = (def->local_address
                                ? x_strdup(def->local_address) : 0);
        class->away_message = (def->away_message
                               ? x_strdup(def->away_message) : 0);
        class->quit_message = (def->quit_message
                               ? x_strdup(def->quit_message) : 0);
        class->attach_message = (def->attach_message
                                 ? x_strdup(def->attach_message) : 0);
        class->detach_message = (def->detach_message
                                 ? x_strdup(def->detach_message) : 0);
        class->detach_nickname = (def->detach_nickname
                                  ? x_strdup(def->detach_nickname) : 0);
        class->log_dir = (def->log_dir ? x_strdup(def->log_dir) : 0);
        class->log_program = (def->log_program 
                              ? x_strdup(def->log_program) : 0);
        if (def->dcc_proxy_ports) {
          class->dcc_proxy_ports = (int *)malloc(sizeof(int)
                                                 * def->dcc_proxy_ports_sz);
          memcpy(class->dcc_proxy_ports, def->dcc_proxy_ports,
                 sizeof(int) * def->dcc_proxy_ports_sz);
        }
        class->dcc_capture_directory = (def->dcc_capture_directory
                                        ? x_strdup(def->dcc_capture_directory)
                                        : 0);
        class->dcc_tunnel_incoming = (def->dcc_tunnel_incoming
                                      ? x_strdup(def->dcc_tunnel_incoming) : 0);
        class->dcc_tunnel_outgoing = (def->dcc_tunnel_outgoing
                                      ? x_strdup(def->dcc_tunnel_outgoing) : 0);
        class->switch_user = (def->switch_user
                              ? x_strdup(def->switch_user) : 0);
        class->motd_file = (def->motd_file ? x_strdup(def->motd_file) : 0);

      } else if (class && !strcasecmp(key, "password")) {
        /* connection {
             :
             password "foo"
             :
           } */
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        free(class->password);
        class->password = str;

      } else if (class && !strcasecmp(key, "server")) {
        /* connection {
             :
             server "irc.linux.com"
             server "irc.linux.com:6670"     # Port other than default
             server "irc.linux.com:6670:foo" # Port and password
             server "irc.linux.com::foo"     # Password and default port
             :
           } */
        struct strlist *s;
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        s = (struct strlist *)malloc(sizeof(struct strlist));
        s->str = str;
        s->next = 0;

        if (class->servers) {
          struct strlist *ss;

          ss = class->servers;
          while (ss->next)
            ss = ss->next;

          ss->next = s;
        } else {
          class->servers = s;
        }

      } else if (class && !strcasecmp(key, "from")) {
        /* connection {
             :
             from "static-132.myisp.com"    # Static hostname
             from "*.myisp.com"             # Masked hostname
             from "192.168.1.1"             # Specific IP
             from "192.168.*"               # IP range
             :
           } */
        struct strlist *s;
        char *str;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        s = (struct strlist *)malloc(sizeof(struct strlist));
        s->str = str;
        s->next = class->masklist;
        class->masklist = s;

      } else if (class && !strcasecmp(key, "join")) {
        /* connection {
             :
             join "#foo"
             join "#foo key,#bar"
             :
           } */
        struct strlist *s;
        char *str, *orig;

        if (_cfg_read_string(&buf, &str))
          UNMATCHED_QUOTE;

        orig = str;
        while (str && strlen(str)) {
          char *ptr;

          ptr = strchr(str, ',');
          if (ptr)
            *(ptr++) = 0;
          str += strspn(str, WS);

          if (strlen(str)) {
            s = (struct strlist *)malloc(sizeof(struct strlist));
            s->str = x_strdup(str);
            s->next = 0;

            if (class->channels) {
              struct strlist *ss;

              ss = class->channels;
              while (ss->next)
                ss = ss->next;

              ss->next = s;
            } else {
              class->channels = s;
            }
          } else {
            error("Missing channel name in 'join' at line %ld of %s",
                  line, filename);
            valid = 0;
            free(orig);
            break;
          }

          str = ptr;
        }
        free(orig);

      } else if (class && !strcmp(key, "}")) {
        /* No auto-connect?  Then we *need* jump */
        if (!class->server_autoconnect)
          class->allow_jump = 1;

        /* Check that a password and at least one server were defined */
        if (!class->password) {
          error("Connection class defined without password "
                "before line %ld of %s", line, filename);
          valid = 0;
        } else if (!class->servers
                   && (class->server_autoconnect || !class->allow_jump_new)) {
          error("Connection class defined without a server "
                "before line %ld of %s", line, filename);
          valid = 0;
        }

        /* Add to the list of servers if valid, otherwise free it */
        if (valid) {
          class->orig_local_address = (class->local_address
                                       ? x_strdup(class->local_address) : 0);
          class->next_server = class->servers;
          class->next = connclasses;
          connclasses = class;
          class = 0;
        } else {
          ircnet_freeconnclass(class);
          class = 0;
          break;
        }

      } else {
        /* Bad key! */
        error("Unknown config file key '%s' at line %ld of %s",
              key, line, filename);
        valid = 0;
        break;
      }

      /* Skip whitespace.  The only things that can trail are comments
         and close braces, and we re-pass to do those */
      buf += strspn(buf, WS);
      if (*buf && (*buf != '#') && (*buf != '}')) {
        error("Unexpected data at end of line %ld of %s", line, filename);
        valid = 0;
        break;
      }
    }
  }

  /* Argh, untidy stuff left around */
  if (class) {
    ircnet_freeconnclass(class);
    class = 0;
    if (valid) {
      error("Unmatched open brace in %s", filename);
      valid = 0;
    }
  }

  fclose(fd);
  free(def->server_port);
  free(def->server_throttle);
  free(def->initial_modes);
  free(def->drop_modes);
  free(def->refuse_modes);
  free(def->local_address);
  free(def->away_message);
  free(def->quit_message);
  free(def->attach_message);
  free(def->detach_message);
  free(def->detach_nickname);
  free(def->log_dir);
  free(def->log_program);
  free(def->dcc_proxy_ports);
  free(def->dcc_capture_directory);
  free(def->dcc_tunnel_incoming);
  free(def->dcc_tunnel_outgoing);
  free(def->switch_user);
  free(def->motd_file);
  return (valid ? 0 : -1);
}

/* Read a boolean value from config file */
static int _cfg_read_bool(char **buf, int *val) {
  char *ptr;

  ptr = *buf;
  *buf += strcspn(*buf, WS);
  *((*buf)++) = 0;

  if (!strcasecmp(ptr, "yes")) {
    *val = 1;
  } else if (!strcasecmp(ptr, "true")) {
    *val = 1;
  } else if (!strcasecmp(ptr, "y")) {
    *val = 1;
  } else if (!strcasecmp(ptr, "t")) {
    *val = 1;
  } else if (!strcasecmp(ptr, "no")) {
    *val = 0;
  } else if (!strcasecmp(ptr, "false")) {
    *val = 0;
  } else if (!strcasecmp(ptr, "n")) {
    *val = 0;
  } else if (!strcasecmp(ptr, "f")) {
    *val = 0;
  } else {
    *val = (atoi(ptr) ? 1 : 0);
  }

  return 0;
}

/* Read a numeric value from config file */
static int _cfg_read_numeric(char **buf, long *val) {
  char *ptr;

  ptr = *buf;
  *buf += strcspn(*buf, WS);
  *((*buf)++) = 0;

  *val = atol(ptr);

  return 0;
}

/* Read a string value from config file */
static int _cfg_read_string(char **buf, char **val) {
  char *ptr;

  if (**buf == '"') {
    ptr = ++(*buf);

    while (1) {
      *buf += strcspn(*buf, "\"");
      if (**buf != '"') {
        return -1;
      } else if (*(*buf - 1) == '\\') {
        (*buf)++;
        continue;
      } else {
        break;
      }
    }
  } else {
    ptr = *buf;
    *buf += strcspn(*buf, WS);
  }
  *((*buf)++) = 0;

  *val = x_strdup(ptr);

  return 0;
}

/* Read a numeric pair from config file */
static int _cfg_read_pair(char **buf, long **val) {
  char *ptr, *col;
  long ret[2];

  ptr = *buf;
  *buf += strcspn(*buf, WS);
  *((*buf)++) = 0;

  col = strchr(ptr, ':');
  if (col) {
    *(col++) = 0;
    ret[1] = atol(col);
  } else {
    ret[1] = 1;
  }
  ret[0] = atol(ptr);
  
  if (ret[0] || ret[1]) {
    *val = (long *)malloc(sizeof(long) * 2);
    memcpy(*val, ret, sizeof(long) * 2);
  } else {
    *val = 0;
  }

  return 0;
}
