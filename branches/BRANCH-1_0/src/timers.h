/* dircproxy
 * Copyright (C) 2000,2001,2002,2003 Scott James Remnant <scott@netsplit.com>.
 *
 * timers.h
 * --
 * @(#) $Id: timers.h,v 1.4.4.1 2002/12/29 21:33:39 scott Exp $
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
