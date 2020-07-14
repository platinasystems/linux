/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 * Copyright(c) 2018 Platina Systems, Inc.
 */

u64 xeth_onie_mac_base(struct device *dev)
{
	char mb[ETH_ALEN];
	ssize_t n;

	n = onie_get_tlv(dev, onie_type_mac_base, ETH_ALEN, mb);
	if (n < 0)
		return n;
	if (n != ETH_ALEN)
		eth_random_addr(mb);
	return ether_addr_to_u64(mb);
}

u16 xeth_onie_num_macs(struct device *dev)
{
	u8 v[2];
	ssize_t n;

	n = onie_get_tlv(dev, onie_type_num_macs, sizeof(u16), v);
	return (n == sizeof(u16)) ? (v[0] << 8) | v[1] : 0;
}

u8 xeth_onie_device_version(struct device *dev)
{
	u8 dv[1];
	ssize_t n;

	n = onie_get_tlv(dev, onie_type_device_version, sizeof(u8), dv);
	return (n == sizeof(u8)) ? dv[0] : 0;
}
