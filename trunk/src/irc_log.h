/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_log.h
 * --
 * @(#) $Id: irc_log.h,v 1.3 2000/08/29 11:12:31 keybuk Exp $
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
extern int irclog_maketempdir(struct ircproxy *);
extern int irclog_closetempdir(struct ircproxy *);
extern int irclog_open(struct ircproxy *, const char *);
extern void irclog_close(struct ircproxy *, const char *);
extern void irclog_free(struct logfile *);
extern int irclog_msg(struct ircproxy *, const char *, const char *,
                      const char *, ...);
extern int irclog_notice(struct ircproxy *, const char *, const char *,
                         const char *, ...);
extern int irclog_autorecall(struct ircproxy *, const char *);

#endif /* __DIRCPROXY_IRC_LOG_H */
