/* dircproxy
 * Copyright (C) 2001 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * dcc_send.h
 * --
 * @(#) $Id: dcc_send.h,v 1.2 2001/01/11 15:29:21 keybuk Exp $
 *
 * This file is distributed according to the GNU General Public
 * License.  For full details, read the top of 'main.c' or the
 * file called COPYING that was distributed with this code.
 */

#ifndef __DIRCPROXY_DCC_SEND_H
#define __DIRCPROXY_DCC_SEND_H

/* required includes */
#include "dcc_net.h"

/* functions */
extern void dccsend_connected(struct dccproxy *, int);
extern void dccsend_connectfailed(struct dccproxy *, int, int);
extern void dccsend_accepted(struct dccproxy *);

#endif /* __DIRCPROXY_DCC_SEND_H */
