/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_log.h
 * --
 * @(#) $Id: irc_log.h,v 1.2 2000/05/24 20:30:46 keybuk Exp $
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

/* functions */
extern int irclog_makedir(struct ircproxy *);
extern int irclog_closedir(struct ircproxy *);
extern int irclog_open(struct ircproxy *, const char *, struct logfile *);
extern void irclog_close(struct logfile *);
extern int irclog_write(struct logfile *, const char *, ...);
extern int irclog_write_to(struct ircproxy *, const char *, const char *, ...);
extern int irclog_notice_to(struct ircproxy *, const char *, const char *, ...);
extern int irclog_notice_toall(struct ircproxy *, const char *, ...);
extern char *irclog_read(struct logfile *);
extern int irclog_startread(struct logfile *, unsigned long);
extern int irclog_recall(struct ircproxy *, struct logfile *, unsigned long);

#endif /* __DIRCPROXY_IRC_LOG_H */
