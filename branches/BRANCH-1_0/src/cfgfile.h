/* dircproxy
 * Copyright (C) 2002 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * cfgfile.h
 * --
 * @(#) $Id: cfgfile.h,v 1.6 2002/02/06 10:07:42 scott Exp $
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
