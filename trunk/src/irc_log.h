/* dircproxy
 * Copyright (C) 2002 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_log.h
 * --
 * $Id: irc_log.h,v 1.13 2002/11/02 17:41:21 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef _DIRCPROXY_IRC_LOG_H
#define _DIRCPROXY_IRC_LOG_H

/* Required includes */
#include <stdio.h>

#include "irc_net.h"

/* Special log destination parameters */
#define IRC_LOGFILE_ALL ((char *)-1)
#define IRC_LOGFILE_SERVER ((char *)0)

/* Types of event we can log */
#define IRC_LOG_NONE   0x0000
#define IRC_LOG_MSG    0x0001
#define IRC_LOG_NOTICE 0x0002
#define IRC_LOG_ACTION 0x0004
#define IRC_LOG_CTCP   0x0008
#define IRC_LOG_JOIN   0x0010 
#define IRC_LOG_PART   0x0020
#define IRC_LOG_KICK   0x0040
#define IRC_LOG_QUIT   0x0080
#define IRC_LOG_NICK   0x0100
#define IRC_LOG_MODE   0x0200
#define IRC_LOG_TOPIC  0x0400
#define IRC_LOG_CLIENT 0x0800
#define IRC_LOG_SERVER 0x1000
#define IRC_LOG_ERROR  0x2000
#define IRC_LOG_ALL    0x3fff

/* Functions to initialise internal logging */
int irclog_maketempdir(IRCProxy *);
int irclog_init(IRCProxy *, const char *);
int irclog_open(IRCProxy *, const char *);

/* Functions to clean up internal logging */
void irclog_close(IRCProxy *, const char *);
void irclog_free(LogFile *);
void irclog_closetempdir(IRCProxy *);

/* Log a message */
int irclog_log(IRCProxy *, int, const char *, const char *, const char *, ...);

/* Recall log messages from the internal log */
extern int irclog_autorecall(struct ircproxy *, const char *);
extern int irclog_recall(struct ircproxy *, const char *, long, long,
                         const char *);

/* Convert numeric flags to string names and back again */
int	    irclog_strtoflag(const char *);
const char *irclog_flagtostr(int);

#endif /* !_DIRCPROXY_IRC_LOG_H */
