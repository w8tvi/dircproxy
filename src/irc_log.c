/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_log.c
 *  - Handling of log files
 * --
 * @(#) $Id: irc_log.c,v 1.3 2000/05/13 04:41:55 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#include <pwd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>

#include <dircproxy.h>
#include "sock.h"
#include "sprintf.h"
#include "irc_net.h"
#include "irc_prot.h"
#include "irc_log.h"

/* forward declarations */
static int _irclog_write(struct logfile *, const char *, va_list);
static int _irclog_notice(struct ircproxy *, struct logfile *, const char *,
                          const char *, va_list);

/* Create a directory for log files */
int irclog_makedir(struct ircproxy *p) {
  struct passwd *pw;

  if (p->logdir)
    return 0;

  pw = getpwuid(geteuid());
  if (pw) {
    struct stat statinfo;
    char *tmpdir;

    tmpdir = getenv("TMPDIR");
    p->logdir = x_sprintf("%s/%s-%s-%d-%d", (tmpdir ? tmpdir : "/tmp"),
                          PACKAGE, pw->pw_name, getpid(), p->client_sock);

    if (lstat(p->logdir, &statinfo)) {
      if (errno != ENOENT) {
        DEBUG_SYSCALL_FAIL("lstat");
        free(p->logdir);
        p->logdir = 0;
        return -1;
       } else if (mkdir(p->logdir, 0700)) {
        DEBUG_SYSCALL_FAIL("mkdir");
        free(p->logdir);
        p->logdir = 0;
        return -1;
      }
    } else if (!S_ISDIR(statinfo.st_mode)) {
      free(p->logdir);
      p->logdir = 0;
      return -1;
    }
  } else {
    return -1;
  }

  return 0;
}

/* Clean up directory of log files */
int irclog_closedir(struct ircproxy *p) {
  if (!p->logdir)
    return 0;

  rmdir(p->logdir);

  free(p->logdir);
  p->logdir = 0;
  return 0;
}

/* Open a log file. 0 = ok, -1 = error */
int irclog_open(struct ircproxy *p, const char *filename, struct logfile *log) {
  char *ptr;

  if (!p->logdir)
    return 0;

  log->filename = x_sprintf("%s/%s", p->logdir, filename);

  /* Channel names are allowed to contain . and / according to the IRC
     protocol.  These are nasty as it means someone could theoretically
     create a channel called #/../../etc/passwd and the program would try
     to unlink "/tmp/#/../../etc/passwd" = "/etc/passwd".  If running as root
     this could be bad.  So to compensate we replace '.' with ',' and '/' with
     ':'.  These two characters are NOT allowed in channel names and thus seem
     to make good replacements */
  ptr = log->filename + strlen(p->logdir) + 1;
  while (*ptr) {
    switch (*ptr) {
      case '.':
        *ptr = ',';
        break;
      case ':':
        *ptr = ':';
        break;
    }

    ptr++;
  }

  if (unlink(log->filename) && (errno != ENOENT)) {
    DEBUG_SYSCALL_FAIL("unlink");
    free(log->filename);
    return -1;
  }

  log->file = fopen(log->filename, "w+");
  if (!log->file) {
    DEBUG_SYSCALL_FAIL("fopen");
    free(log->filename);
    return -1;
  }

  if (fchmod(log->file, 0600))
    DEBUG_SYSCALL_FAIL("fchmod");

  log->open = 1;
  log->nlines = 0;

  return 0;
}

/* Close and free a log file */
void irclog_close(struct logfile *log) {
  if (!log->open)
    return;

  fclose(log->file);
  unlink(log->filename);
  free(log->filename);
}

/* Do an actual write to a log file */
static int _irclog_write(struct logfile *log, const char *format, va_list ap) {
  char *msg;

  if (!log->open)
    return 0;

  msg = x_vsprintf(format, ap);

  fseek(log->file, 0, SEEK_END);
  fprintf(log->file, "%s\n", msg);
  fflush(log->file);
  log->nlines++;

  return 0;
}

/* Do an actual write of a notice to a log file */
static int _irclog_notice(struct ircproxy *p, struct logfile *log,
                          const char *to, const char *format, va_list ap) {
  char *msg;

  if (!log->open)
    return 0;

  msg = x_vsprintf(format, ap);

  fseek(log->file, 0, SEEK_END);
  fprintf(log->file, ":%s %s %s :%s\n", 
          (p->servername ? p->servername : PACKAGE), "NOTICE", to, msg);
  fflush(log->file);
  log->nlines++;

  return 0;
}

/* Write to a log file */
int irclog_write(struct logfile *log, const char *format, ...) {
  va_list ap;
  int ret;

  va_start(ap, format);
  ret = _irclog_write(log, format, ap);
  va_end(ap);

  return ret;
}

/* Find a log file and then write to it */
int irclog_write_to(struct ircproxy *p, const char *dest,
                    const char *format, ...) {
  struct ircchannel *c;
  struct logfile *log;
  va_list ap;
  int ret;

  c = ircnet_fetchchannel(p, dest);
  log = (c ? &(c->log) : &(p->misclog));

  va_start(ap, format);
  ret = _irclog_write(log, format, ap);
  va_end(ap);

  return ret;
}

/* Find a log file and then write a notice to it */
int irclog_notice_to(struct ircproxy *p, const char *dest,
                     const char *format, ...) {
  struct ircchannel *c;
  va_list ap;
  int ret;

  c = ircnet_fetchchannel(p, dest);

  va_start(ap, format);
  ret = _irclog_notice(p, (c ? &(c->log) : &(p->misclog)),
                       (c ? c->name : p->nickname), format, ap);
  va_end(ap);

  return ret;
}

/* Write a notice to all log files */
int irclog_notice_toall(struct ircproxy *p, const char *format, ...) {
  struct ircchannel *c;
  va_list ap;

  c = p->channels;
  while (c) {
    va_start(ap, format);
    _irclog_notice(p, &(c->log), c->name, format, ap);
    va_end(ap);

    c = c->next;
  }
  
  return 0; 
}

/* Read a line from the log */
char *irclog_read(struct logfile *log) {
  char buff[512];

  if (!log->open)
    return 0;

  if (!fgets(buff, 512, log->file)) {
    return 0;
  } else {
    char *tmp;

    tmp = buff + strlen(buff);
    while ((tmp >= buff) && (*tmp <= 32)) *(tmp--) = 0;

    tmp = strdup(buff);

    return tmp;
  }
}

/* Start a read from a certain line */
int irclog_startread(struct logfile *log, unsigned long skip) {
  unsigned long i;

  if (!log->open)
    return 0;

  fseek(log->file, 0, SEEK_SET);

  for (i = 0; i < skip; i++) {
    char *str;

    str = irclog_read(log);
    free(str);
  }

  return 0;
}

/* Do a recall */
int irclog_recall(struct ircproxy *p, struct logfile *log,
                  unsigned long numlines) {
  if (!log->open)
    return 0;

  printf("Doing recall of %lu lines from file with %lu lines\n", numlines,
         log->nlines);

  irclog_startread(log, (log->nlines > numlines ? log->nlines - numlines : 0));

  while (1) {
    char *str;
    str = irclog_read(log);
    if (str) {
      sock_send(p->client_sock, "%s\r\n", str);
      free(str);
    } else {
      break;
    }
  }

  return 0;
}
