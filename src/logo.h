/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * logo.h
 * --
 * @(#) $Id: logo.h,v 1.4 2002/12/29 21:30:12 scott Exp $
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
  " http://dircproxy.googlecode.com/ ",
  0
};

/* static string to use as format for VERSION */
#ifdef SSL
static char *verstr = "                                  %s SSL";
#else /* SSL */
static char *verstr = "                                      %s";
#endif /* SSL */

#endif /* __DIRCPROXY_LOGO_H */
