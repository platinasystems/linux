/* Platina Systems XETH driver for the MK1 top of rack ethernet switch
 * Copyright(c) 2018 Platina Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <uapi/linux/xeth.h>

static int xeth_parse_xethbr(const char *name, struct xeth_priv_ref *ref)
{
	u16 u;
	const char *p = name + 7;

	if (xeth_pr_true_expr(!*p, "[%s] incomplete", name))
		return -EINVAL;
	if (xeth_pr_true_expr(sscanf(p, "%hu", &u) != 1,
			      "[%s] invalid BRIDGE", name))
		return -EINVAL;
	if (xeth_pr_true_expr(1 > u || u >= xeth.n.userids,
			      "[%s] out-of-range ID %u", name, u))
		return -EINVAL;
	ref->porti = -1;
	ref->subporti = -1;
	ref->portid = -1;
	ref->id = u;
	ref->ndi = ref->id;
	ref->iflinki = ref->id & 1;
	ref->devtype = XETH_DEVTYPE_BRIDGE;
	return 0;
}

static int xeth_parse_xeth(const char *name, struct xeth_priv_ref *ref)
{
	int n;
	u16 u;
	const char *p = name + 4;

	if (xeth_pr_true_expr(!*p, "[%s] incomplete", name))
		return -EINVAL;
	if (xeth_pr_true_expr(sscanf(p, "%hu%n", &u, &n) != 1,
			      "[%s] invalid PORT [%s]", name, p))
		return -EINVAL;
	p += n;
	u -= xeth.n.base;
	if (xeth_pr_true_expr(u >= xeth.n.ports,
			      "[%s] out-of-range PORT %u", name, u))
		return -EINVAL;
	ref->porti = u;
	ref->subporti = -1;
	ref->portid = 4094 - u;
	ref->id = ref->portid;
	ref->devtype = XETH_DEVTYPE_PORT;
	if (*p == '-') {
		p++;
		if (xeth_pr_true_expr(sscanf(p, "%hu%n", &u, &n) != 1,
				      "[%s] invalid SUBPORT [%s]", name, p))
			return -EINVAL;
		u -= xeth.n.base;
		if (xeth_pr_true_expr(u >= xeth.n.subports,
				      "[%s] out-of-range SUBPORT %u", name, u))
			return -EINVAL;
		p += n;
		ref->subporti = u;
		ref->portid -= (u * xeth.n.ports);
		ref->id = ref->portid;
	}
	if (*p == '.') {
		p++;
		if (xeth_pr_true_expr(sscanf(p, "%hu%n", &u, &n) != 1,
				      "[%s] invalid ID [%s]", name, p))
			return -EINVAL;
		if (xeth_pr_true_expr(1 > u || u >= xeth.n.userids,
				      "[%s] out-of-range ID %u", name, u))
			return -EINVAL;
		p += n;
		switch (*p) {
		case 't':
			ref->devtype = XETH_DEVTYPE_TAGGED_BRIDGE_PORT;
			break;
		case 'u':
			ref->devtype = XETH_DEVTYPE_UNTAGGED_BRIDGE_PORT;
			break;
		case '\0':
			xeth_pr("<%s> requires 't' or 'u' suffix", name);
			return -EINVAL;
		default:
			xeth_pr("<%s> invalid suffix <%s>", name, p);
			return -EINVAL;
		}
		p++;
		ref->id = u;
		ref->ndi = -1;
	} else {
		ref->ndi = ref->id;
	}
	if (xeth_pr_true_expr(*p, "<%s> invalid suffix <%s>", name, p))
		return -EINVAL;
	ref->iflinki = ref->id & 1;
	return 0;
}

int xeth_parse_name(const char *name, struct xeth_priv_ref *ref)
{
	if (memcmp(name, "xethbr.", 7) == 0)
		return xeth_parse_xethbr(name, ref);
	else if (memcmp(name, "xeth", 4) == 0)
		return xeth_parse_xeth(name, ref);
	xeth_pr("'%s' invalid ifname", name);
	return -EINVAL;
}
