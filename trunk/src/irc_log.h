/* dircproxy
 * Copyright (C) 2002 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_log.h
 * --
 * @(#) $Id: irc_log.h,v 1.12 2002/10/22 13:25:45 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_IRC_LOG_H
#define __DIRCPROXY_IRC_LOG_H

/* required includes */
#include <stdio.h>
#include "irc_net.h"

/* special log destination parameters */
#define IRC_LOGFILE_ALL ((char *)-1)
#define IRC_LOGFILE_SERVER ((char *)0)

/* types of event we can log */
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

/* functions */
extern int irclog_maketempdir(struct ircproxy *);
extern int irclog_closetempdir(struct ircproxy *);
extern int irclog_init(struct ircproxy *, const char *);
extern void irclog_free(struct logfile *);
extern int irclog_open(struct ircproxy *, const char *);
extern void irclog_close(struct ircproxy *, const char *);
extern int irclog_log(struct ircproxy *, int, const char *, const char *,
                      const char *, ...);
extern int irclog_autorecall(struct ircproxy *, const char *);
extern int irclog_recall(struct ircproxy *, const char *, long, long,
                         const char *);

int	    irclog_strtoflag(const char *);
const char *irclog_flagtostr(int);

#endif /* __DIRCPROXY_IRC_LOG_H */
