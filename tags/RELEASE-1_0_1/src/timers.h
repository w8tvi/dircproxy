/* dircproxy
 * Copyright (C) 2001 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * timers.h
 * --
 * @(#) $Id: timers.h,v 1.3 2001/01/11 15:29:21 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_TIMERS_H
#define __DIRCPROXY_TIMERS_H

/* handy defines */
#define TIMER_FUNCTION(_FUNC) ((void (*)(void *, void *)) _FUNC)

/* functions */
extern int timer_exists(void *, const char *);
extern char *timer_new(void *, const char *, unsigned long,
                       void (*)(void *, void *), void *);
extern int timer_del(void *, char *);
extern int timer_delall(void *);
extern int timer_poll(void);
extern void timer_flush(void);

#endif /* __DIRCPROXY_TIMERS_H */
