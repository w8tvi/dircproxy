/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * main.c
 *  - Program main loop
 *  - Command line handling
 *  - Initialisation and shutdown
 *  - Signal handling
 *  - Debug functions
 * --
 * @(#) $Id: main.c,v 1.44 2000/10/30 13:41:20 keybuk Exp $
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
#include "getopt.h"
#include "sprintf.h"
#include "cfgfile.h"
#include "irc_net.h"
#include "irc_client.h"
#include "irc_server.h"
#include "timers.h"
#include "dns.h"
#include "net.h"

/* forward declarations */
static void sig_term(int);
static void sig_hup(int);
static void sig_child(int);
#ifdef DEBUG_MEMORY
static void sig_usr(int);
#endif /* DEBUG_MEMORY */
static int _print_usage(void);
static int _print_version(void);
static int _print_help(void);

/* This is so "ident" and "what" can query version etc - useful (not) */
const char *rcsid = "@(#) $Id: main.c,v 1.44 2000/10/30 13:41:20 keybuk Exp $";

/* The name of the program */
static char *progname;

/* whether we went in the background or not */
static int in_background = 0;

/* set to 1 to abort the main loop */
static int stop_poll = 0;

/* Port we're listening on */
static char *listen_port;

/* The configuration file we used */
static char *config_file;

/* Global variables */
struct globalvars g;

/* Long options */
struct option long_opts[] = {
  { "config-file", 1, NULL, 'f' },
  { "help", 0, NULL, 'h' },
  { "version", 0, NULL, 'v' },
#ifndef DEBUG
  { "no-daemon", 0, NULL, 'D' },
#else /* DEBUG */
  { "daemon", 0, NULL, 'D' },
#endif /* DEBUG */
  { "inetd", 0, NULL, 'I' },
  { "listen-port", 1, NULL, 'P' }
};

/* Options */
#define GETOPTIONS "f:hvDIP:"

/* We need this */
int main(int argc, char *argv[]) {
  int optc, show_help, show_version, show_usage;
  char *local_file, *cmd_listen_port;
  int inetd_mode, no_daemon;

  /* Set up some globals */
  progname = argv[0];
  listen_port = x_strdup(DEFAULT_LISTEN_PORT);

#ifndef DEBUG
  no_daemon = 0;
#else /* DEBUG */
  no_daemon = 1;
#endif /* DEBUG */
  local_file = cmd_listen_port = 0;
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
      if (cfg_read(local_file, &listen_port, &g)) {
        /* If the local one didn't exist, set to 0 so we open
           global one */
        free(local_file);
        local_file = 0;
      } else {
        config_file = x_strdup(local_file);
      }
    }
  } else if (local_file) {
    if (cfg_read(local_file, &listen_port, &g)) {
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
    cfg_read(global_file, &listen_port, &g);
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

  /* Set signal handlers */
  signal(SIGTERM, sig_term);
  signal(SIGINT, sig_term);
  signal(SIGHUP, sig_hup);
  signal(SIGCHLD, sig_child);
#ifdef DEBUG_MEMORY
  signal(SIGUSR1, sig_usr);
  signal(SIGUSR2, sig_usr);
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
      debug("Running in the background");

      switch (fork()) {
        case -1:
          syscall_fail("fork", "first", 0);
          return -1;
        case 0:
          break;
        default:
          return 0;
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
          return 0;
      }
      
      /* Set our umask */    
      umask(0);

      /* Okay, we're in the background now */
      in_background = 1;
    }

  } else {
    debug("Inetd SuperTed mode!");

    /* running under inetd means we are backgrounded right *now* */
    in_background = 1;

    /* Hook STDIN into a new proxy */
    ircnet_hooksocket(STDIN_FILENO);
  }
 
  /* Open a connection to syslog if we're in the background */
  if (in_background)
    openlog(PACKAGE, LOG_PID, LOG_USER);
  
  /* Main loop! */
  while (!stop_poll) {
    int ns, nt;

    ircnet_expunge_proxies();
    ns = net_poll();
    nt = timer_poll();

    if (!ns && !nt)
      break;
  }

  /* Free up stuff */
  ircnet_flush();
  dns_flush();
  timer_flush();


  /* Do a lingering close on all sockets */
  net_closeall();
  net_flush();

  /* Close down and free up memory */
  if (!inetd_mode && !no_daemon)
    closelog();
  free(listen_port);
  free(config_file);

#ifdef DEBUG_MEMORY
  mem_report("termination");
#endif /* DEBUG_MEMORY */

  return 0;
}

/* Signal to stop polling */
static void sig_term(int sig) {
  debug("Received signal %d to stop", sig);
  stop();
}

/* Signal to reload configuration file */
static void sig_hup(int sig) {
  struct ircconnclass *oldclasses, *c;
  struct globalvars newglobals;
  char *new_listen_port;

  debug("Received signal %d to reload from %s", sig, config_file);
  new_listen_port = x_strdup(DEFAULT_LISTEN_PORT);
  oldclasses = connclasses;
  connclasses = 0;

  if (cfg_read(config_file, &new_listen_port, &newglobals)) {
    /* Config file reload failed */
    error("Reload of configuration file %s failed", config_file);
    ircnet_flush_connclasses(&connclasses);

  /* Copy over new globals */
  memcpy(&g, &newglobals, sizeof(struct globalvars));
    connclasses = oldclasses;
    free(new_listen_port);
    return;
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
      ircserver_send_peercmd(p, "QUIT", ":Permission revoked - %s %s",
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

  /* Restore the signal */
  signal(sig, sig_hup);
}

/* Signal to reap child process */
static void sig_child(int sig) {
  int status;
  pid_t pid;

  debug("Received signal %d to reap", sig);
  pid = wait(&status);
  debug("Reaped process %d, exit status %d", pid, status);

  /* Handle any DNS children */
  dns_endrequest(pid, status);

  /* Restore the signal */
  signal(sig, sig_child);
}


#ifdef DEBUG_MEMORY
/* On USR signals, dump debug information */
static void sig_usr(int sig) {
  mem_report(sig == SIGUSR1 ? 0 : "signal");
  signal(sig, sig_usr);
}
#endif /* DEBUG_MEMORY */

/* Print the usage instructions to stderr */
static int _print_usage(void) {
  fprintf(stderr, "%s: Try '%s --help' for more information.\n",
          progname, progname);

  return 0;
}

/* Print the version number to stderr */
static int _print_version(void) {
  fprintf(stderr, "%s %s\n", PACKAGE, VERSION);

  return 0;
}

/* Print the help to stderr */
static int _print_help(void) {
  fprintf(stderr, "%s.  Detachable IRC proxy.\n\n", PACKAGE);
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
  fprintf(stderr, "  -f, --config-file=FILE  Use this file instead of the "
                                            "default\n\n");

  return 0;
}

/* Called when a system call fails.  Print it to stderr or syslog */
int syscall_fail(const char *function, const char *arg, const char *message) {
  char *msg;

  msg = x_sprintf("%s(%s) failed: %s", function, (arg ? arg : ""),
                  (message ? message : strerror(errno)));
  if (in_background) {
    syslog(LOG_NOTICE, "%s", msg);
  } else {
#ifdef DEBUG
    fprintf(stderr, "%s: %c[33;1m%s%c[m\n", progname, 27, msg, 27);
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
    fprintf(stderr, "%s: %c[31;1m%s%c[m\n", progname, 27, msg, 27);
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
