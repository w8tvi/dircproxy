/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * timers.h
 * --
 * @(#) $Id: timers.h,v 1.1 2000/05/13 02:14:26 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_TIMERS_H
#define __DIRCPROXY_TIMERS_H

/* required includes */
#include "irc_net.h"

/* functions */
extern int timer_exists(struct ircproxy *, const char *);
extern char *timer_new(struct ircproxy *, const char *, unsigned long,
                       void (*)(struct ircproxy *, void *), void *);
extern int timer_del(struct ircproxy *, char *);
extern int timer_delall(struct ircproxy *);
extern int timer_poll(void);
extern void timer_flush(void);

#endif /* __DIRCPROXY_TIMERS_H */
