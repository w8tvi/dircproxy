/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * dircproxy.h
 * --
 * @(#) $Id: dircproxy.h,v 1.3 2000/05/13 05:12:52 keybuk Exp $
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

/* Configuration values that aren't in the config file */

/* FALLBACK_USERNAME
 * Before sending username's to the server in a USER command, we strip it
 * of bogus characters.  It shouldn't happen, but if somehow it ends up with
 * no other characters left, this value will be used.
 */
#define FALLBACK_USERNAME "user"

/* FALLBACK_NICKNAME
 * When sending a nickname while detached, its possible that we can get
 * errors back from the server.  To this end, we have to generate a new
 * nickname, because the client isn't around to do it for us.  It really
 * will try to generate you a new one, but if all else fails we need
 * something to fall back on
 */
#define FALLBACK_NICKNAME "dircproxy"


/* Defaults for values in the configuration file.  Change the config file
 * instead of these */

/* DEFAULT_LISTEN_PORT
 * What port do we listen on for new client connections?
 */
#define DEFAULT_LISTEN_PORT "57000"

/* DEFAULT_SERVER_PORT
 * What port do we connect to IRC servers on if the server string doesn't
 * explicitly set one
 */
#define DEFAULT_SERVER_PORT "6667"

/* DEFAULT_SERVER_RETRY
 * How many seconds after disconnection or last connection attempt do we
 * wait before retrying again?
 */
#define DEFAULT_SERVER_RETRY 15

/* DEFAULT_SERVER_DNSRETRY
 * How many seconds after last connection attempt do we wait before trying
 * again if the error was DNS related?
 */
#define DEFAULT_SERVER_DNSRETRY 60

/* DEFAULT_MAXATTEMPTS
 * If we are disconnected from the server, how many times should we iterate
 * the server list before giving up and declaring the proxied connection
 * dead?
 * 0 = iterate forever
 */
#define DEFAULT_SERVER_MAXATTEMPTS 0

/* DEFAULT_SERVER_MAXINITATTEMPTS
 * On first connection, how many times should we iterate the server list
 * before giving up and declaring the proxied connection dead?
 * 0 = iterate forever, not recommended!
 */
#define DEFAULT_SERVER_MAXINITATTEMPTS 5

/* DEFAULT_CHANNEL_REJOIN
 * If we are kicked off a channel, how many seconds do we wait before attempting
 * to rejoin.
 * -1 = Don't rejoin
 *  0 = Immediately
 */
#define DEFAULT_CHANNEL_REJOIN 5

/* DEFAULT_LOG_AUTORECALL
 * How many lines of log off the bottom do we give to the client when it
 * reattaches?
 */
#define DEFAULT_LOG_AUTORECALL 128

/* DEFAULT_DETACH_AWAY
 * If the client detaches without leaving an AWAY message, set this as the
 * AWAY message until it comes back.
 * 0 = don't do this
 */
#define DEFAULT_DETACH_AWAY "Not available, messages are logged"

/* handy debug macros */
#define DEBUG_SYSCALL_DOH(_func, _msg, _num) fprintf(stderr, \
                                               "%s: %s() failed: %s [%d]\n",\
                                               progname, (_func), (_msg), \
                                               (_num))
#define DEBUG_SYSCALL_FAIL(_func) DEBUG_SYSCALL_DOH((_func), strerror(errno), \
                                                    errno)

/* ack, make me a config file! */
#define TODO_CFG_PASS "foo"
#define TODO_CFG_SERVER "irc.linux.com:6667"
#define TODO_CFG_BINDHOST 0

/* global variables */
extern char *progname;

#endif /* __DIRCPROXY_DIRCPROXY_H */
