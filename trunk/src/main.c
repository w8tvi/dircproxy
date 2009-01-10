/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 *
 * main.c
 *  - Program main loop
 *  - Command line handling
 *  - Initialisation and shutdown
 *  - Signal handling
 *  - Debug functions
 * --
 * @(#) $Id: main.c,v 1.57 2004/02/13 23:39:34 bear Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <pwd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>

#include <dircproxy.h>
#include "getopt/getopt.h"
#include "sprintf.h"
#include "cfgfile.h"
#include "irc_net.h"
#include "irc_client.h"
#include "irc_server.h"
#include "dcc_net.h"
#include "timers.h"
#include "dns.h"
#include "net.h"

/* forward declarations */
static void _sig_term(int);
static void _sig_hup(int);
static void _sig_child(int);
#ifdef DEBUG_MEMORY
static void _sig_usr(int);
#endif /* DEBUG_MEMORY */
static int _reload_config(void);
static int _print_usage(void);
static int _print_version(void);
static int _print_help(void);

/* This is so "ident" and "what" can query version etc - useful (not) */
const char *rcsid = "@(#) $Id: main.c,v 1.57 2004/02/13 23:39:34 bear Exp $";

/* The name of the program */
static char *progname;

/* whether we went in the background or not */
static int in_background = 0;

/* set to 1 to abort the main loop */
static int stop_poll = 0;

/* set to 1 to reload the configuration file */
static int reload_config = 0;

/* Port we're listening on */
static char *listen_port;

/* File to write our pid to */
static char *pid_file;

/* The configuration file we used */
static char *config_file;

/* Global variables */
struct globalvars g;

/* Long options */
static struct option long_opts[] = {
  { "config-file", 1, NULL, 'f' },
  { "help", 0, NULL, 'h' },
  { "version", 0, NULL, 'v' },
#ifndef DEBUG
  { "no-daemon", 0, NULL, 'D' },
#else /* DEBUG */
  { "daemon", 0, NULL, 'D' },
#endif /* DEBUG */
  { "inetd", 0, NULL, 'I' },
  { "listen-port", 1, NULL, 'P' },
  { "pid-file", 1, NULL, 'p' },
  { 0, 0, 0, 0 }
};

/* Options */
#define GETOPTIONS "f:hvDIP:"

/* We need this */
int main(int argc, char *argv[]) {
  int optc, show_help, show_version, show_usage;
  char *local_file, *cmd_listen_port, *cmd_pid_file;
  int inetd_mode, no_daemon;

  /* Set up some globals */
  progname = argv[0];
  listen_port = x_strdup(DEFAULT_LISTEN_PORT);
  pid_file = (DEFAULT_PID_FILE ? x_strdup(DEFAULT_PID_FILE) : 0);

#ifndef DEBUG
  no_daemon = 0;
#else /* DEBUG */
  no_daemon = 1;
#endif /* DEBUG */
  local_file = cmd_listen_port = cmd_pid_file = 0;
  show_help = show_version = show_usage = inetd_mode = 0;
  while ((optc = getopt_long(argc, argv, GETOPTIONS, long_opts, NULL)) != -1) {
    switch (optc) {
      case 'h':
        show_help = 1;
        break;
      case 'v':
        show_version = 1;
        break;
      case 'D':
#ifndef DEBUG
        no_daemon = 1;
#else /* DEBUG */
        no_daemon = 0;
#endif /* DEBUG */
        break;
      case 'I':
        inetd_mode = 1;
        break;
      case 'P':
        free(cmd_listen_port);
        cmd_listen_port = x_strdup(optarg);
        break;
      case 'p':
        free(cmd_pid_file);
        cmd_pid_file = x_strdup(optarg);
        break;
      case 'f':
        free(local_file);
        local_file = x_strdup(optarg);
        break;
      default:
        show_usage = 1;
        break;
    }
  }

  if (show_usage || (optind < argc)) {
    _print_usage();
    return 1;
  }

  if (show_version) {
    _print_version();
    if (!show_help)
      return 0;
  }

  if (show_help) {
    _print_help();
    return 0;
  }

  /* If no -f was specified use the home directory */
  if (!local_file && !inetd_mode) {
    struct stat statinfo;
    struct passwd *pw;

    pw = getpwuid(geteuid());
    if (pw && pw->pw_dir) {
      local_file = x_sprintf("%s/%s", pw->pw_dir, USER_CONFIG_FILENAME);
      debug("Local config file: %s", local_file);
      if (!stat(local_file, &statinfo) && (statinfo.st_mode & 0077)) {
        fprintf(stderr, "%s: Permissions of %s must be 0700 or "
                        "more restrictive\n", progname, local_file);
        free(local_file);
        return 2;
      }
      if (cfg_read(local_file, &listen_port, &pid_file, &g)) {
        /* If the local one didn't exist, set to 0 so we open
           global one */
        free(local_file);
        local_file = 0;
      } else {
        config_file = x_strdup(local_file);
      }
    }
  } else if (local_file) {
    if (cfg_read(local_file, &listen_port, &pid_file, &g)) {
      /* This is fatal! */
      fprintf(stderr, "%s: Couldn't read configuration from %s: %s\n",
              progname, local_file, strerror(errno));
      free(local_file);
      return 2;
    } else {
      config_file = x_strdup(local_file);
    }
  }

  /* Read global config file if local one not found */
  if (!local_file) {
    char *global_file;

    /* Not fatal if it doesn't exist */
    global_file = x_sprintf("%s/%s", SYSCONFDIR, GLOBAL_CONFIG_FILENAME);
    debug("Global config file: %s", global_file);
    cfg_read(global_file, &listen_port, &pid_file, &g);
    config_file = x_strdup(global_file);
    free(global_file);
  } else {
    free(local_file);
  }

  /* Check we got some connection classes */
  if (!connclasses) {
    fprintf(stderr, "%s: No connection classes have been defined.\n", progname);
    return 2;
  }

  /* -P overrides config file */
  if (cmd_listen_port) {
    free(listen_port);
    listen_port = cmd_listen_port;
  }

  /* -p overrides pid file */
  if (cmd_pid_file) {
    free(pid_file);
    pid_file = cmd_pid_file;
  }

  /* Set signal handlers */
  signal(SIGTERM, _sig_term);
  signal(SIGINT, _sig_term);
  signal(SIGHUP, _sig_hup);
  signal(SIGCHLD, _sig_child);
#ifdef DEBUG_MEMORY
  signal(SIGUSR1, _sig_usr);
  signal(SIGUSR2, _sig_usr);
#endif /* DEBUG_MEMORY */

  /* Broken Pipe?  This means that someone disconnected while we were
     sending stuff.  Naughty! */
  signal(SIGPIPE, SIG_IGN);

  if (!inetd_mode) {
    debug("Ordinary console dodge-monkey mode");

    /* Make listening socket before we fork */
    if (ircnet_listen(listen_port)) {
      fprintf(stderr, "%s: Unable to establish listen port\n", progname);
      return 3;
    }

    /* go daemon here */
    if (!no_daemon) {
      switch (go_daemon()) {
        case -1:
          return -1;
        case 0:
          break;
        default:
          return 0;
      }
    }

  } else {
    /* running under inetd means we are backgrounded right *now* */
    in_background = 1;

    debug("Inetd SuperTed mode!");

    /* Hook STDIN into a new proxy */
    ircnet_hooksocket(STDIN_FILENO);
  }
 
  /* Open a connection to syslog if we're in the background */
  if (in_background)
    openlog(PACKAGE, LOG_PID, LOG_USER);

  if (pid_file) {
    FILE *pidfile;

    pidfile = fopen(pid_file, "w");
    if (pidfile) {
      fchmod(fileno(pidfile), 0600);
      fprintf(pidfile, "%d\n", getpid());
      fclose(pidfile);
    } else {
      syscall_fail("fopen", pid_file, 0);
    }
  }
  
  /* Main loop! */
  while (!stop_poll) {
    int ns, nt, status;
    pid_t pid;

    ircnet_expunge_proxies();
    dccnet_expunge_proxies();
    ns = net_poll();
    nt = timer_poll();

    /* Reap any children */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
      debug("Reaped process %d, exit status %d", pid, status);
      
      /* Handle any DNS children */
      dns_endrequest(pid, status);
    }

    /* Reload the configuration file? */
    if (reload_config) {
      _reload_config();
      reload_config = 0;
    }

    if (!ns && !nt)
      break;
  }

  if (pid_file) {
    unlink(pid_file);
  }

  /* Free up stuff */
  ircnet_flush();
  dccnet_flush();
  dns_flush();
  timer_flush();

  /* Do a lingering close on all sockets */
  net_closeall();
  net_flush();

  /* Close down and free up memory */
  if (!inetd_mode && !no_daemon)
    closelog();
  free(listen_port);
  free(pid_file);
  free(config_file);

#ifdef DEBUG_MEMORY
  mem_report("termination");
#endif /* DEBUG_MEMORY */

  return 0;
}

