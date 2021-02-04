/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_NB_H
#define __NET_ETHERNET_XETH_NB_H

#include <linux/netdevice.h>

struct xeth_nb {
	struct notifier_block fib;
	struct notifier_block inetaddr;
	struct notifier_block inet6addr;
	struct notifier_block netdevice;
	struct notifier_block netevent;
};

struct xeth_nb *xeth_mux_nb(struct net_device *mux);
struct net_device *xeth_mux_of_nb(struct xeth_nb *);

int xeth_nb_start_fib(struct net_device *mux);
int xeth_nb_start_inetaddr(struct net_device *mux);
int xeth_nb_start_inet6addr(struct net_device *mux);
int xeth_nb_start_netdevice(struct net_device *mux);
int xeth_nb_start_netevent(struct net_device *mux);

void xeth_nb_stop_fib(struct net_device *mux);
void xeth_nb_stop_inetaddr(struct net_device *mux);
void xeth_nb_stop_inet6addr(struct net_device *mux);
void xeth_nb_stop_netdevice(struct net_device *mux);
void xeth_nb_stop_netevent(struct net_device *mux);

#endif	/* __NET_ETHERNET_XETH_NB_H */
