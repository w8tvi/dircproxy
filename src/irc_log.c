/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_log.c
 *  - Handling of log files
 * --
 * @(#) $Id: irc_log.c,v 1.15 2000/08/30 13:09:10 keybuk Exp $
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
#include <errno.h>
#include <time.h>

#include <dircproxy.h>
#include "sock.h"
#include "sprintf.h"
#include "irc_net.h"
#include "irc_prot.h"
#include "irc_client.h"
#include "irc_string.h"
#include "irc_log.h"

/* forward declarations */
static void _irclog_close(struct logfile *);
static struct logfile *_irclog_getlog(struct ircproxy *, const char *);
static char *_irclog_read(struct logfile *);
static int _irclog_write(struct logfile *, const char *, ...);
static int _irclog_writetext(struct logfile *, char, const char *, char, int,
                             const char *, va_list);
static int _irclog_text(struct ircproxy *, const char *, char, const char *,
                        char, const char *, va_list);
static int _irclog_recall(struct ircproxy *, struct logfile *, unsigned long,
                          unsigned long, const char *, const char *);

/* Log time format for strftime(3) */
#define LOG_TIME_FORMAT "%H:%M"

/* Log time/date format for strftime(3) */
#define LOG_TIMEDATE_FORMAT "%a, %d %b %Y %H:%M:%S %z"

/* Define MIN() */
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif /* MIN */

/* Create a temporary directory for log files */
int irclog_maketempdir(struct ircproxy *p) {
  struct passwd *pw;

  if (p->temp_logdir)
    return 0;

  pw = getpwuid(geteuid());
  if (pw) {
    struct stat statinfo;
    char *tmpdir;

    tmpdir = getenv("TMPDIR");
    debug("TMPDIR = '%s'", (tmpdir ? tmpdir : "(null)"));
    debug("Username = '%s'", pw->pw_name);
    debug("PID = '%d'", getpid());
    p->temp_logdir = x_sprintf("%s/%s-%s-%d-%d", (tmpdir ? tmpdir : "/tmp"),
                               PACKAGE, pw->pw_name, getpid(), p->client_sock);
    debug("temp_logdir = '%s'", p->temp_logdir);

    if (lstat(p->temp_logdir, &statinfo)) {
      if (errno != ENOENT) {
        syscall_fail("lstat", p->temp_logdir, 0);
        free(p->temp_logdir);
        p->temp_logdir = 0;
        return -1;
       } else if (mkdir(p->temp_logdir, 0700)) {
        syscall_fail("mkdir", p->temp_logdir, 0);
        free(p->temp_logdir);
        p->temp_logdir = 0;
        return -1;
      }
    } else if (!S_ISDIR(statinfo.st_mode)) {
      debug("Existed, but not directory");
      free(p->temp_logdir);
      p->temp_logdir = 0;
      return -1;
    }
  } else {
    debug("Couldn't get username!");
    return -1;
  }

  return 0;
}

/* Clean up temporary directory of log files */
int irclog_closetempdir(struct ircproxy *p) {
  if (!p->temp_logdir)
    return 0;

  debug("Freeing '%s'", p->temp_logdir);
  rmdir(p->temp_logdir);
  free(p->temp_logdir);
  p->temp_logdir = 0;
  return 0;
}

/* Open a log file. 0 = ok, -1 = error */
int irclog_open(struct ircproxy *p, const char *to) {
  char *ptr, *dir, *filename;
  struct logfile *log;

  log = _irclog_getlog(p, to);
  if (!log)
    return -1;

  if (log == &(p->other_log)) {
    dir = (p->conn_class->other_log_dir ? p->conn_class->other_log_dir
                                        : p->temp_logdir);
    ptr = filename = x_strdup("other");
    debug("Log is other_log, using '%s/%s'", dir, filename);
  } else {
    dir = (p->conn_class->chan_log_dir ? p->conn_class->chan_log_dir
                                       : p->temp_logdir);
    ptr = filename = x_strdup(to);
    debug("Log is chan_log, using '%s/%s'", dir, filename);
  }

  /* Channel names are allowed to contain . and / according to the IRC
     protocol.  These are nasty as it means someone could theoretically
     create a channel called #/../../etc/passwd and the program would try
     to unlink "/tmp/#/../../etc/passwd" = "/etc/passwd".  If running as root
     this could be bad.  So to compensate we replace '.' with ',' and '/' with
     ':'.  These two characters are NOT allowed in channel names and thus seem
     to make good replacements */
  while (*ptr) {
    switch (*ptr) {
      case '.':
        *ptr = ',';
        break;
      case '/':
        *ptr = ':';
        break;
    }

    ptr++;
  }

  if (log->filename)
    free(log->filename);
  log->filename = x_sprintf("%s/%s", dir, filename);
  free(filename);
  debug("log->filename = '%s'", log->filename);

  if (dir == p->temp_logdir) {
    debug("Going under temp_logdir");

    /* Unlink first for security, then open w+ */
    if (unlink(log->filename) && (errno != ENOENT)) {
      syscall_fail("unlink", log->filename, 0);
      free(log->filename);
      log->filename = 0;
      return -1;
    }

    log->file = fopen(log->filename, "w+");
    if (!log->file) {
      syscall_fail("fopen", log->filename, 0);
      free(log->filename);
      log->filename = 0;
      return -1;
    }
    log->open = 1;
    
    if (chmod(log->filename, 0600))
      syscall_fail("chmod", log->filename, 0);
    
    log->nlines = 0;
    log->keep = 0;

  } else {
    /* Open and append to existing files (as long as they are files) */
    struct stat statinfo;
    char *l, tbuf[40];
    time_t now;

    debug("Going under a user directory");

    if (lstat(log->filename, &statinfo)) {
      if (errno != ENOENT) {
        syscall_fail("lstat", p->temp_logdir, 0);
        free(log->filename);
        log->filename = 0;
        return -1;
      }
    } else if (!S_ISREG(statinfo.st_mode)) {
      debug("File existed, but wasn't a file");
      free(log->filename);
      log->filename = 0;
      return -1;
    }

    log->file = fopen(log->filename, "a+");
    if (!log->file) {
      syscall_fail("fopen", log->filename, 0);
      free(log->filename);
      log->filename = 0;
      return -1;
    }
    log->open = 1;
   
    fseek(log->file, 0, SEEK_SET);
    log->nlines = 0;
    while ((l = _irclog_read(log))) {
      free(l);
      log->nlines++;
    }
    debug("Counted %lu lines in the file", log->nlines);

    /* Output a "logging began" line */
    time(&now);
    strftime(tbuf, sizeof(tbuf), LOG_TIMEDATE_FORMAT, localtime(&now));
    _irclog_write(log, "* Logging started %s", tbuf);
    log->keep = 1;
  }

  return 0;
}

/* Close a log file */
void irclog_close(struct ircproxy *p, const char *to) {
  struct logfile *log;
  
  log = _irclog_getlog(p, to);
  if (!log)
    return;

  _irclog_close(log);
}

/* Actually close a log file */
static void _irclog_close(struct logfile *log) {
  if (!log->open)
    return;

  debug("Closing log file '%s'", log->filename);

  if (log->keep) {
    /* Output a "logging ended" line */
    char tbuf[40];
    time_t now;

    time(&now);
    strftime(tbuf, sizeof(tbuf), LOG_TIMEDATE_FORMAT, localtime(&now));
    _irclog_write(log, "* Logging finished %s", tbuf);
  }
    
  fclose(log->file);
  log->open = 0;
}

/* Actually close and free a log file */
void irclog_free(struct logfile *log) {
  if (!log->filename)
    return;

  debug("Freeing up log file '%s'", log->filename);

  if (log->open)
    _irclog_close(log);
  if (!log->keep)
    unlink(log->filename);
  free(log->filename);
  log->keep = log->nlines = 0;
}

/* Get a log file structure out of an ircproxy */
static struct logfile *_irclog_getlog(struct ircproxy *p, const char *to) {
  struct ircchannel *c;
  
  if (!to)
    return 0;

  c = ircnet_fetchchannel(p, to);
  if (c) {
    return &(c->log);
  } else {
    return &(p->other_log);
  }
}

/* Read a line from the log */
static char *_irclog_read(struct logfile *log) {
  char buff[512], *line;

  if (!log->open)
    return 0;

  line = 0;
  while (1) {
    if (!fgets(buff, 512, log->file)) {
      debug("fgets() failed, no end-of-line reached");
      free(line);
      return 0;
    } else if (!strlen(buff)) {
      debug("fgets() returned empty string, no end-of-line reached");
      free(line);
      return 0;
    } else {
      char *ptr;

      debug("fgets() returned a string");
      
      if (line) {
        char *new;

        new = x_sprintf("%s%s", line, buff);
        free(line);
        line = new;
      } else {
        line = x_strdup(buff);
      }

      ptr = line + strlen(line) - 1;
      if (*ptr == '\n') {
        debug("Ended in \\n");
        while ((ptr >= line) && (*ptr <= 32)) *(ptr--) = 0;
        break;
      }
    }
  }

  return line;
}

/* Write a line to the log */
static int _irclog_write(struct logfile *log, const char *format, ...) {
  va_list ap;
  char *msg;

  if (!log->open)
    return -1;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  fseek(log->file, 0, SEEK_END);
  fprintf(log->file, "%s\n", msg);
  fflush(log->file);
  log->nlines++;

  free(msg);
  return 0;
}

/* Write a PRIVMSG to log file(s) */
int irclog_msg(struct ircproxy *p, const char *to, const char *from,
               const char *format, ...) {
  va_list ap;
  int ret;

  va_start(ap, format);
  ret = _irclog_text(p, to, '<', from, '>', format, ap);
  va_end(ap);

  return ret;
}

/* Write a NOTICE to log file(s) */
int irclog_notice(struct ircproxy *p, const char *to, const char *from,
                  const char *format, ...) {
  va_list ap;
  int ret;

  va_start(ap, format);
  ret = _irclog_text(p, to, '-', from, '-', format, ap);
  va_end(ap);

  return ret;
}

/* Write some text to a log file */
static int _irclog_writetext(struct logfile *log, char prefrom,
                             const char *from, char postfrom, int timestamp,
                             const char *format, va_list ap) {
  char *text;

  text = x_vsprintf(format, ap);

  if (timestamp) {
    char tbuf[40];
    time_t now;

    time(&now);
    strftime(tbuf, sizeof(tbuf), LOG_TIME_FORMAT, localtime(&now));
    _irclog_write(log, "%c%s%c [%s] %s", prefrom, from, postfrom, tbuf, text);
  } else {
    _irclog_write(log, "%c%s%c %s", prefrom, from, postfrom, text);
  }

  free(text);

  return 0;
}

/* Write some text to log file(s) */
static int _irclog_text(struct ircproxy *p, const char *to, char prefrom,
                        const char *from, char postfrom, const char *format,
                        va_list ap) {
  if (to) {
    struct logfile *log;
    int timestamp;
    
    /* Write to one file */
    log = _irclog_getlog(p, to);
    if (!log)
      return -1;

    timestamp = (log == &(p->other_log) ? p->conn_class->other_log_timestamp
                                        : p->conn_class->chan_log_timestamp);

    _irclog_writetext(log, prefrom, from, postfrom, timestamp, format, ap);
  } else {
    struct ircchannel *c;

    /* Write to all files */
    _irclog_writetext(&(p->other_log), prefrom, from, postfrom,
                      p->conn_class->other_log_timestamp, format, ap);
    c = p->channels;
    while (c) {
      _irclog_writetext(&(c->log), prefrom, from, postfrom,
                        p->conn_class->chan_log_timestamp, format, ap);
      c = c->next;
    }
  }

  return 0;
}

/* Called to automatically recall stuff */
int irclog_autorecall(struct ircproxy *p, const char *to) {
  unsigned long recall, start, lines;
  struct logfile *log;

  log = _irclog_getlog(p, to);
  if (!log)
    return -1;

  if (log == &(p->other_log)) {
    recall = p->conn_class->other_log_recall;
  } else {
    recall = p->conn_class->chan_log_recall;
  }

  start = (recall > log->nlines ? 0 : log->nlines - recall);
  lines = log->nlines - start;

  return _irclog_recall(p, log, start, lines, to, 0);
}

/* Called to do the recall from a log file */
static int _irclog_recall(struct ircproxy *p, struct logfile *log,
                          unsigned long start, unsigned long lines,
                          const char *to, const char *from) {
  FILE *file;
  int close;

  /* If the file isn't open, we have to open it and remember to close it
     later */
  if (log->open) {
    file = log->file;
    close = 0;
  } else if (log->filename) {
    file = fopen(log->filename, "r");
    if (!file) {
      ircclient_send_notice(p, "Couldn't open log file %s", log->filename);
      syscall_fail("fopen", log->filename, 0);
      return -1;
    }
    close = 1;
  } else {
    return -1;
  }

  /* Jump to the beginning */
  fseek(file, 0, SEEK_SET);

  if (start < log->nlines) {
    char *l;

    /* Skip start lines */
    while (start-- && (l = _irclog_read(log))) free(l);

    /* Make lines sensible */
    lines = MIN(lines, log->nlines - start);

    /* Recall lines */
    while (lines-- && (l = _irclog_read(log))) {
      /* Message or Notice lines, these require a bit of parsing */
      if ((*l == '<') || (*l == '-')) {
        char *src, *msg;

        /* Source starts one character in */
        src = l + 1;
        if (!*src) {
          free(l);
          continue;
        }

        /* Message starts after a space and the correct closing character */
        msg = strchr(l, ' ');
        if (!msg || (*(msg - 1) != (*l == '<' ? '>' : '-'))) {
          free(l);
          continue;
        }

        /* Delete the closing character and skip the space */
        *(msg - 1) = 0;
        msg++;

        /* Do filtering */
        if (from) {
          char *comp, *ptr;

          /* We just check the nickname, so strip off anything after the ! */
          comp = x_strdup(src);
          if ((ptr = strchr(comp, '!')))
            *ptr = 0;

          /* Check the nicknames are the same */
          if (irc_strcasecmp(comp, from)) {
            free(comp);
            free(l);
            continue;
          }
        }

        /* Send the line */
        sock_send(p->client_sock, ":%s %s %s :%s\r\n", src,
                  (*l == '<' ? "PRIVMSG" : "NOTICE"), to, msg);

      } else if (strncmp(l, "* ", 2)) {
        /* Anything thats not a comment gets sent as a notice */
        ircclient_send_notice(p, "%s", l);

      }

      free(l);
    }
  }

  /* Either close, or skip back to the end */
  if (close) {
    fclose(file);
  } else {
    fseek(file, 0, SEEK_END);
  }
  return 0;
}
