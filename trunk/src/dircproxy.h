/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * dircproxy.h
 * --
 * @(#) $Id: dircproxy.h,v 1.2 2000/05/13 02:38:03 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_DIRCPROXY_H
#define __DIRCPROXY_DIRCPROXY_H

/* required includes */
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#else /* HAVE_CONFIG_H */
#define PACKAGE "dircproxy"
#define VERSION "-debug"
#endif /* HAVE_CONFIG_H */

#include "memdebug.h"

/* handy debug macros */
#define DEBUG_SYSCALL_DOH(_func, _msg, _num) fprintf(stderr, \
                                               "%s: %s() failed: %s [%d]\n",\
                                               progname, (_func), (_msg), \
                                               (_num))
#define DEBUG_SYSCALL_FAIL(_func) DEBUG_SYSCALL_DOH((_func), strerror(errno), \
                                                    errno)

/* ack, make me a config file! */
#define TODO_CFG_LISTENPORT "57000"
#define TODO_CFG_DEFAULTPORT "6667"
#define TODO_CFG_PASS "foo"
#define TODO_CFG_SERVER "irc.linux.com:6667"
#define TODO_CFG_BINDHOST 0
#define TODO_CFG_RETRY 15
#define TODO_CFG_DNSRETRY 60
#define TODO_CFG_MAXINITATTEMPTS 5
#define TODO_CFG_MAXATTEMPTS 0
#define TODO_CFG_DETACHAWAY "Not available, messages are logged"
#define TODO_CFG_REJOIN 5
#define TODO_CFG_INITRECALL 128

/* global variables */
extern char *progname;

#endif /* __DIRCPROXY_DIRCPROXY_H */
