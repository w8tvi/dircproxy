/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * timers.c
 *  - Scheduling events
 * --
 * @(#) $Id: timers.c,v 1.9 2002/12/29 21:30:12 scott Exp $
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
#include "timers.h"

/* structure of a timer */
struct timer {
  char *id;
  time_t time;
  void (*function)(void *, void *);
  void *boundto;
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
int timer_exists(void *b, const char *id) {
  struct timer *t;

  t = timers;
  while (t) {
    if ((b == t->boundto) && !strcmp(id, t->id))
      return 1;
    t = t->next;
  }

  return 0;
}

/* Add a new timer */
char *timer_new(void *b, const char *id, unsigned long interval,
                void (*func)(void *, void *), void *data) {
  struct timer *t;

  if (id && timer_exists(b, id))
    return 0;

  t = (struct timer *)malloc(sizeof(struct timer));
  if (id) {
    t->id = x_strdup(id);
  } else {
    t->id = x_sprintf("timer%lu", nexttimer++);
  }
  t->time = (interval ? time(NULL) + interval : 0);
  t->function = func;
  t->boundto = b;
  t->data = data;

  t->next = timers;
  timers = t;

  debug("Timer %s will be triggered in %d seconds", t->id,
        (t->time ? t->time - time(NULL) : 0));
  return t->id;
}

/* Delete a timer */
int timer_del(void *b, char *id) {
  struct timer *t, *l;

  l = 0;
  t = timers;

  while (t) {
    if ((b == t->boundto) && !strcmp(id, t->id)) {
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
int timer_delall(void *b) {
  struct timer *t, *l;
  int numdone;

  l = 0;
  t = timers;
  numdone = 0;

  while (t) {
    if (t->boundto == b) {
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
      void (*function)(void *, void *);
      struct timer *n;
      void *b, *data;

      function = t->function;
      b = t->boundto;
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
        function(b, data);
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
