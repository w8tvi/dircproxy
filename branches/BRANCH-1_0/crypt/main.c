/* dircproxy-crypt
 * Copyright (C) 2000,2001,2002,2003 Scott James Remnant <scott@netsplit.com>.
 *
 * main.c
 *  - Encyrpt a password taken from stdin or the command line
 *
 * This is deliberately simple, it doesn't even malloc() anything (which,
 * for something I wrote, is amazing).
 * --
 * @(#) $Id: main.c,v 1.3.4.1 2002/12/29 21:33:35 scott Exp $
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
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#else /* HAVE_CONFIG_H */
#define PACKAGE "dircproxy"
#define VERSION "-debug"
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_CRYPT_H
#include <crypt.h>
#else /* HAVE_CRYPT_H */
#include <unistd.h>
#endif /* HAVE_CRYPT_H */

#include "getopt/getopt.h"

/* forward declarations */
static void _encrypt(const char *);
static void _saltchar(char *);
static int _print_usage(void);
static int _print_version(void);
static int _print_help(void);

/* This is so "ident" and "what" can query version etc - userful (not) */
const char *rcsid = "@(#) $Id: main.c,v 1.3.4.1 2002/12/29 21:33:35 scott Exp $";

/* The name of the program */
static char *progname;

/* Long options */
static struct option long_opts[] = {
  { "help", 0, NULL, 'h' },
  { "version", 0, NULL, 'v' },
  { 0, 0, 0, 0}
};

/* Options */
#define GETOPTIONS "hv"

/* The main func */
int main(int argc, char *argv[]) {
  int optc, show_help, show_version, show_usage;
  
  /* Get arguments */
  progname = argv[0];
  show_help = show_version = show_usage = 0;
  while ((optc = getopt_long(argc, argv, GETOPTIONS, long_opts, NULL)) != -1) {
    switch (optc) {
      case 'h':
        show_help = 1;
        break;
      case 'v':
        show_version = 1;
        break;
      default:
        show_usage = 1;
        break;
    }
  }

  if (show_usage) {
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

  /* Randomize */
  srand(time(0));

  if (optind < argc) {
    while (optind < argc) {
      _encrypt(argv[optind]);
      optind++;
    }
  } else {
    char pass[80], *ret;

    printf("Password: ");
    ret = fgets(pass, sizeof(pass), stdin);
    printf("\n");
    if (ret) {
      char *ptr;

      ptr = pass + strlen(pass);
      while ((ptr >= ret) && (*ptr <= 32)) *(ptr--) = 0;

      _encrypt(pass);
    }
  }

  return 0;
}

/* Encrypt a password */
static void _encrypt(const char *pass) {
  char salt[3], *enc;

  _saltchar(&(salt[0]));
  _saltchar(&(salt[1]));
  salt[2] = 0;

  enc = crypt(pass, salt);
  printf("%s = %s\n", pass, enc);
}

/* Pick a random salt character */
static void _saltchar(char *c) {
  static char *chars = "abcdefghijklmnopqrstuvwxyz"
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                       "01234567890";
  int ran;

  ran = (int)((double)(strlen(chars) - 1) * (rand() / (RAND_MAX + 1.0)));
  *c = chars[ran];
}

/* Print the usage instructions to stderr */
static int _print_usage(void) {
  fprintf(stderr, "%s: Try '%s --help' for more information.\n",
          progname, progname);

  return 0;
}

/* Print the version number to stderr */
static int _print_version(void) {
  fprintf(stderr, "dircproxy-crypt (%s %s)\n", PACKAGE, VERSION);

  return 0;
}

/* Print the help to stderr */
static int _print_help(void) {
  fprintf(stderr, "dircproxy-crypt.  Encyrpt passwords for %s.\n\n", PACKAGE);
  fprintf(stderr, "Usage: %s [OPTION]... [PASSWORD]...\n\n", progname);
  fprintf(stderr, "If a long option shows an argument as mandatory, then "
                  "it is mandatory\nfor the equivalent short option also.  "
                  "Similarly for optional arguments.\n\n");
  fprintf(stderr, "  -h, --help              Print a summary of the options\n");
  fprintf(stderr, "  -v, --version           Print the version number\n\n");
  fprintf(stderr, "  PASSWORD                Plaintext password to crypt\n\n");
  fprintf(stderr, "If no passwords are given on the command line, you will "
                  "be asked for one\non standard input.\n\n");

  return 0;
}
