/* dircproxy
 * Copyright (C) 2002 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * irc_log.c
 *  - Handling of log files
 *  - Handling of log programs
 *  - Recalling from log files
 * --
 * @(#) $Id: irc_log.c,v 1.41 2002/10/17 18:45:30 scott Exp $
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
#include "net.h"
#include "sprintf.h"
#include "irc_net.h"
#include "irc_prot.h"
#include "irc_client.h"
#include "irc_string.h"
#include "irc_log.h"

/* forward declarations */
static char *_irclog_safe(char *);
static void _irclog_close(struct logfile *);
static struct logfile *_irclog_getlog(struct ircproxy *, const char *);
static FILE *_irclog_openuser(struct ircproxy *, const char *);
static char *_irclog_read(FILE *);
static void _irclog_printf(FILE *, const char *, ...);
static int _irclog_write(struct logfile *, const char *, ...);
static int _irclog_pipe(struct ircproxy *, int, const char *, const char *,
                        const char *);
static int _irclog_writetext(struct ircproxy *, struct logfile *, int,
                             const char *, const char *, const char *);
static int _irclog_recall(struct ircproxy *, struct logfile *, unsigned long,
                          unsigned long, const char *, const char *);

/* Log time format for strftime(3) */
#define LOG_TIME_FORMAT "%H:%M"

/* User log time format */
#define LOG_USER_TIME_FORMAT "[%d %b %H:%M] "

/* Log time/date format for strftime(3) */
#define LOG_TIMEDATE_FORMAT "%a, %d %b %Y %H:%M:%S %z"

/* Define MIN() */
#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif /* MIN */