/* Signal to stop polling */
static void _sig_term(int sig) {
  debug("Received signal %d to stop", sig);
  stop();
}

/* Signal to reload configuration file */
static void _sig_hup(int sig) {
  debug("Received signal %d to reload config", sig);
  reload_config = 1;

  /* Restore the signal */
  signal(sig, _sig_hup);
}

/* Signal to reap child process.  Don't do anything other than interrupt
 * whatever we're doing so we reach the waitpid loop in the main loop quicker
 */
static void _sig_child(int sig) {
  debug("Received signal %d to reap", sig);

  /* Restore the signal */
  signal(sig, _sig_child);
}


#ifdef DEBUG_MEMORY
/* On USR signals, dump debug information */
static void _sig_usr(int sig) {
  mem_report(sig == SIGUSR1 ? 0 : "signal");
  signal(sig, _sig_usr);
}
#endif /* DEBUG_MEMORY */

/* Reload the configuration file */
static int _reload_config(void) {
  struct ircconnclass *oldclasses, *c;
  struct globalvars newglobals;
  char *new_listen_port, *new_pid_file;

  debug("Reloading config from %s", config_file);
  new_listen_port = x_strdup(DEFAULT_LISTEN_PORT);
  new_pid_file = (DEFAULT_PID_FILE ? x_strdup(DEFAULT_PID_FILE) : 0);
  oldclasses = connclasses;
  connclasses = 0;

  if (cfg_read(config_file, &new_listen_port, &new_pid_file, &newglobals)) {
    /* Config file reload failed */
    error("Reload of configuration file %s failed", config_file);
    ircnet_flush_connclasses(&connclasses);

    connclasses = oldclasses;
    free(new_listen_port);
    free(new_pid_file);
    return -1;
  }

  /* Copy over new globals */
  memcpy(&g, &newglobals, sizeof(struct globalvars));

  /* Listen port changed */
  if (strcmp(listen_port, new_listen_port)) {
    debug("Changing listen_port from %s to %s", listen_port, new_listen_port);
    if (ircnet_listen(new_listen_port)) {
      /* This isn't fatal */
      error("Unable to change listen_port from %s to %s",
            listen_port, new_listen_port);
    } else {
      free(listen_port);
      listen_port = new_listen_port;
      new_listen_port = 0;
    }
  }

  /* Change the pid file variable (don't bother rewriting it) */
  free(pid_file);
  pid_file = new_pid_file;
  new_pid_file = 0;
  
  /* Match everything back up to the old stuff */
  c = connclasses;
  while (c) {
    struct ircconnclass *o;

    o = oldclasses;
    while (o) {
      if (!strcmp(c->password, o->password)) {
        struct ircproxy *p;

        p = ircnet_fetchclass(o);
        if (p)
          p->conn_class = c;

        break;
      }

      o = o->next;
    }

    c = c->next;
  }

  /* Kill anyone who got lost in the reload */
  c = oldclasses;
  while (c) {
    struct ircproxy *p;

    p = ircnet_fetchclass(c);
    if (p) {
      p->conn_class = 0;
      ircserver_send_command(p, "QUIT", ":Permission revoked - %s %s",
                             PACKAGE, VERSION);
      ircserver_close_sock(p);

      ircclient_send_error(p, "No longer permitted to use this proxy");
      ircclient_close(p);
    }

    c = c->next;
  }
  
  /* Clean up */
  ircnet_flush_connclasses(&oldclasses);
  free(new_listen_port);

  return 0;
}

