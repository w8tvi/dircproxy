/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * main.c
 *  - Program main loop
 * --
 * @(#) $Id: main.c,v 1.7 2000/05/24 17:42:48 keybuk Exp $
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
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>

#include <dircproxy.h>
#include "getopt.h"
#include "sprintf.h"
#include "cfgfile.h"
#include "irc_net.h"
#include "timers.h"

/* forward declarations */
static void sig_term(int);
#ifdef DEBUG
static void sig_usr(int);
#endif /* DEBUG */
static int _print_usage(void);
static int _print_version(void);
static int _print_help(void);

/* This is so "ident" and "what" can query version etc - useful (not) */
const char *rcsid = "@(#) $Id: main.c,v 1.7 2000/05/24 17:42:48 keybuk Exp $";

/* The name of the program */
char *progname;

/* Configuration variables */
char *listen_port = 0;
char *server_port = 0;
long server_retry = DEFAULT_SERVER_RETRY;
long server_dnsretry = DEFAULT_SERVER_DNSRETRY;
long server_maxattempts = DEFAULT_SERVER_MAXATTEMPTS;
long server_maxinitattempts = DEFAULT_SERVER_MAXINITATTEMPTS;
long channel_rejoin = DEFAULT_CHANNEL_REJOIN;
unsigned long log_autorecall = DEFAULT_LOG_AUTORECALL;

/* set to 1 to abort the main loop */
static int stop_poll = 0;

/* Long options */
struct option long_opts[] = {
  { "help", 0, NULL, 'h' },
  { "version", 0, NULL, 'v' },
  { "no-daemon", 0, NULL, 'D' },
  { "inetd", 0, NULL, 'I' },
  { "no-global-config", 0, NULL, 'G' },
  { "listen-port", 0, NULL, 'P' },
  { "config-file", 0, NULL, 'f' }
};

/* Options */
#define GETOPTIONS "hvDIGP:f:"

/* We need this */
int main(int argc, char *argv[]) {
  int optc, show_help, show_version, show_usage;
  int inetd_mode, no_daemon, no_global_config;
  char *local_file;

  /* Set up some globals */
  progname = argv[0];
  listen_port = x_strdup(DEFAULT_LISTEN_PORT);
  server_port = x_strdup(DEFAULT_SERVER_PORT);

#ifndef DEBUG
  no_daemon = 0;
#else /* DEBUG */
  no_daemon = 1;
#endif
  local_file = 0;
  show_help = show_version = show_usage = inetd_mode = no_global_config = 0;
  while ((optc = getopt_long(argc, argv, GETOPTIONS, long_opts, NULL)) != -1) {
    switch (optc) {
      case 'h':
        show_help = 1;
        break;
      case 'v':
        show_version = 1;
        break;
      case 'D':
        no_daemon = 1;
        break;
      case 'I':
        inetd_mode = 1;
        break;
      case 'G':
        no_global_config = 1;
        break;
      case 'P':
        free(listen_port);
        listen_port = x_strdup(optarg);
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

  /* Set signal handlers */
  signal(SIGTERM, sig_term);
#ifdef DEBUG
  signal(SIGUSR1, sig_usr);
  signal(SIGUSR2, sig_usr);
#endif /* DEBUG */

  /* Broken Pipe?  This means that someone disconnected while we were
     sending stuff.  Naughty! */
  signal(SIGPIPE, SIG_IGN);

  if (!inetd_mode) {
    /* go daemon here */
    if (!no_daemon) {
      switch (fork()) {
        case -1:
          DEBUG_SYSCALL_FAIL("fork");
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
          DEBUG_SYSCALL_FAIL("fork");
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
    }

    if (ircnet_listen(DEFAULT_LISTEN_PORT)) {
      fprintf(stderr, "%s: Unable to establish listen port\n", progname);
      return 2;
    }
  } else {
    ircnet_hooksocket(STDIN_FILENO);
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
  free(server_port);
  free(listen_port);

#ifdef DEBUG
  mem_report("termination");
#endif /* DEBUG */

  return 0;
}

/* Signal to stop polling */
static void sig_term(int sig) {
  stop_poll = 1;
}

#ifdef DEBUG
/* On USR signals, dump debug information */
static void sig_usr(int sig) {
  mem_report(sig == SIGUSR1 ? 0 : "signal");
  signal(sig, sig_usr);
}
#endif /* DEBUG */

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
  fprintf(stderr, "  -D, --no-daemon         Remain in the foreground\n");
  fprintf(stderr, "  -I, --inetd             Being run from inetd "
                                            "(implies -D)\n");
  fprintf(stderr, "  -G, --no-global-config  Do not read the global "
                                            "configuration file\n");
  fprintf(stderr, "  -P, --listen-port=PORT  Port to listen for clients on\n");
  fprintf(stderr, "  -f, --config-file=FILE  Use this file instead of "
                                            "~/%s\n\n", USER_CONFIG_FILENAME);

  return 0;
}