/* Create a temporary directory for log files */
int irclog_maketempdir(struct ircproxy *p) {
  struct passwd *pw;
  static unsigned int counter = 0;

  if (p->temp_logdir)
    return 0;

  pw = getpwuid(geteuid());
  if (pw) {
    struct stat statinfo;
    char *tmpdir;

    tmpdir = getenv("TMPDIR");
    if (!tmpdir)
      tmpdir = getenv("TEMP");
    debug("TMPDIR = '%s'", (tmpdir ? tmpdir : "(null)"));
    debug("Username = '%s'", pw->pw_name);
    debug("PID = '%d'", getpid());
    p->temp_logdir = x_sprintf("%s/%s-%s-%d-%d", (tmpdir ? tmpdir : "/tmp"),
                               PACKAGE, pw->pw_name, getpid(), counter++);
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

/* Make the name of a logfile safe */
static char *_irclog_safe(char *name) {
  char *ptr;

  /* Channel names are allowed to contain . and / according to the IRC
     protocol.  These are nasty as it means someone could theoretically
     create a channel called #/../../etc/passwd and the program would try
     to unlink "/tmp/#/../../etc/passwd" = "/etc/passwd".  If running as root
     this could be bad.  So to compensate we replace '/' with ':' as thats not
     valid in channel names. */
  ptr = name;
  while (*ptr) {
    switch (*ptr) {
      case '/':
        *ptr = ':';
        break;
    }

    ptr++;
  }

  return name;
}

/* Initialise a log file, opening the copy if necessary */
int irclog_init(struct ircproxy *p, const char *to) {
  char *filename;
  struct logfile *log;

  log = _irclog_getlog(p, to);
  if (!log)
    return -1;

  if (!p->temp_logdir)
    return -1;

  /* Store the config */
  if (log == &(p->server_log)) {
    debug("Initialising server log file");
    filename = x_strdup("server");
    log->maxlines = p->conn_class->server_log_maxsize;
    log->always = p->conn_class->server_log_always;

  } else if (log == &(p->private_log)) {
    debug("Initialising private log file");
    filename = x_strdup("private");
    log->maxlines = p->conn_class->private_log_maxsize;
    log->always = p->conn_class->private_log_always;

  } else {
    debug("Initialising channel log file for %s", to);
    filename = x_strdup(to);
    irc_strlwr(filename);
    log->maxlines = p->conn_class->chan_log_maxsize;
    log->always = p->conn_class->chan_log_always;
  }

  /* Store the filename */
  if (log->filename)
    free(log->filename);
  log->filename = x_sprintf("%s/%s", p->temp_logdir, _irclog_safe(filename));
  log->made = 0;
  debug("log->filename = '%s'", log->filename);
  free(filename);

  return 0;
}

/* Free up log file information */
void irclog_free(struct logfile *log) {
  if (!log->filename)
    return;

  if (log->open)
    _irclog_close(log);

  debug("Freeing up log file '%s'", log->filename);

  unlink(log->filename);
  free(log->filename);
  log->nlines = 0;
  log->made = 0;
}

/* Open a previously init'd log file. 0 = ok, -1 = error */
int irclog_open(struct ircproxy *p, const char *to) {
  struct logfile *log;

  log = _irclog_getlog(p, to);
  if (!log || !log->filename)
    return -1;
  if (log->open)
    return 0;

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
  log->open = log->made = 1;
  
  if (chmod(log->filename, 0600))
    syscall_fail("chmod", log->filename, 0);
  
  log->nlines = 0;

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
  fclose(log->file);
  log->open = 0;
}

/* Get a log file structure out of an ircproxy */
static struct logfile *_irclog_getlog(struct ircproxy *p, const char *to) {
  struct ircchannel *c;

  if (to) {
    c = ircnet_fetchchannel(p, to);
    if (c) {
      return &(c->log);
    } else {
      return &(p->private_log);
    }
  } else {
    return &(p->server_log);
  }
}

/* Open a user copy of a log file */
static FILE *_irclog_openuser(struct ircproxy *p, const char *to) {
  struct stat statinfo;
  char *filename, *userfile;
  FILE *user_log;

  if (!p->conn_class->log_dir)
    return 0;

  if (to == IRC_LOGFILE_ALL) {
    return 0;

  } else if (to == IRC_LOGFILE_SERVER) {
    filename = x_strdup("SERVER");
  
  } else {
    filename = x_strdup(to);
    irc_strlwr(filename);
  }

  /* Work out the copy filename, and clean up */
  userfile = x_sprintf("%s/%s.log",
                       p->conn_class->log_dir, _irclog_safe(filename));
  free(filename);

  /* Make sure it's safe */
  if (lstat(userfile, &statinfo)) {
    if (errno != ENOENT) {
      syscall_fail("lstat", userfile, 0);
      free(userfile);
      userfile = 0;
    }
  } else if (!S_ISREG(statinfo.st_mode)) {
    debug("File existed, but wasn't a file");
    free(userfile);
    userfile = 0;
  }

  /* Open it for append */
  if (userfile) {
    user_log = fopen(userfile, "a+");
    if (!user_log)
      syscall_fail("fopen", userfile, 0);
    free(userfile);
  } else {
    user_log = 0;
  }

  return user_log;
}

/* Read a line from the log */
static char *_irclog_read(FILE *file) {
  char buff[512], *line;

  line = 0;
  while (1) {
    if (!fgets(buff, 512, file)) {
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
        while ((ptr >= line) && (!ptr || strchr(" \t\r\n", *ptr))) *(ptr--) = 0;
        break;
      }
    }
  }

  return line;
}

/* Write a line to the end of a file */
static void _irclog_printf(FILE *fd, const char *format, ...) {
  va_list ap;
  char *msg;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  fseek(fd, 0, SEEK_END);
  fputs(msg, fd);
  fflush(fd);

  free(msg);
}

/* Write a line to the log */
static int _irclog_write(struct logfile *log, const char *format, ...) {
  va_list ap;
  char *msg;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  if (log->open && log->maxlines && (log->nlines >= log->maxlines)) {
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

    /* Make sure it's got the right permissions */
    if (chmod(log->filename, 0600))
      syscall_fail("chmod", log->filename, 0);

    /* Eat from the start */
    while ((log->nlines >= log->maxlines) && (l = _irclog_read(log->file))) {
      free(l);
      log->nlines--;
    }

    /* Write the rest */
    while ((l = _irclog_read(log->file))) {
      fprintf(out, "%s\n", l);
      free(l);
    }

    /* Close the input file, thereby *whoosh*ing it */
    fclose(log->file);
    log->file = out;
  }

  /* Write to the log file */
  if (log->open) {
    _irclog_printf(log->file, "%s\n", msg);
    log->nlines++;
  }

  free(msg);
  return 0;
}

/* Write a line through a pipe to a given program */
static int _irclog_pipe(struct ircproxy *p, int event,
                        const char *to, const char *from, const char *text) {
  int pi[2], pid;

  if (!p->conn_class->log_program)
    return 1;

  /* Prepare a pipe */
  if (pipe(pi)) {
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
    close(pi[0]);
    fd = fdopen(pi[1], "w");
    if (!fd) {
      syscall_fail("fdopen", 0, 0);
      close(pi[1]);
      return 1;
    }
    fprintf(fd, "%s\n", text);
    fflush(fd);
    fclose(fd);

  } else {
    /* Child, copy pipe to STDIN then exec the process */
    close(pi[1]);
    if (dup2(pi[0], STDIN_FILENO) != STDIN_FILENO) {
      syscall_fail("dup2", 0, 0);
      close(pi[0]);
      return 1;
    }

    /* Run with two arguments */
    execlp(p->conn_class->log_program, p->conn_class->log_program,
           irclog_flagtostr(event), to, from, 0);

    /* We can't get here.  Well we can, it means something went wrong */
    syscall_fail("execlp", p->conn_class->log_program, 0);
    exit(10);
  }

  return 0;
}

/* Write a message to log file(s) */
int irclog_log(struct ircproxy *p, int event, const char *to, const char *from,
               const char *format, ...) {
  char *text;
  va_list ap;

  if (!(p->conn_class->log_events & event))
    return 0;

  va_start(ap, format);
  text = x_vsprintf(format, ap);

  if (to != IRC_LOGFILE_ALL) {
    struct logfile *log;
    
    /* Write to one file */
    log = _irclog_getlog(p, to);
    if (!log)
      return -1;

    _irclog_writetext(p, log, event, to, from, text);
  } else {
    struct ircchannel *c;

    /* Write to all files except the private one */
    _irclog_writetext(p, &(p->server_log), event, IRC_LOGFILE_SERVER,
                      from, text);
    c = p->channels;
    while (c) {
      _irclog_writetext(p, &(c->log), event, c->name, from, text);
      c = c->next;
    }
  }

  free(text);
  va_end(ap);

  return 0;
}
  
/* Write some text to a log file */
static int _irclog_writetext(struct ircproxy *p, struct logfile *log,
                             int event, const char *to, const char *from,
                             const char *text) {
  const char *dest;
  FILE *user_log;
  time_t now;

  if (to == IRC_LOGFILE_ALL) {
    return -1;
  } else if (to == IRC_LOGFILE_SERVER) {
    dest = "SERVER";
  } else {
    dest = to;
  }
 
  time(&now);
  if (p->conn_class->log_timeoffset)
    now -= (p->conn_class->log_timeoffset * 60);
  
  _irclog_write(log, "%lu %s %s %s %s",
                now, irclog_flagtostr(event), dest, from, text);

  /* Write to the user's copy FIXME do this properly */
  user_log = _irclog_openuser(p, to);
  if (user_log) {
    char tbuf[40];
    
    if (p->conn_class->log_timestamp) {
      strftime(tbuf, sizeof(tbuf), LOG_USER_TIME_FORMAT, localtime(&now));
    } else {
      tbuf[0] = '0';
    }

    /* Print a nicely formatted entry to the log file */
    if (event & IRC_LOG_MSG) {
      _irclog_printf(user_log, "%s<%s> %s\n", tbuf, from, text);
    } else if (event & IRC_LOG_NOTICE) {
      _irclog_printf(user_log, "%s-%s- %s\n", tbuf, from, text);
    } else if (event & IRC_LOG_ACTION) {
      char *nick, *ptr;

      nick = x_strdup(from);
      ptr = strchr(nick, '!');
      if (ptr)
        *ptr = 0;

      _irclog_printf(user_log, "%s* %s %s\n", tbuf, nick, text);
      free(nick);
    } else if (event & IRC_LOG_CTCP) {
      _irclog_printf(user_log, "%s[%s] %s\n", tbuf, from, text);
    } else if (event & IRC_LOG_JOIN) {
      _irclog_printf(user_log, "%s--> %s\n", tbuf, text);
    } else if (event & IRC_LOG_PART) {
      _irclog_printf(user_log, "%s<-- %s\n", tbuf, text);
    } else if (event & IRC_LOG_KICK) {
      _irclog_printf(user_log, "%s<-- %s\n", tbuf, text);
    } else if (event & IRC_LOG_QUIT) {
      _irclog_printf(user_log, "%s<-- %s\n", tbuf, text);
    } else if (event & IRC_LOG_NICK) {
      _irclog_printf(user_log, "%s--- %s\n", tbuf, text);
    } else if (event & IRC_LOG_MODE) {
      _irclog_printf(user_log, "%s--- %s\n", tbuf, text);
    } else if (event & IRC_LOG_TOPIC) {
      _irclog_printf(user_log, "%s--- %s\n", tbuf, text);
    } else if (event & IRC_LOG_CLIENT) {
      _irclog_printf(user_log, "%s*** %s\n", tbuf, text);
    } else if (event & IRC_LOG_SERVER) {
      _irclog_printf(user_log, "%s*** %s\n", tbuf, text);
    } else if (event & IRC_LOG_ERROR) {
      _irclog_printf(user_log, "%s*** %s\n", tbuf, text);
    }
      
    fclose(user_log);
  }

  /* Write to the pipe */
  _irclog_pipe(p, event, dest, from, text);

  return 0;
}

/* Called to automatically recall stuff */
int irclog_autorecall(struct ircproxy *p, const char *to) {
  unsigned long recall, start, lines;
  struct logfile *log;

  log = _irclog_getlog(p, to);
  if (!log)
    return -1;

  if (log == &(p->server_log)) {
    recall = p->conn_class->server_log_recall;
  } else if (log == &(p->private_log)) {
    recall = p->conn_class->private_log_recall;
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
                  long start, long lines, const char *from) {
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
  } else if (log->filename && log->made) {
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

  /* If to is 0, then we're recalling from the server_log, and need to send
   * it to the nickname */
  if (!to)
    to = p->nickname ? p->nickname : "";

  /* Jump to the beginning */
  fseek(file, 0, SEEK_SET);

  if (start < log->nlines) {
    char *l;

    /* Skip start lines */
    while (start && (l = _irclog_read(file))) {
      free(l);
      start--;
    }

    /* Make lines sensible */
    lines = MIN(lines, log->nlines - start);

    /* Recall lines */
    while (lines && (l = _irclog_read(file))) {
      time_t when = 0;
      char *ll;

      /* Timestamped line for relative log creation */
      ll = l;
      if (*l == '@') {
        char *ts, *rest;

        /* Timestamp one character in */
        ts = l + 1;
        if (!*ts) {
          free(ll);
          continue;
        }

        /* Message continues after a space */
        rest = strchr(l, ' ');
        if (!rest) {
          free(ll);
          continue;
        }

        /* Delete the space */
        *(rest++) = 0;
        l = rest;

        /* Obtain the timestamp */
        when = strtoul(ts, (char **)NULL, 10);
      }

      /* Message or Notice lines, these require a bit of parsing */
      if ((*l == '<') || (*l == '-') || (*l == '[')) {
        char *src, *msg, lastchar;

        /* Source starts one character in */
        src = l + 1;
        if (!*src) {
          free(ll);
          continue;
        }

        /* Message starts after a space and the correct closing character */
        msg = strchr(l, ' ');
        if (*l == '<') {
          lastchar = '>';
        } else if (*l == '[') {
          lastchar = ']';
        } else {
          lastchar = '-';
        }
        if (!msg || (*(msg - 1) != lastchar)) {
          free(ll);
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
            free(ll);
            continue;
          }
          free(comp);
        }

        /* If there was a timestamp on it, we either fake the old-style
           stuff or do the new fancy stuff */
        if (when && p->conn_class->log_timestamp) {
          char tbuf[40];

          if (p->conn_class->log_relativetime) {
            time_t now, diff;

            time(&now);
            diff = now - when;

            if (diff < 82800L) {
              /* Within 23 hours [hh:mm] */
              strftime(tbuf, sizeof(tbuf), "%H:%M", localtime(&when));
            } else if (diff < 518400L) {
              /* Within 6 days [day hh:mm] */
              strftime(tbuf, sizeof(tbuf), "%a %H:%M", localtime(&when));
            } else if (diff < 25920000L) {
              /* Within 300 days [d mon] */
              strftime(tbuf, sizeof(tbuf), "%d %b", localtime(&when));
            } else {
              /* Otherwise [d mon yyyy] */
              strftime(tbuf, sizeof(tbuf), "%d %b %Y", localtime(&when));
            }
          } else {
            strftime(tbuf, sizeof(tbuf), LOG_TIME_FORMAT, localtime(&when));
          }

          /* Send the line */
          if (*l == '[') {
            char *cmd;

            /* Its a CTCP, so we have to place the command before the
               timestamp */
            cmd = msg;
            msg += strcspn(msg, " ");
            if (*msg)
              *(msg++) = 0;

            net_send(p->client_sock, ":%s PRIVMSG %s :\001%s [%s]%s%s\001\r\n",
                     src, to, cmd, tbuf, (strlen(msg) ? " " : ""), msg);
          } else {
            net_send(p->client_sock, ":%s %s %s :[%s] %s\r\n", src,
                     (*l == '<' ? "PRIVMSG" : "NOTICE"), to, tbuf, msg);
          }
        } else {
          /* Send the line */
          if (*l == '[') {
            net_send(p->client_sock, ":%s PRIVMSG %s :\001%s\001\r\n", src,
                     to, msg);
          } else {
            net_send(p->client_sock, ":%s %s %s :%s\r\n", src,
                     (*l == '<' ? "PRIVMSG" : "NOTICE"), to, msg);
          }
        }

      } else if (strncmp(l, "* ", 2)) {
        /* Anything thats not a comment gets sent as a notice */
        ircclient_send_notice(p, "%s", l);

      }

      free(ll);
      lines--;
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

/* Convert a textual flag name into the flag's value */
int irclog_strtoflag(const char *str) {
  if (!strcasecmp(str, "message")) {
    return IRC_LOG_MSG;
  } else if (!strcasecmp(str, "notice")) {
    return IRC_LOG_NOTICE;
  } else if (!strcasecmp(str, "action")) {
    return IRC_LOG_ACTION;
  } else if (!strcasecmp(str, "ctcp")) {
    return IRC_LOG_CTCP;
  } else if (!strcasecmp(str, "join")) {
    return IRC_LOG_JOIN;
  } else if (!strcasecmp(str, "part")) {
    return IRC_LOG_PART;
  } else if (!strcasecmp(str, "kick")) {
    return IRC_LOG_KICK;
  } else if (!strcasecmp(str, "quit")) {
    return IRC_LOG_QUIT;
  } else if (!strcasecmp(str, "nick")) {
    return IRC_LOG_NICK;
  } else if (!strcasecmp(str, "mode")) {
    return IRC_LOG_MODE;
  } else if (!strcasecmp(str, "topic")) {
    return IRC_LOG_TOPIC;
  } else if (!strcasecmp(str, "client")) {
    return IRC_LOG_CLIENT;
  } else if (!strcasecmp(str, "server")) {
    return IRC_LOG_SERVER;
  } else if (!strcasecmp(str, "error")) {
    return IRC_LOG_ERROR;
  } else {
    return 0;
  }
}

/* Convert a flag into a textual flag name */
char *irclog_flagtostr(int flag) {
  if (flag & IRC_LOG_MSG) {
    return "message";
  } else if (flag & IRC_LOG_NOTICE) {
    return "notice";
  } else if (flag & IRC_LOG_ACTION) {
    return "action";
  } else if (flag & IRC_LOG_CTCP) {
    return "ctcp";
  } else if (flag & IRC_LOG_JOIN) {
    return "join";
  } else if (flag & IRC_LOG_PART) {
    return "part";
  } else if (flag & IRC_LOG_KICK) {
    return "kick";
  } else if (flag & IRC_LOG_QUIT) {
    return "quit";
  } else if (flag & IRC_LOG_NICK) {
    return "nick";
  } else if (flag & IRC_LOG_MODE) {
    return "mode";
  } else if (flag & IRC_LOG_TOPIC) {
    return "topic";
  } else if (flag & IRC_LOG_CLIENT) {
    return "client";
  } else if (flag & IRC_LOG_SERVER) {
    return "server";
  } else if (flag & IRC_LOG_ERROR) {
    return "error";
  } else {
    return "(unknown)";
  }
}