/* Print the usage instructions to stderr */
static int _print_usage(void) {
  fprintf(stderr, "%s: Try '%s --help' for more information.\n",
          progname, progname);

  return 0;
}

/* Print the version number to stderr */
static int _print_version(void) {
  fprintf(stderr, "%s %s - (C) 2009 Noel Shrum & Francois Harvey - http://dircproxy.googlecode.com/", PACKAGE, VERSION);
   
#ifdef SSL
  fprintf(stderr, " - SSL");
#endif /* SSL */
#ifdef DEBUG
  fprintf(stderr, " - DEBUG");
#endif /* DEBUG */
   fprintf(stderr, "\n");
  return 0;
}

/* Print the help to stderr */
static int _print_help(void) {
  fprintf(stderr, "%s %s.  Detachable IRC proxy.\n", PACKAGE, VERSION);
  fprintf(stderr, "(C) 2009 Noel Shrum & Francois Harvey - http://dircproxy.googlecode.com/\n\n");
  fprintf(stderr, "Usage: %s [OPTION]...\n\n", progname);
  fprintf(stderr, "If a long option shows an argument as mandatory, then "
                  "it is mandatory\nfor the equivalent short option also.  "
                  "Similarly for optional arguments.\n\n");
  fprintf(stderr, "  -h, --help              Print a summary of the options\n");
  fprintf(stderr, "  -v, --version           Print the version number\n");
#ifndef DEBUG
  fprintf(stderr, "  -D, --no-daemon         Remain in the foreground\n");
#else /* DEBUG */
  fprintf(stderr, "  -D, --daemon            Run as a daemon to debug it\n");
#endif /* DEBUG */
  fprintf(stderr, "  -I, --inetd             Being run from inetd "
                                            "(implies -D)\n");
  fprintf(stderr, "  -P, --listen-port=PORT  Port to listen for clients on\n");
  fprintf(stderr, "  -p, --pid-file=FILE     Write PID to this file\n");
  fprintf(stderr, "  -f, --config-file=FILE  Use this file instead of the "
                                            "default\n\n");

  return 0;
}

