/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * help.h
 * --
 * @(#) $Id: help.h,v 1.2 2000/09/29 12:43:36 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_HELP_H
#define __DIRCPROXY_HELP_H

/* help index */
static char *help_index[] = {
  "dircproxy can be controlled using the /DIRCPROXY command.",
  "This command takes one or more parameters, the first of",
  "which is a command, the rest of which depend on the",
  "command chosen.",
  "",
  "Valid commands:",
  0
};
static char *help_index_end[] = {
  "",
  "For more information on a command, use /DIRCPROXY HELP",
  "followed by the command",
  0
};

/* help help */
static char *help_help[] = {
  "/DIRCPROXY HELP",
  "displays general help on dircproxy and a list of valid",
  "commands.",
  "",
  "/DIRCPROXY HELP [command]",
  "displays specific help on the requested command.",
  0
};

/* help quit */
static char *help_quit[] = {
  "/DIRCPROXY QUIT [quit message]",
  "detaches you from dircproxy, and also ends the proxied",
  "session to the server.  This is the only way to do this.",
  "An optional /QUIT message may be supplied, you may need",
  "to prefix this with a : if it contains spaces.",
  0
};

/* help detach */
static char *help_detach[] = {
  "/DIRCPROXY DETACH [away message]",
  "detaches you from dircproxy, and sets an optional away",
  "message.  This usually has the same effect as simply",
  "closing your client, or using your clients /QUIT command.",
  "The away message may need to be prefixed with a : if it",
  "contains spaces",
  0
};

/* help recall */
static char *help_recall[] = {
  "/DIRCPROXY RECALL [from] <lines>",
  "/DIRCPROXY RECALL ALL",
  "recalls log messages from the server/private message log",
  "file.  You can specify the number of lines to recall, and",
  "a starting point in the file, or you can specify ALL to",
  "recall all log messages",
  "",
  "/DIRCPROXY RECALL <nickname> [from] lines>",
  "/DIRCPROXY RECALL <nickname> ALL",
  "recalls log messages from the server/private message log",
  "that were sent by the nickname given.  You can specify",
  "the number of lines to recall and an optional starting",
  "point (in the full log file).  You may also specify ALL.",
  "",
  "/DIRCPROXY RECALL <channel> [from] <lines>",
  "/DIRCPROXY RECALL <channel> ALL",
  "recalls log messages from the channel log specified.  You",
  "can specify the number of lines to recall and an optional",
  "starting point in the file, or you can specify ALL to",
  "recall all log messages",
  0
};

/* help persist */
static char *help_persist[] = {
  "/DIRCPROXY PERSIST",
  "for use if running dircproxy under inetd.  Creates a new",
  "listening socket, and links your current proxied session",
  "to it so you can reconnect later",
  0
};

#endif /* __DIRCPROXY_HELP_H */
