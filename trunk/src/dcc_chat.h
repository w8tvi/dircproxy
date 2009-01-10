/* dircproxy
 * Copyright (C) 2000-2003 Scott James Remnant <scott at netsplit dot com>
 * 
 * Copyright (C) 2004-2008 Francois Harvey <contact at francoisharvey dot ca>
 * 
 * Copyright (C) 2008-2009 Noel Shrum <noel dot w8tvi at gmail dot com>
 *                         Francois Harvey <contact at francoisharvey dot ca>
 * 
 * 
 * dcc_chat.h
 * --
 * @(#) $Id: dcc_chat.h,v 1.6 2002/12/29 21:30:11 scott Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_DCC_CHAT_H
#define __DIRCPROXY_DCC_CHAT_H

/* required includes */
#include "dcc_net.h"

/* functions */
extern void dccchat_connected(struct dccproxy *, int);
extern void dccchat_connectfailed(struct dccproxy *, int, int);
extern void dccchat_accepted(struct dccproxy *);

#endif /* __DIRCPROXY_DCC_CHAT_H */
