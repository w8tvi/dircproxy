/* dircproxy
 * Copyright (C) 2001 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * cfgfile.h
 * --
 * @(#) $Id: cfgfile.h,v 1.4 2001/01/11 15:29:21 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_CFGFILE_H
#define __DIRCPROXY_CFGFILE_H

/* functions */
extern int cfg_read(const char *, char **, struct globalvars *);

#endif /* __DIRCPROXY_CFGFILE_H */
