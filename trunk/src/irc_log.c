/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_log.c
 *  - Handling of log files
 *  - Handling of log programs
 *  - Recalling from log files
 * --
 * @(#) $Id: irc_log.c,v 1.23 2000/10/13 12:44:20 keybuk Exp $
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
static int _irclog_pipe(const char *, char, const char *, char, const char *,
                        const char *);
static int _irclog_writetext(struct logfile *, char, const char *, char,
                             const char *, int, const char *,
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
    log->maxlines = p->conn_class->other_log_maxsize;
    debug("Log is other_log, using '%s/%s'", dir, filename);
  } else {
    dir = (p->conn_class->chan_log_dir ? p->conn_class->chan_log_dir
                                       : p->temp_logdir);
    ptr = filename = x_strdup(to);
    log->maxlines = p->conn_class->chan_log_maxsize;
    debug("Log is chan_log, using '%s/%s'", dir, filename);
  }

  /* Channel names are allowed to contain . and / according to the IRC
     protocol.  These are nasty as it means someone could theoretically
     create a channel called #/../../etc/passwd and the program would try
     to unlink "/tmp/#/../../etc/passwd" = "/etc/passwd".  If running as root
     this could be bad.  So to compensate we replace '/' with ':' as thats not
     valid in channel names. */
  while (*ptr) {
    switch (*ptr) {
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

  if (log->open)
    _irclog_close(log);

  debug("Freeing up log file '%s'", log->filename);
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
      free(line);
      return 0;
    } else if (!strlen(buff)) {
      free(line);
      return 0;
    } else {
      char *ptr;

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

  if (log->maxlines && (log->nlines >= log->maxlines)) {
    FILE *out;
    char *l;

    /* We can't simply add .tmp or something on the end, because there is
       always a possibility that might be a channel name.  Besides using
       temporary files always looks icky to me.  This "Sick Puppy" way of
       reading from an unlinked file sits with me much better (says a lot
       about me, that) */
    fseek(log->file, 0, SEEK_SET);
    unlink(log->filename);

    /* This *really* shouldn't happen */
    out = fopen(log->filename, "w+");
    if (!out) {
      syscall_fail("fopen", log->filename, 0);
      return -1;
    }

    /* Eat from the start */
    while ((log->nlines >= log->maxlines) && (l = _irclog_read(log))) {
      free(l);
      log->nlines--;
    }

    /* Write the rest */
    while ((l = _irclog_read(log))) {
      fprintf(out, "%s\n", l);
      free(l);
    }

    /* Close the input file, thereby *whoosh*ing it */
    fclose(log->file);
    log->file = out;
  }

  fseek(log->file, 0, SEEK_END);
  fprintf(log->file, "%s\n", msg);
  fflush(log->file);
  log->nlines++;

  free(msg);
  return 0;
}

/* Write a line through a pipe to a given program */
static int _irclog_pipe(const char *program, char prefrom, const char *from,
                        char postfrom, const char *to, const char *text) {
  int p[2], pid;

  if (!program)
    return 1;

  /* Prepare a pipe */
  if (pipe(p)) {
    syscall_fail("pipe", 0, 0);
    return 1;
  }

  /* Do the fork() thing */
  pid = fork();
  if (pid == -1) {
    syscall_fail("fork", 0, 0);
    return 1;

  } else if (pid) {
    FILE *fd;

    /* Parent - write text to a file descriptor of the pipe */
    close(p[0]);
    fd = fdopen(p[1], "w");
    if (!fd) {
      syscall_fail("fdopen", 0, 0);
      close(p[1]);
      return 1;
    }
    fprintf(fd, "%s\n", text);
    fflush(fd);
    fclose(fd);

  } else {
    char *nick;

    /* Child, copy pipe to STDIN then exec the process */
    close(p[1]);
    if (dup2(p[0], STDIN_FILENO) != STDIN_FILENO) {
      syscall_fail("dup2", 0, 0);
      close(p[0]);
      return 1;
    }
    
    nick = x_sprintf("%c%s%c", prefrom, from, postfrom);
    execlp(program, program, nick, to, 0);

    /* We can't get here.  Well we can, it means something went wrong */
    syscall_fail("execlp", program, 0);
    exit(10);
  }

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
                             const char *from, char postfrom, const char *to,
                             int timestamp, const char *prog,
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
  _irclog_pipe(prog, prefrom, from, postfrom, to, text);

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
    char *program;
    
    /* Write to one file */
    log = _irclog_getlog(p, to);
    if (!log)
      return -1;

    if (log == &(p->other_log)) {
      timestamp = p->conn_class->other_log_timestamp;
      program = p->conn_class->other_log_program;
    } else {
      timestamp = p->conn_class->chan_log_timestamp;
      program = p->conn_class->chan_log_program;
    }

    _irclog_writetext(log, prefrom, from, postfrom, to, timestamp, program,
                      format, ap);
  } else {
    struct ircchannel *c;

    /* Write to all files */
    _irclog_writetext(&(p->other_log), prefrom, from, postfrom, to,
                      p->conn_class->other_log_timestamp,
                      p->conn_class->other_log_program, format, ap);
    c = p->channels;
    while (c) {
      _irclog_writetext(&(c->log), prefrom, from, postfrom, to,
                        p->conn_class->chan_log_timestamp,
                        p->conn_class->other_log_program, format, ap);
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

  /* Don't recall anything */
  if (!recall)
    return 0;
  
  /* Recall everything */
  if (recall == -1) {
    start = 0;
  } else {
    start = (recall > log->nlines ? 0 : log->nlines - recall);
  }
  lines = log->nlines - start;

  return _irclog_recall(p, log, start, lines, to, 0);
}

/* Called to manually recall stuff */
int irclog_recall(struct ircproxy *p, const char *to,
                  unsigned long start, unsigned long lines, const char *from) {
  struct logfile *log;

  log = _irclog_getlog(p, to);
  if (!log)
    return -1;

  /* Recall everything */
  if (lines == -1) {
    start = 0;
    lines = log->nlines;
  }

  /* Recall recent */
  if (start == -1)
    start = (lines > log->nlines ? 0 : log->nlines - lines);

  return _irclog_recall(p, log, start, lines, to, from);
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
