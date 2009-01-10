/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 *
 * irc_log.c
 *  - Handling of log files
 *  - Handling of log programs
 *  - Recalling from log files
 * --
 * $Id: irc_log.c,v 1.49 2004/03/27 15:15:35 bear Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */
#include "dircproxy.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "sprintf.h"
#include "irc_net.h"

#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "net.h"
#include "irc_prot.h"
#include "irc_client.h"
#include "irc_string.h"

#include "irc_log.h"


/* Log time format for strftime(3) */
#define LOG_TIME_FORMAT "[%H:%M] "

/* User log time format */
#define LOG_USER_TIME_FORMAT "[%d %b %H:%M] "

/* Log time/date format for strftime(3) */
#define LOG_TIMEDATE_FORMAT "%a, %d %b %Y %H:%M:%S %z"

/* Define MIN() */
#ifndef MIN
# define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif /* !MIN */

/* Convenient code defines */
#define IS_SERVER_LOG(_p, _log)  ((_log) == &((_p)->server_log))
#define IS_PRIVATE_LOG(_p, _log) ((_log) == &((_p)->private_log))

// MacOSX dont add PACKAGE_NAME to config.h
#ifndef PACKAGE_NAME
# define PACKAGE_NAME "dircproxy"
#endif 
       

/* Key/Value pairs to hold a string event flag and associated #define value */
typedef struct _flag_info {
	char *name;
	int   value;
} FlagInfo;


/* Forward prototypes for internal functions */
static char *	_safe_name(char *);
static LogFile *_logfile_get(IRCProxy *, const char *);
static void	_logfile_close(LogFile *);
static FILE *	_open_user_log(IRCProxy *, const char *);
static char *	_log_read(FILE *);
static void	_log_printf(FILE *, const char *, ...);
static int	_logfile_write(LogFile *, const char *, ...);
static int	_log_pipe(IRCProxy *, int, const char *, const char *,
			  const char *);
static int	_logfile_writetext(IRCProxy *, LogFile *, int, const char *,
				   const char *, const char *);

static int _irclog_recall(struct ircproxy *, struct logfile *, unsigned long,
                          unsigned long, const char *, const char *);


/* The translation table between our #defines and string event types */
static FlagInfo flag_table[] = {
	{ "message",	IRC_LOG_MSG },
	{ "notice",	IRC_LOG_NOTICE },
	{ "action",	IRC_LOG_ACTION },
	{ "ctcp",	IRC_LOG_CTCP },
	{ "join",	IRC_LOG_JOIN },
	{ "part",	IRC_LOG_PART },
	{ "kick",	IRC_LOG_KICK },
	{ "quit",	IRC_LOG_QUIT },
	{ "nick",	IRC_LOG_NICK },
	{ "mode",	IRC_LOG_MODE },
	{ "topic",	IRC_LOG_TOPIC },
	{ "client",	IRC_LOG_CLIENT },
	{ "server",	IRC_LOG_SERVER },
	{ "error",	IRC_LOG_ERROR },
	{ NULL,		IRC_LOG_NONE }
};


/* irclog_maketempdir
 * Create the temporary directory in which we place internal log files.
 * The name of this is based on the username running dircproxy, the process
 * ID of this dircproxy and a static counter for each time we're called.
 * The temp directory can be changed with either the TMPDIR or TEMP environment
 * variables.
 */
int
irclog_maketempdir(IRCProxy *p)
{
	static unsigned int  counter = 0;
	struct passwd       *pw;
	struct stat	     statinfo;
	const char	    *tmpdir, *uname;

	/* Don't allow ourselves to be called twice on the same proxy */
	if (p->temp_logdir)
		return 0;

	/* Find a temporary directory */
	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = getenv("TEMP");
	if (tmpdir == NULL)
		tmpdir = "/tmp";
	debug("Temp Directory = '%s'", tmpdir);

	/* Get our username */
	pw = getpwuid(geteuid());
	if (pw != NULL) {
		uname = pw->pw_name;
	} else {
		char uid[16];

		snprintf(uid, sizeof uid, "%d", geteuid());
		uname = uid;
	}
	debug("Username = '%s'", uname);

	/* Combine them all to make the log directory name */
	debug("Process ID = '%d'", getpid());
        p->temp_logdir = x_sprintf("%s/%s-%s-%d-%d", tmpdir, PACKAGE_NAME,
				   uname, getpid(), counter++);
	debug("Log temp directory = '%s'", p->temp_logdir);

	/* Make sure this is a safe directory to use */
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

	return 0;
}

