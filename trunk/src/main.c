/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * main.c
 *  - Program main loop
 * --
 * @(#) $Id: main.c,v 1.23 2000/08/25 09:42:00 keybuk Exp $
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
#include "timers.h"

/* forward declarations */
static void sig_term(int);
#ifdef DEBUG_MEMORY
static void sig_usr(int);
#endif /* DEBUG_MEMORY */
static int _print_usage(void);
static int _print_version(void);
static int _print_help(void);

/* This is so "ident" and "what" can query version etc - useful (not) */
const char *rcsid = "@(#) $Id: main.c,v 1.23 2000/08/25 09:42:00 keybuk Exp $";

/* The name of the program */
char *progname;

/* whether we went in the background or not */
int in_background = 0;

/* This will be going before the next release! */
unsigned long log_autorecall = DEFAULT_LOG_AUTORECALL;

/* set to 1 to abort the main loop */
static int stop_poll = 0;

/* Long options */
struct option long_opts[] = {
  { "help", 0, NULL, 'h' },
  { "version", 0, NULL, 'v' },
#ifndef DEBUG
  { "no-daemon", 0, NULL, 'D' },
#else /* DEBUG */
  { "daemon", 0, NULL, 'D' },
#endif /* DEBUG */
  { "inetd", 0, NULL, 'I' },
  { "listen-port", 1, NULL, 'P' },
  { "config-file", 1, NULL, 'f' }
};

/* Options */
#define GETOPTIONS "hvDIP:f:"

/* We need this */
int main(int argc, char *argv[]) {
  char *local_file, *cmd_listen_port, *listen_port;
  int optc, show_help, show_version, show_usage;
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
  if (!local_file) {
    struct passwd *pw;

    pw = getpwuid(geteuid());
    if (pw && pw->pw_dir) {
      local_file = x_sprintf("%s/%s", pw->pw_dir, USER_CONFIG_FILENAME);
      debug("Local config file: %s", local_file);
      if (cfg_read(local_file, &listen_port)) {
        /* If the local one didn't exist, set to 0 so we open
           global one */
        free(local_file);
        local_file = 0;
      }
    }
  } else {
    if (cfg_read(local_file, &listen_port)) {
      /* This is fatal! */
      fprintf(stderr, "%s: Couldn't read configuration from %s: %s\n",
              progname, local_file, strerror(errno));
      free(local_file);
      return 2;
    }
  }

  /* Read global config file if local one not found */
  if (!local_file) {
    char *global_file;

    /* Not fatal if it doesn't exist */
    global_file = x_sprintf("%s/%s", SYSCONFDIR, GLOBAL_CONFIG_FILENAME);
    debug("Global config file: %s", global_file);
    cfg_read(global_file, &listen_port);
    free(global_file);
  } else {
    free(local_file);
  }

  /* Check we got some connection classes */
  if (!connclasses) {
    fprintf(stderr, "No configuration classes are defined.  You need to "
            "create a\nconfiguration file and define some in it.\n");
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
#ifdef DEBUG_MEMORY
  signal(SIGUSR1, sig_usr);
  signal(SIGUSR2, sig_usr);
#endif /* DEBUG_MEMORY */

  /* Broken Pipe?  This means that someone disconnected while we were
     sending stuff.  Naughty! */
  signal(SIGPIPE, SIG_IGN);

  if (!inetd_mode) {
    /* go daemon here */
    if (!no_daemon) {
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

      /* Ignore HUP signals */
      signal(SIGHUP, SIG_IGN);

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

      /* Open a connection to syslog */
      openlog(progname, LOG_PID, LOG_USER);

      /* Okay, we're in the background now */
      in_background = 1;
    }

    if (ircnet_listen(listen_port)) {
      fprintf(stderr, "%s: Unable to establish listen port\n", progname);
      return 2;
    }
  } else {
    ircnet_hooksocket(STDIN_FILENO);

    /* running under inetd means we are backgrounded */
    in_background = 1;
  }
  
  /* Main loop! */
  while (!stop_poll) {
    int ns, nt;

    ns = ircnet_poll();
    nt = timer_poll();

    if (!ns && !nt)
      stop_poll = 1;
  }

  /* Free up stuff */
  ircnet_flush();
  timer_flush();
  if (!inetd_mode && !no_daemon)
    closelog();
  free(listen_port);

#ifdef DEBUG_MEMORY
  mem_report("termination");
#endif /* DEBUG_MEMORY */

  return 0;
}

/* Signal to stop polling */
static void sig_term(int sig) {
  debug("Received signal %d to stop", sig);
  stop_poll = 1;
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
  fprintf(stderr, "Try '%s --help' for more information.\n", progname);

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
    fprintf(stderr, "%s: %s\n", progname, msg);
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
  msg = x_vsprintf(format, ap);
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
