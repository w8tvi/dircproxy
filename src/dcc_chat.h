/* dircproxy
 * Copyright (C) 2000 Scott James Remnant <scott@netsplit.com>.
 * All Rights Reserved.
 *
 * dcc_chat.h
 * --
 * @(#) $Id: dcc_chat.h,v 1.1 2000/11/01 15:03:30 keybuk Exp $
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
extern void dccchat_data(struct dccproxy *, int);
extern void dccchat_error(struct dccproxy *, int, int);

#endif /* __DIRCPROXY_DCC_CHAT_H */