/* _safe_name
 * Channel names are allowed to contain . and / according to the IRC
 * protocol.  These are nasty as it means someone could theoretically create
 * a channel called #/../../etc/passwd and the program would try to unlink
 * "/tmp/#/../../etc/passwd" = "/etc/passwd".  * If running as root this
 * could be bad.  So to compensate we replace '/' with ':' as thats not
 * valid in channel names.
 * We do the same for $ and \ (for Microsoft OS) 
 */
static char *
_safe_name(char *name)
{
	char *ptr;

	ptr = name;
	while (*ptr) {
		switch (*ptr) {
		 case '$':
		 case '\\':
		 case '/':
		   *ptr = ':';
		   break;
		}

		ptr++;
	}

	return name;
}

/* _logfile_get
 * Get the appropriate LogFile from an IRCProxy
 */
static LogFile *
_logfile_get(IRCProxy *p, const char *to)
{
	IRCChannel *c;

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

/* irclog_init
 * Initialise a log file, this decides on, and allocates, a filename and is
 * usually called when creating the proxy/channel this log file is a member
 * of.  This does not open the file.
 */
int
irclog_init(IRCProxy *p, const char *to)
{
	char	*filename;
	LogFile	*log;

	if (!(log = _logfile_get(p, to)))
		return -1;

	if (!p->temp_logdir)
		return -1;

	/* Copy the config, this makes it fixed while a log file is open,
	 * which is probably the right thing to do.
	 */
	if (IS_SERVER_LOG(p, log)) {
		debug("Initialising server log file");
		filename = x_strdup("server");
		log->maxlines = p->conn_class->server_log_maxsize;
		log->always = p->conn_class->server_log_always;

	} else if (IS_PRIVATE_LOG(p, log)) {
		debug("Initialising private log file");
		filename = x_strdup("private");
		log->maxlines = p->conn_class->private_log_maxsize;
		log->always = p->conn_class->private_log_always;

	} else {
		debug("Initialising channel log file for %s", to);
		filename = x_strdup(to);
		irc_strlwr(_safe_name(filename));
		log->maxlines = p->conn_class->chan_log_maxsize;
		log->always = p->conn_class->chan_log_always;
	}

	/* Store the filename in the LogFile */
	if (log->filename)
		free(log->filename);
	log->filename = x_sprintf("%s/%s", p->temp_logdir, filename);
	debug("Log filename = '%s'", log->filename);
	log->made = 0;

	free(filename);
	return 0;
}

/* irclog_open
 * Open a previously initialised log file
 */
int
irclog_open(IRCProxy *p, const char *to)
{
	LogFile *log;

	log = _logfile_get(p, to);
	if (!log || !log->filename)
		return -1;
	if (log->open)
		return 0;

	/* Unlink first for security */
	if (unlink(log->filename) && (errno != ENOENT)) {
		syscall_fail("unlink", log->filename, 0);
		free(log->filename);
		log->filename = 0;
		return -1;
	}

	/* Open for reading and writing */
	log->file = fopen(log->filename, "w+");
	if (log->file == NULL) {
		syscall_fail("fopen", log->filename, 0);
		free(log->filename);
		log->filename = 0;
		return -1;
	}

	/* Try to remove world and group read/write */
	if (fchmod(fileno(log->file), 0600))
		syscall_fail("fchmod", log->filename, 0);
  
	log->open = log->made = 1;
	log->nlines = 0;
	return 0;
}

/* _logfile_close
 * Close the filehandle associated with a LogFile structure
 */
static void
_logfile_close(LogFile *log)
{
	if (!log->open)
		return;

	debug("Closing log file '%s'", log->filename);
	fclose(log->file);
	log->open = 0;
}

/* irclog_close
 * Close a log file, don't log anything more to it for now.  This doesn't
 * unlink the file or free the information, it just indicates that logging
 * has concluded.  The file and information remains so we can reopen it to
 * recall things.
 */
void
irclog_close(IRCProxy *p, const char *to)
{
	LogFile *log;

	if (!(log = _logfile_get(p, to)))
		return;
	_logfile_close(log);
}

/* irclog_free
 * Close a log file and free up all of it's information.  Once this is called
 * the log file will need to be reinitialised before it can be used, and
 * nothing will be able to be recalled.
 */
void
irclog_free(LogFile *log)
{
	if (!log->filename)
		return;

	/* Close it if necessary */
	if (log->open)
		_logfile_close(log);

	/* Unlink the file, and free up the space used by the filename */
	debug("Freeing up log file '%s'", log->filename);
	unlink(log->filename);
	free(log->filename);
	log->nlines = 0;
	log->made = 0;
}

/* irclog_closetempdir
 * Remove the temporary directory and free up the space in the IRCProxy
 * structure.  This should only be called once all log files have been closed.
 */
void
irclog_closetempdir(IRCProxy *p)
{
	if (!p->temp_logdir)
		return;

	debug("Freeing log temp directory '%s'", p->temp_logdir);
	rmdir(p->temp_logdir);
	free(p->temp_logdir);
	p->temp_logdir = 0;
}

/* _open_use_log
 * Open a file to which we append log messages in a human-readable format.
 * This file should be closed once you've finished with it, it'll be opened
 * again next time (to allow the user to wipe it while we're running).
 */
static FILE *
_open_user_log(IRCProxy *p, const char *to)
{
	struct stat  statinfo;
	char	    *filename, *userfile;
	FILE	    *log;

	if (!p->conn_class->log_dir)
		return NULL;

	/* Work out the filename, because we don't have a LogFile structure
	 * we simply accept whatever we're given.
	 */
	if (to == IRC_LOGFILE_SERVER) {
		filename = x_strdup("Server");
	
	} else {
		filename = x_strdup(to);
		irc_strlwr(_safe_name((char*)to));
	}

	/* The filename is under the user's log_dir */
	userfile = x_sprintf("%s/%s.log", p->conn_class->log_dir, filename);
	debug("User log file = '%s'", userfile);
	free(filename);

	/* Make sure it's safe to use */
	if (lstat(userfile, &statinfo)) {
		if (errno != ENOENT) {
			syscall_fail("lstat", userfile, 0);
			free(userfile);
			return NULL;
		}
	} else if (!S_ISREG(statinfo.st_mode)) {
		debug("File existed, but wasn't a file");
		free(userfile);
		return NULL;
	}

	/* Open the file for appending */
	if (!(log = fopen(userfile, "a")))
		syscall_fail("fopen", userfile, 0);
	free(userfile);

	return log;
}

/* Read a line from the log FIXME */
static char *_log_read(FILE *file) {
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

/* _log_printf
 * Seek to the end of the file, write a line then flush the file so it
 * appears immediately.
 */
static void
_log_printf(FILE *fd, const char *format, ...)
{
	va_list  ap;
	char	*msg;

	/* Slurp the arguments in like printf */
	va_start(ap, format);
	msg = x_vsprintf(format, ap);
	va_end(ap);

	/* Write the line at the end of the file, then flush */
	fseek(fd, 0, SEEK_END);
	fputs(msg, fd);
	fflush(fd);

	free(msg);
}

/* Write a line to the log FIXME don't just roll by line counts now? */
static int _logfile_write(struct logfile *log, const char *format, ...) {
  va_list ap;
  char *msg;

  va_start(ap, format);
  msg = x_vsprintf(format, ap);
  va_end(ap);

  if (log->open && log->maxlines && (log->nlines >= log->maxlines)) {
    FILE *fout;
    char *l;

    /* We can't simply add .tmp or something on the end, because there is
       always a possibility that might be a channel name.  Besides using
       temporary files always looks icky to me.  This "Sick Puppy" way of
       reading from an unlinked file sits with me much better (says a lot
       about me, that) */
    fseek(log->file, 0, SEEK_SET);
    unlink(log->filename);

    /* This *really* shouldn't happen */
    fout = fopen(log->filename, "w+");
    if (!fout) {
      syscall_fail("fopen", log->filename, 0);
      return -1;
    }

    /* Make sure it's got the right permissions */
    if (fchmod( fileno(fout), 0600))
      syscall_fail("fchmod", log->filename, 0);

    /* Eat from the start */
    while ((log->nlines >= log->maxlines) && (l = _log_read(log->file))) {
      free(l);
      log->nlines--;
    }

    /* Write the rest */
    while ((l = _log_read(log->file))) {
      fprintf(fout, "%s\n", l);
      free(l);
    }

    /* Close the input file, thereby *whoosh*ing it */
    fclose(log->file);
    log->file = fout;
  }

  /* Write to the log file */
  if (log->open) {
    _log_printf(log->file, "%s\n", msg);
    log->nlines++;
  }

  free(msg);
  return 0;
}

/* _log_pipe
 * Call a program with the log type, source and destination information as
 * arguments, providing the message to log on its standard input.
 */
static int
_log_pipe(IRCProxy *p, int event, const char *to, const char *from,
	  const char *text)
{
	int   pfd[2], pid;
	FILE *fd;

	if (!p->conn_class->log_program)
		return 1;

	/* Prepare a pipe */
	if (pipe(pfd)) {
		syscall_fail("pipe", 0, 0);
		return 1;
	}

	/* Do the fork() thing */
	switch (pid = fork()) {
	case -1:
		/* Failed :( */
		syscall_fail("fork", 0, 0);
		return -1;

	case 0:
		/* Child process, close the write end of the pipe */
		close(pfd[1]);

		/* Copy read end to STDIN */
		if (dup2(pfd[0], STDIN_FILENO) != STDIN_FILENO) {
			syscall_fail("dup2", 0, 0);
			close(pfd[0]);
			return 1;
		}
		close(pfd[0]);
		
		/* Run the log program with the appropriate arguments.
		 * Use current environment and search the PATH if necessary.
		 */
		execlp(p->conn_class->log_program, p->conn_class->log_program,
		       irclog_flagtostr(event), to, from, NULL);

		/* Uh-oh!  Where's the kaboom?
		 * There was supposed to be an earth-shattering kaboom! 
		 */
		syscall_fail("execlp", p->conn_class->log_program, 0);
		exit(10);

	default:
		/* Parent process, close the read end of the pipe */
		close(pfd[0]);

		/* Open the write end as a FILE * */
		if (!(fd = fdopen(pfd[1], "w"))) {
			syscall_fail("fdopen", 0, 0);
			close(pfd[1]);
			return -1;
		}

		/* Write the log message to the new FILE * and close */
		fprintf(fd, "%s\n", text);
		fflush(fd);
		fclose(fd);
	}

	return 0;
}

/* Write some text to a log file */
static int _logfile_writetext(struct ircproxy *p, struct logfile *log, int event, const char *to, const char *from, const char *text) {
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
  
  _logfile_write(log, "%lu %s %s %s %s",
                now, irclog_flagtostr(event), dest, from, text);

  /* Write to the user's copy */
  user_log = _open_user_log(p, to);
  if (user_log) {
    char tbuf[40];
    
    if (p->conn_class->log_timestamp) {
      strftime(tbuf, sizeof(tbuf), LOG_USER_TIME_FORMAT, localtime(&now));
    } else {
      tbuf[0] = '0';
    }

    /* Print a nicely formatted entry to the log file */
    if (event & IRC_LOG_MSG) {
      _log_printf(user_log, "%s<%s> %s\n", tbuf, from, text);
    } else if (event & IRC_LOG_NOTICE) {
      _log_printf(user_log, "%s-%s- %s\n", tbuf, from, text);
    } else if (event & IRC_LOG_ACTION) {
      char *nick, *ptr;

      nick = x_strdup(from);
      ptr = strchr(nick, '!');
      if (ptr)
        *ptr = 0;

      _log_printf(user_log, "%s* %s %s\n", tbuf, nick, text);
      free(nick);
    } else if (event & IRC_LOG_CTCP) {
      _log_printf(user_log, "%s[%s] %s\n", tbuf, from, text);
    } else if (event & IRC_LOG_JOIN) {
      _log_printf(user_log, "%s--> %s\n", tbuf, text);
    } else if (event & IRC_LOG_PART) {
      _log_printf(user_log, "%s<-- %s\n", tbuf, text);
    } else if (event & IRC_LOG_KICK) {
      _log_printf(user_log, "%s<-- %s\n", tbuf, text);
    } else if (event & IRC_LOG_QUIT) {
      _log_printf(user_log, "%s<-- %s\n", tbuf, text);
    } else if (event & IRC_LOG_NICK) {
      _log_printf(user_log, "%s--- %s\n", tbuf, text);
    } else if (event & IRC_LOG_MODE) {
      _log_printf(user_log, "%s--- %s\n", tbuf, text);
    } else if (event & IRC_LOG_TOPIC) {
      _log_printf(user_log, "%s--- %s\n", tbuf, text);
    } else if (event & IRC_LOG_CLIENT) {
      _log_printf(user_log, "%s*** %s\n", tbuf, text);
    } else if (event & IRC_LOG_SERVER) {
      _log_printf(user_log, "%s*** %s\n", tbuf, text);
    } else if (event & IRC_LOG_ERROR) {
      _log_printf(user_log, "%s*** %s\n", tbuf, text);
    }
      
    fclose(user_log);
  }

  /* Write to the pipe */
  _log_pipe(p, event, dest, from, text); 

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
    log = _logfile_get(p, to);
    if (!log)
      return -1;

    _logfile_writetext(p, log, event, to, from, text);
  } else {
    struct ircchannel *c;

    /* Write to all files except the private one */
    _logfile_writetext(p, &(p->server_log), event, IRC_LOGFILE_SERVER,
                      from, text);
    c = p->channels;
    while (c) {
	    _logfile_writetext(p, &(c->log), event, c->name, from, text);
      c = c->next;
    }
  }

  free(text);
  va_end(ap);

  return 0;
}


  
/* Called to automatically recall stuff FIXME */
int irclog_autorecall(struct ircproxy *p, const char *to) {
  unsigned long recall, start, lines;
  struct logfile *log;

  log = _logfile_get(p, to);
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

/* Called to manually recall stuff FIXME */
int irclog_recall(struct ircproxy *p, const char *to,
                  long start, long lines, const char *from) {
  struct logfile *log;

  log = _logfile_get(p, to);
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

/* Called to do the recall from a log file FIXME */
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

  debug("recalling log [%s]\r\n", log->filename);

  /* If to is 0, then we're recalling from the server_log, and need to send
   * it to the nickname */
  if (!to)
    to = p->nickname ? p->nickname : "";

  /* Jump to the beginning */
  fseek(file, 0, SEEK_SET);

  if (start < log->nlines) {
    char *msg;

    /* Skip start lines */
    while (start && (msg = _log_read(file))) {
      free(msg);
      start--;
    }

    /* Make lines sensible */
    lines = MIN(lines, log->nlines - start);

    /* Recall lines */
    while (lines && (msg = _log_read(file))) {
      time_t when = 0;
      char *ll;
      int event;
      char *src, *frm, *eventtext;
      char *work, *rest;
      time_t now, diff;
      char tbuf[40];

      tbuf[0] = 0;
     
      debug("log: [%s]\r\n", msg);

       /*
	* 1079304950 client #dircproxy dircproxy You connected
	* 1079304951 join #dircproxy niven.freenode.net You joined the channel
	* 1079305132 client #dircproxy dircproxy You disconnected
	* 1079305143 message #dircproxy bear!~bear@pa.comcast.net lah
	* 1079305150 action #dircproxy bear!~bear@pa.comcast.net moons the channel
	*/

      ll   = msg;
      work = msg;
      if (!*work) {                             /* Timestamp is first value now */
        free(ll);
        continue;
      }
  
      rest = strchr(msg, ' ');                  /* parse to the space */
      if (!rest) {
        free(ll);
        continue;
      }

      *(rest++) = 0;                            /* delete the space */
      msg = rest;
  
      when = strtoul(work, (char **)NULL, 10);  /* Obtain the timestamp */

      eventtext = msg;                          /* store the event text */
      if (!*eventtext) {
        free(ll);
        continue;
      }
      
      rest = strchr(msg, ' ');                  /* Message continues after a space */
      if (!rest) {
        free(ll);
        continue;
      }
      
      *(rest++) = 0;                            /* Delete the space */
      msg = rest;

      event = irclog_strtoflag(eventtext);      /* determine the event code */
      src   = msg;                              /* log source */
      
      rest = strchr(msg, ' ');                  /* Message continues after a space */
      if (!rest) {
        free(ll);
        continue;
      }
  
      *(rest++) = 0;                            /* Delete the space */
      msg = rest;
  
      frm = msg;                                /* where the entry is from */
      
      rest = strchr(msg, ' ');                  /* Message continues after a space */
      if (!rest) {
        free(ll);
        continue;
      }
      
      *(rest++) = 0;                             /* Delete the space */
      msg = rest;
  
      debug("timestamp: %d event: %d src: [%s] frm: [%s] log: [%s]\r\n", when, event, src, frm, msg);

      /* If the log_timestamp option is on, format the timestamp */
      if (when && p->conn_class->log_timestamp) {
        if (p->conn_class->log_relativetime) {
          time(&now);
          diff = now - when;

          if (diff < 82800L) {                /* Within 23 hours [hh:mm] */
            strftime(tbuf, sizeof(tbuf), "[%H:%M] ", localtime(&when));
          } else if (diff < 518400L) {        /* Within 6 days [day hh:mm] */
            strftime(tbuf, sizeof(tbuf), "[%a %H:%M] ", localtime(&when));
          } else if (diff < 25920000L) {      /* Within 300 days [d mon] */
            strftime(tbuf, sizeof(tbuf), "[%d %b] ", localtime(&when));
          } else {                            /* Otherwise [d mon yyyy] */
            strftime(tbuf, sizeof(tbuf), "[%d %b %Y] ", localtime(&when));
          }
        } else {
          strftime(tbuf, sizeof(tbuf), LOG_TIME_FORMAT, localtime(&when));
        }
      }

      /* Message or Notice lines, these require a bit of parsing */
      if ((event == IRC_LOG_NOTICE) || (event == IRC_LOG_MSG) || (event == IRC_LOG_ACTION)) {
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
      }
      
        /* Send the line */
      if (event == IRC_LOG_MSG) {
        net_send(p->client_sock, ":%s PRIVMSG %s :%s%s\r\n", frm, to, tbuf, msg);
      } else if (event == IRC_LOG_ACTION) {
        net_send(p->client_sock, ":%s PRIVMSG %s :\001ACTION %s%s\001\r\n", frm, to, tbuf, msg);
      } else if (event == IRC_LOG_CTCP) {
        net_send(p->client_sock, ":%s PRIVMSG %s :\001%s %s%s%s\001\r\n", src, to, eventtext, tbuf, (strlen(msg) ? " " : ""), msg);
      } else if (event == IRC_LOG_NOTICE) {
        ircclient_send_notice(p, "%s", msg);
      } else {
        net_send(p->client_sock, ":%s PRIVMSG %s :%s%s\r\n", src, to, tbuf, msg);
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

/* irclog_strtoflag
 * Convert a textual flag name into the equivalent #define value
 */
int
irclog_strtoflag(const char *str)
{
	FlagInfo *fi;

	for (fi = flag_table; fi->name != NULL; fi++) {
		if (!strcasecmp(str, fi->name))
			return fi->value;
	}

	return IRC_LOG_NONE;
}

/* irclog_flagtostr
 * Convert a flag #define value into the equivalent textual name.  Returns the
 * empty string if the flag does not exist.
 */
const char *
irclog_flagtostr(int flag)
{
	FlagInfo *fi;

	for (fi = flag_table; fi->name != NULL; fi++) {
		if (fi->value == flag)
			return fi->name;
	}

	return "";
}
