/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * cfgfile.h
 * --
 * @(#) $Id: cfgfile.h,v 1.7 2002/12/29 21:30:11 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_CFGFILE_H
#define __DIRCPROXY_CFGFILE_H

/* functions */
extern int cfg_read(const char *, char **, char **, struct globalvars *);

#endif /* __DIRCPROXY_CFGFILE_H */
