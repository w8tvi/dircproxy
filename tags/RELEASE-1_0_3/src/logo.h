/* dircproxy
 * Copyright (C) 2002 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * logo.h
 * --
 * @(#) $Id: logo.h,v 1.3 2001/12/21 20:15:55 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_LOGO_H
#define __DIRCPROXY_LOGO_H

/* static array to hold the logo */
static char *logo[] = {
  "     _ _                w e l c o m e   t o ",
  "  __| (_)_ __ ___ _ __  _ __ _____  ___   _ ",
  " / _` | | '__/ __| '_ \\| '__/ _ \\ \\/ / | | |",
  "| (_| | | | | (__| |_) | | | (_) >  <| |_| |",
  " \\__,_|_|_|  \\___| .__/|_|  \\___/_/\\_\\\\__, |",
  "                 |_|                  |___/ ",
  0
};

/* static string to use as format for VERSION */
static char *verstr = "                                      %s";

#endif /* __DIRCPROXY_LOGO_H */
