/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * timers.c
 *  - Scheduling events
 * --
 * @(#) $Id: timers.c,v 1.5 2000/10/10 13:08:36 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dircproxy.h>
#include "sprintf.h"
#include "irc_net.h"
#include "timers.h"

/* structure of a timer */
struct timer {
  char *id;
  time_t time;
  struct ircproxy *proxy;
  void (*function)(struct ircproxy *, void *);
  void *data;
  
  struct timer *next;
};

/* forward declarations */
static int _timer_free(struct timer *);

/* list of current timers */
static struct timer *timers = 0;

/* next dynamic id */
static unsigned long nexttimer = 0;

/* Check if a timer exists */
int timer_exists(struct ircproxy *p, const char *id) {
  struct timer *t;

  t = timers;
  while (t) {
    if ((p == t->proxy) && !strcmp(id, t->id))
      return 1;
    t = t->next;
  }

  return 0;
}

/* Add a new timer */
char *timer_new(struct ircproxy *p, const char *id, unsigned long interval,
                void (*func)(struct ircproxy *, void *), void *data) {
  struct timer *t;

  if (id && timer_exists(p, id))
    return 0;

  t = (struct timer *)malloc(sizeof(struct timer));
  t->proxy = p;
  if (id) {
    t->id = x_strdup(id);
  } else {
    t->id = x_sprintf("timer%lu", nexttimer++);
  }
  t->time = (interval ? time(NULL) + interval : 0);
  t->function = func;
  t->data = data;

  t->next = timers;
  timers = t;

  debug("Timer %s will be triggered in %d seconds", t->id,
        (t->time ? t->time - time(NULL) : 0));
  return t->id;
}

/* Delete a timer */
int timer_del(struct ircproxy *p, char *id) {
  struct timer *t, *l;

  l = 0;
  t = timers;

  while (t) {
    if ((p == t->proxy) && !strcmp(id, t->id)) {
      if (l) {
        l->next = t->next;
      } else {
        timers = t->next;
      }

      debug("Timer %s will not be triggered (%d on the clock)", 
            t->id, (t->time ? t->time - time(NULL) : 0));
      _timer_free(t);
      return 0;
    } else {
      l = t;
      t = t->next;
    }
  }

  return -1;
}

/* Delete all timers with a certain ircproxy class */
int timer_delall(struct ircproxy *p) {
  struct timer *t, *l;
  int numdone;

  l = 0;
  t = timers;
  numdone = 0;

  while (t) {
    if (t->proxy == p) {
      struct timer *n;

      n = t->next;
      debug("Timer %s will not be triggered (%d on the clock)", 
            t->id, (t->time ? t->time - time(NULL) : 0));
      _timer_free(t);

      if (l) {
        t = l->next = n;
      } else {
        t = timers = n;
      }

      numdone++;
    } else {
      l = t;
      t = t->next;
    }
  }

  return numdone;
}

/* Poll the timers */
int timer_poll(void) {
  struct timer *t, *l;
  time_t ctime;

  l = 0;
  t = timers;
  ctime = time(NULL);

  while (t) {

    if (t->time <= ctime) {
      void (*function)(struct ircproxy *, void *);
      struct ircproxy *p;
      struct timer *n;
      void *data;

      function = t->function;
      p = t->proxy;
      data = t->data;
      n = t->next;
      debug("Timer %s triggered", t->id);
      _timer_free(t);

      if (l) {
        t = l->next = n;
      } else {
        t = timers = n;
      }

      if (function)
        function(p, data);
    } else {
      l = t;
      t = t->next;
    }
  }

  return (timers ? 1 : 0);
}

/* Free a timer */
static int _timer_free(struct timer *t) {
  free(t->id);
  free(t);
  return 0;
}

/* Get rid of all the proxies */
void timer_flush(void) {
  struct timer *t;

  t = timers;

  while (t) {
    struct timer *n;

    n = t->next;
    debug("Timer %s never triggered (%d on the clock)", 
          t->id, (t->time ? t->time - time(NULL) : 0));
    _timer_free(t);
    t = n;
  }

  timers = 0;
}