/* Called when a system call fails.  Print it to stderr or syslog */
int syscall_fail(const char *function, const char *arg, const char *message) {
  char *msg;
  int err;

  err = errno;

  msg = x_sprintf("%s(%s) failed: %s", function, (arg ? arg : ""),
                  (message ? message : strerror(err)));
  if (in_background) {
    syslog(LOG_NOTICE, "%s", msg);
  } else {
#ifdef DEBUG
    fprintf(stderr, "%s: \033[33;1m%s\033[m\n", progname, msg);
#else /* DEBUG */
    fprintf(stderr, "%s: %s\n", progname, msg);
#endif /* DEBUG */
  }

  free(msg);
  return 0;
}

/* Called to log an error */
int error(const char *format, ...) {
  va_list ap;
  char *msg;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);
 
  if (in_background) {
    syslog(LOG_ERR, "%s", msg);
  } else {
#ifdef DEBUG
    fprintf(stderr, "%s: \033[31;1m%s\033[m\n", progname, msg);
#else /* DEBUG */
    fprintf(stderr, "%s: %s\n", progname, msg);
#endif /* DEBUG */
  }

  free(msg);
  return 0;
}

/* Called to output debugging information to stderr or syslog */
int debug(const char *format, ...) {
#ifdef DEBUG
  va_list ap;
  char *msg;

  va_start(ap, format);
#ifdef DEBUG_MEMORY
  msg = xx_vsprintf(0, 0, format, ap);
#else /* DEBUG_MEMORY */
  msg = x_vsprintf(format, ap);
#endif /* DEBUG_MEMORY */
  va_end(ap);
 
  if (in_background) {
    syslog(LOG_DEBUG, "%s", msg);
  } else {
    printf("%s\n", msg);
  }

  free(msg);
#endif /* DEBUG */

  return 0;
}

/* Called to stop dircproxy */
int stop(void) {
  stop_poll = 1;

  return 0;
}

/* Called to set queue config reload */
int reload(void) {
  reload_config = 1;

  return 0;
}

/* Called to go into the background */
int go_daemon(void) {
  if (in_background)
    return 0;

  debug("Running in the background");

  switch (fork()) {
    case -1:
      syscall_fail("fork", "first", 0);
      return -1;
    case 0:
      break;
    default:
      return 1;
  }

  /* Become process group leader */
  setsid();

  switch (fork()) {
    case -1:
      syscall_fail("fork", "second", 0);
      return -1;
    case 0:
      break;
    default:
      return 2;
  }
  
  /* Set our umask */    
  umask(0);

  /* Okay, we're in the background now */
  in_background = 1;
  return 0;
}
