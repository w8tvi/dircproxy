/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * main.c
 *  - Program main loop
 * --
 * @(#) $Id: main.c,v 1.2 2000/05/13 02:34:59 keybuk Exp $
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

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>

#include <dircproxy.h>
#include "getopt.h"
#include "irc_net.h"
#include "timers.h"

/* forward declarations */
#ifdef DEBUG
static void sig_alarm(int);
static void sig_usr(int);
#endif /* DEBUG */
static int _print_usage(void);
static int _print_version(void);
static int _print_help(void);

/* This is so "ident" and "what" can query version etc - useful (not) */
const char *rcsid = "@(#) $Id: main.c,v 1.2 2000/05/13 02:34:59 keybuk Exp $";

/* The name of the program */
char *progname;

/* set to 1 to abort the main loop */
static int stop_poll = 0;

/* Long options */
struct option long_opts[] = {
  { "help", 0, NULL, 'h' },
  { "version", 0, NULL, 'v' },
  { "inetd", 0, NULL, 'I' },
  { "no-daemon", 0, NULL, 'D' },
  { "config-file", 0, NULL, 'f' },
  { "password", 0, NULL, 'p' },
  { "server", 0, NULL, 's' },
  { "bind-host", 0, NULL, 'H' },
  { "from-mask", 0, NULL, 'm' },
  { "listen-port", 0, NULL, 'P' }
};

/* Options */
#define GETOPTIONS "hvIDf:p:s:H:m:P:"

/* We need this */
int main(int argc, char *argv[]) {
  int optc, show_help, show_version, show_usage;
  int inetd_mode, no_daemon;

  progname = argv[0];

#ifndef DEBUG
  no_daemon = 0;
#else /* DEBUG */
  no_daemon = 1;
#endif
  show_help = show_version = show_usage = inetd_mode = 0;
  while ((optc = getopt_long(argc, argv, GETOPTIONS, long_opts, NULL)) != -1) {
    switch (optc) {
      case 'h':
        show_help = 1;
        break;
      case 'v':
        show_version = 1;
        break;
      case 'I':
        inetd_mode = 1;
        break;
      case 'D':
        no_daemon = 0;
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

#ifdef DEBUG
  /* Set signal handlers */
  signal(SIGALRM, sig_alarm);
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

    if (ircnet_listen(TODO_CFG_LISTENPORT)) {
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

#ifdef DEBUG
  mem_report("termination");
#endif /* DEBUG */

  return 0;
}

#ifdef DEBUG
/* Signal to stop polling */
static void sig_alarm(int sig) {
  stop_poll = 1;
  printf("Received ALARM\n");
}

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
  fprintf(stderr, "  -h, --help       Print a summary of the options\n");
  fprintf(stderr, "  -v, --version    Print the version number\n");
  fprintf(stderr, "  -D, --no-daemon  Remain in the foreground\n");
  fprintf(stderr, "  -I, --inetd      Being run from inetd (implies -D)\n\n");

  return 0;
}
