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

#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/module.h>
#include <net/rtnetlink.h>

#include "xeth.h"
#include "debug.h"
#include "sysfs.h"

/* from <drivers/net/ethernet/intel/ixgbe> */
#define IXGBE_MAX_JUMBO_FRAME_SIZE 9728

#define platina_mk1_n_ports	32
#define platina_mk1_n_subports	4
#define platina_mk1_n_iflinks	2
#define platina_mk1_n_nds	(platina_mk1_n_ports * platina_mk1_n_subports)
#define platina_mk1_n_ids	(2 + platina_mk1_n_nds)
#define platina_mk1_n_encap	VLAN_HLEN

static struct attribute platina_mk1_ethtool_stat_attrs[] = {
	new_xeth_sysfs_attr(mmu_multicast_tx_cos0_drop_bytes),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos0_drop_packets),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos1_drop_bytes),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos1_drop_packets),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos2_drop_bytes),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos2_drop_packets),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos3_drop_bytes),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos3_drop_packets),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos4_drop_bytes),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos4_drop_packets),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos5_drop_bytes),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos5_drop_packets),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos6_drop_bytes),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos6_drop_packets),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos7_drop_bytes),
	new_xeth_sysfs_attr(mmu_multicast_tx_cos7_drop_packets),
	new_xeth_sysfs_attr(mmu_multicast_tx_qm_drop_bytes),
	new_xeth_sysfs_attr(mmu_multicast_tx_qm_drop_packets),
	new_xeth_sysfs_attr(mmu_multicast_tx_sc_drop_bytes),
	new_xeth_sysfs_attr(mmu_multicast_tx_sc_drop_packets),
	new_xeth_sysfs_attr(mmu_rx_threshold_drop_bytes),
	new_xeth_sysfs_attr(mmu_rx_threshold_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_0_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_0_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_10_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_10_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_11_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_11_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_12_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_12_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_13_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_13_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_14_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_14_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_15_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_15_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_16_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_16_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_17_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_17_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_18_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_18_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_19_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_19_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_1_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_1_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_20_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_20_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_21_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_21_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_22_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_22_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_23_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_23_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_24_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_24_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_25_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_25_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_26_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_26_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_27_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_27_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_28_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_28_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_29_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_29_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_2_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_2_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_30_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_30_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_31_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_31_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_32_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_32_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_33_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_33_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_34_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_34_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_35_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_35_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_36_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_36_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_37_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_37_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_38_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_38_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_39_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_39_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_3_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_3_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_40_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_40_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_41_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_41_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_42_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_42_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_43_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_43_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_44_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_44_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_45_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_45_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_46_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_46_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_47_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_47_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_4_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_4_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_5_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_5_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_6_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_6_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_7_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_7_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_8_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_8_drop_packets),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_9_drop_bytes),
	new_xeth_sysfs_attr(mmu_tx_cpu_cos_9_drop_packets),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos0_drop_bytes),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos0_drop_packets),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos1_drop_bytes),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos1_drop_packets),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos2_drop_bytes),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos2_drop_packets),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos3_drop_bytes),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos3_drop_packets),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos4_drop_bytes),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos4_drop_packets),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos5_drop_bytes),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos5_drop_packets),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos6_drop_bytes),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos6_drop_packets),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos7_drop_bytes),
	new_xeth_sysfs_attr(mmu_unicast_tx_cos7_drop_packets),
	new_xeth_sysfs_attr(mmu_unicast_tx_qm_drop_bytes),
	new_xeth_sysfs_attr(mmu_unicast_tx_qm_drop_packets),
	new_xeth_sysfs_attr(mmu_unicast_tx_sc_drop_bytes),
	new_xeth_sysfs_attr(mmu_unicast_tx_sc_drop_packets),
	new_xeth_sysfs_attr(mmu_wred_queue_cos0_drop_packets),
	new_xeth_sysfs_attr(mmu_wred_queue_cos1_drop_packets),
	new_xeth_sysfs_attr(mmu_wred_queue_cos2_drop_packets),
	new_xeth_sysfs_attr(mmu_wred_queue_cos3_drop_packets),
	new_xeth_sysfs_attr(mmu_wred_queue_cos4_drop_packets),
	new_xeth_sysfs_attr(mmu_wred_queue_cos5_drop_packets),
	new_xeth_sysfs_attr(mmu_wred_queue_cos6_drop_packets),
	new_xeth_sysfs_attr(mmu_wred_queue_cos7_drop_packets),
	new_xeth_sysfs_attr(mmu_wred_queue_qm_drop_packets),
	new_xeth_sysfs_attr(mmu_wred_queue_sc_drop_packets),
	new_xeth_sysfs_attr(port_rx_1024_to_1518_byte_packets),
	new_xeth_sysfs_attr(port_rx_128_to_255_byte_packets),
	new_xeth_sysfs_attr(port_rx_1519_to_1522_byte_vlan_packets),
	new_xeth_sysfs_attr(port_rx_1519_to_2047_byte_packets),
	new_xeth_sysfs_attr(port_rx_1tag_vlan_packets),
	new_xeth_sysfs_attr(port_rx_2048_to_4096_byte_packets),
	new_xeth_sysfs_attr(port_rx_256_to_511_byte_packets),
	new_xeth_sysfs_attr(port_rx_2tag_vlan_packets),
	new_xeth_sysfs_attr(port_rx_4096_to_9216_byte_packets),
	new_xeth_sysfs_attr(port_rx_512_to_1023_byte_packets),
	new_xeth_sysfs_attr(port_rx_64_byte_packets),
	new_xeth_sysfs_attr(port_rx_65_to_127_byte_packets),
	new_xeth_sysfs_attr(port_rx_802_3_length_error_packets),
	new_xeth_sysfs_attr(port_rx_9217_to_16383_byte_packets),
	new_xeth_sysfs_attr(port_rx_alignment_error_packets),
	new_xeth_sysfs_attr(port_rx_broadcast_packets),
	new_xeth_sysfs_attr(port_rx_bytes),
	new_xeth_sysfs_attr(port_rx_code_error_packets),
	new_xeth_sysfs_attr(port_rx_control_packets),
	new_xeth_sysfs_attr(port_rx_crc_error_packets),
	new_xeth_sysfs_attr(port_rx_eee_lpi_duration),
	new_xeth_sysfs_attr(port_rx_eee_lpi_events),
	new_xeth_sysfs_attr(port_rx_false_carrier_events),
	new_xeth_sysfs_attr(port_rx_flow_control_packets),
	new_xeth_sysfs_attr(port_rx_fragment_packets),
	new_xeth_sysfs_attr(port_rx_good_packets),
	new_xeth_sysfs_attr(port_rx_jabber_packets),
	new_xeth_sysfs_attr(port_rx_mac_sec_crc_matched_packets),
	new_xeth_sysfs_attr(port_rx_mtu_check_error_packets),
	new_xeth_sysfs_attr(port_rx_multicast_packets),
	new_xeth_sysfs_attr(port_rx_oversize_packets),
	new_xeth_sysfs_attr(port_rx_packets),
	new_xeth_sysfs_attr(port_rx_pfc_packets),
	new_xeth_sysfs_attr(port_rx_pfc_priority_0),
	new_xeth_sysfs_attr(port_rx_pfc_priority_1),
	new_xeth_sysfs_attr(port_rx_pfc_priority_2),
	new_xeth_sysfs_attr(port_rx_pfc_priority_3),
	new_xeth_sysfs_attr(port_rx_pfc_priority_4),
	new_xeth_sysfs_attr(port_rx_pfc_priority_5),
	new_xeth_sysfs_attr(port_rx_pfc_priority_6),
	new_xeth_sysfs_attr(port_rx_pfc_priority_7),
	new_xeth_sysfs_attr(port_rx_promiscuous_packets),
	new_xeth_sysfs_attr(port_rx_runt_bytes),
	new_xeth_sysfs_attr(port_rx_runt_packets),
	new_xeth_sysfs_attr(port_rx_src_address_not_unicast_packets),
	new_xeth_sysfs_attr(port_rx_truncated_packets),
	new_xeth_sysfs_attr(port_rx_undersize_packets),
	new_xeth_sysfs_attr(port_rx_unicast_packets),
	new_xeth_sysfs_attr(port_rx_unsupported_dst_address_control_packets),
	new_xeth_sysfs_attr(port_rx_unsupported_opcode_control_packets),
	new_xeth_sysfs_attr(port_rx_xon_to_xoff_priority_0),
	new_xeth_sysfs_attr(port_rx_xon_to_xoff_priority_1),
	new_xeth_sysfs_attr(port_rx_xon_to_xoff_priority_2),
	new_xeth_sysfs_attr(port_rx_xon_to_xoff_priority_3),
	new_xeth_sysfs_attr(port_rx_xon_to_xoff_priority_4),
	new_xeth_sysfs_attr(port_rx_xon_to_xoff_priority_5),
	new_xeth_sysfs_attr(port_rx_xon_to_xoff_priority_6),
	new_xeth_sysfs_attr(port_rx_xon_to_xoff_priority_7),
	new_xeth_sysfs_attr(port_tx_1024_to_1518_byte_packets),
	new_xeth_sysfs_attr(port_tx_128_to_255_byte_packets),
	new_xeth_sysfs_attr(port_tx_1519_to_1522_byte_vlan_packets),
	new_xeth_sysfs_attr(port_tx_1519_to_2047_byte_packets),
	new_xeth_sysfs_attr(port_tx_1tag_vlan_packets),
	new_xeth_sysfs_attr(port_tx_2048_to_4096_byte_packets),
	new_xeth_sysfs_attr(port_tx_256_to_511_byte_packets),
	new_xeth_sysfs_attr(port_tx_2tag_vlan_packets),
	new_xeth_sysfs_attr(port_tx_4096_to_9216_byte_packets),
	new_xeth_sysfs_attr(port_tx_512_to_1023_byte_packets),
	new_xeth_sysfs_attr(port_tx_64_byte_packets),
	new_xeth_sysfs_attr(port_tx_65_to_127_byte_packets),
	new_xeth_sysfs_attr(port_tx_9217_to_16383_byte_packets),
	new_xeth_sysfs_attr(port_tx_broadcast_packets),
	new_xeth_sysfs_attr(port_tx_bytes),
	new_xeth_sysfs_attr(port_tx_control_packets),
	new_xeth_sysfs_attr(port_tx_eee_lpi_duration),
	new_xeth_sysfs_attr(port_tx_eee_lpi_events),
	new_xeth_sysfs_attr(port_tx_excessive_collision_packets),
	new_xeth_sysfs_attr(port_tx_fcs_errors),
	new_xeth_sysfs_attr(port_tx_fifo_underrun_packets),
	new_xeth_sysfs_attr(port_tx_flow_control_packets),
	new_xeth_sysfs_attr(port_tx_fragments),
	new_xeth_sysfs_attr(port_tx_good_packets),
	new_xeth_sysfs_attr(port_tx_jabber_packets),
	new_xeth_sysfs_attr(port_tx_late_collision_packets),
	new_xeth_sysfs_attr(port_tx_multicast_packets),
	new_xeth_sysfs_attr(port_tx_multiple_collision_packets),
	new_xeth_sysfs_attr(port_tx_multiple_deferral_packets),
	new_xeth_sysfs_attr(port_tx_oversize),
	new_xeth_sysfs_attr(port_tx_packets),
	new_xeth_sysfs_attr(port_tx_pfc_packets),
	new_xeth_sysfs_attr(port_tx_pfc_priority_0_packets),
	new_xeth_sysfs_attr(port_tx_pfc_priority_1_packets),
	new_xeth_sysfs_attr(port_tx_pfc_priority_2_packets),
	new_xeth_sysfs_attr(port_tx_pfc_priority_3_packets),
	new_xeth_sysfs_attr(port_tx_pfc_priority_4_packets),
	new_xeth_sysfs_attr(port_tx_pfc_priority_5_packets),
	new_xeth_sysfs_attr(port_tx_pfc_priority_6_packets),
	new_xeth_sysfs_attr(port_tx_pfc_priority_7_packets),
	new_xeth_sysfs_attr(port_tx_runt_packets),
	new_xeth_sysfs_attr(port_tx_single_collision_packets),
	new_xeth_sysfs_attr(port_tx_single_deferral_packets),
	new_xeth_sysfs_attr(port_tx_system_error_packets),
	new_xeth_sysfs_attr(port_tx_total_collisions),
	new_xeth_sysfs_attr(port_tx_unicast_packets),
	new_xeth_sysfs_attr(punts),
	new_xeth_sysfs_attr(rx_bytes),
	new_xeth_sysfs_attr(rx_packets),
	new_xeth_sysfs_attr(rx_pipe_debug_6),
	new_xeth_sysfs_attr(rx_pipe_debug_7),
	new_xeth_sysfs_attr(rx_pipe_debug_8),
	new_xeth_sysfs_attr(rx_pipe_dst_discard_drops),
	new_xeth_sysfs_attr(rx_pipe_ecn_counter),
	new_xeth_sysfs_attr(rx_pipe_hi_gig_broadcast_packets),
	new_xeth_sysfs_attr(rx_pipe_hi_gig_control_packets),
	new_xeth_sysfs_attr(rx_pipe_hi_gig_l2_multicast_packets),
	new_xeth_sysfs_attr(rx_pipe_hi_gig_l3_multicast_packets),
	new_xeth_sysfs_attr(rx_pipe_hi_gig_unknown_opcode_packets),
	new_xeth_sysfs_attr(rx_pipe_ibp_discard_cbp_full_drops),
	new_xeth_sysfs_attr(rx_pipe_ip4_header_errors),
	new_xeth_sysfs_attr(rx_pipe_ip4_l3_drops),
	new_xeth_sysfs_attr(rx_pipe_ip4_l3_packets),
	new_xeth_sysfs_attr(rx_pipe_ip4_routed_multicast_packets),
	new_xeth_sysfs_attr(rx_pipe_ip6_header_errors),
	new_xeth_sysfs_attr(rx_pipe_ip6_l3_drops),
	new_xeth_sysfs_attr(rx_pipe_ip6_l3_packets),
	new_xeth_sysfs_attr(rx_pipe_ip6_routed_multicast_packets),
	new_xeth_sysfs_attr(rx_pipe_l3_interface_bytes),
	new_xeth_sysfs_attr(rx_pipe_l3_interface_packets),
	new_xeth_sysfs_attr(rx_pipe_multicast_drops),
	new_xeth_sysfs_attr(rx_pipe_niv_forwarding_error_drops),
	new_xeth_sysfs_attr(rx_pipe_niv_frame_error_drops),
	new_xeth_sysfs_attr(rx_pipe_port_table_bytes),
	new_xeth_sysfs_attr(rx_pipe_port_table_packets),
	new_xeth_sysfs_attr(rx_pipe_rxf_drops),
	new_xeth_sysfs_attr(rx_pipe_spanning_tree_state_not_forwarding_drops),
	new_xeth_sysfs_attr(rx_pipe_trill_non_trill_drops),
	new_xeth_sysfs_attr(rx_pipe_trill_packets),
	new_xeth_sysfs_attr(rx_pipe_trill_trill_drops),
	new_xeth_sysfs_attr(rx_pipe_tunnel_error_packets),
	new_xeth_sysfs_attr(rx_pipe_tunnel_packets),
	new_xeth_sysfs_attr(rx_pipe_unicast_packets),
	new_xeth_sysfs_attr(rx_pipe_unknown_vlan_drops),
	new_xeth_sysfs_attr(rx_pipe_vlan_tagged_packets),
	new_xeth_sysfs_attr(rx_pipe_zero_port_bitmap_drops),
	new_xeth_sysfs_attr(tx_bytes),
	new_xeth_sysfs_attr(tx_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x10_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x10_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x11_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x11_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x12_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x12_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x13_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x13_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x14_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x14_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x15_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x15_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x16_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x16_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x17_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x17_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x18_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x18_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x19_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x19_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x1a_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x1a_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x1b_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x1b_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x1c_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x1c_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x1d_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x1d_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x1e_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x1e_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x1f_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x1f_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x20_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x20_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x21_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x21_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x22_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x22_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x23_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x23_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x24_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x24_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x25_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x25_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x26_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x26_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x27_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x27_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x28_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x28_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x29_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x29_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x2a_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x2a_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x2b_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x2b_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x2c_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x2c_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x2d_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x2d_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x2e_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x2e_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x2f_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x2f_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x4_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x4_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x5_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x5_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x6_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x6_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x7_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x7_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x8_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x8_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x9_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0x9_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0xa_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0xa_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0xb_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0xb_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0xc_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0xc_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0xd_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0xd_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0xe_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0xe_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_0xf_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_0xf_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_error_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_error_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_punt_1tag_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_punt_1tag_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_punt_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_punt_packets),
	new_xeth_sysfs_attr(tx_pipe_cpu_vlan_redirect_bytes),
	new_xeth_sysfs_attr(tx_pipe_cpu_vlan_redirect_packets),
	new_xeth_sysfs_attr(tx_pipe_debug_a),
	new_xeth_sysfs_attr(tx_pipe_debug_b),
	new_xeth_sysfs_attr(tx_pipe_ecn_errors),
	new_xeth_sysfs_attr(tx_pipe_invalid_vlan_drops),
	new_xeth_sysfs_attr(tx_pipe_ip4_unicast_aged_and_dropped_packets),
	new_xeth_sysfs_attr(tx_pipe_ip4_unicast_packets),
	new_xeth_sysfs_attr(tx_pipe_ip_length_check_drops),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos0_bytes),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos0_packets),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos1_bytes),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos1_packets),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos2_bytes),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos2_packets),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos3_bytes),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos3_packets),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos4_bytes),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos4_packets),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos5_bytes),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos5_packets),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos6_bytes),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos6_packets),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos7_bytes),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_cos7_packets),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_qm_bytes),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_qm_packets),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_sc_bytes),
	new_xeth_sysfs_attr(tx_pipe_multicast_queue_sc_packets),
	new_xeth_sysfs_attr(tx_pipe_packet_aged_drops),
	new_xeth_sysfs_attr(tx_pipe_packets_dropped),
	new_xeth_sysfs_attr(tx_pipe_port_table_bytes),
	new_xeth_sysfs_attr(tx_pipe_port_table_packets),
	new_xeth_sysfs_attr(tx_pipe_purge_cell_error_drops),
	new_xeth_sysfs_attr(tx_pipe_spanning_tree_state_not_forwarding_drops),
	new_xeth_sysfs_attr(tx_pipe_trill_access_port_drops),
	new_xeth_sysfs_attr(tx_pipe_trill_non_trill_drops),
	new_xeth_sysfs_attr(tx_pipe_trill_packets),
	new_xeth_sysfs_attr(tx_pipe_tunnel_error_packets),
	new_xeth_sysfs_attr(tx_pipe_tunnel_packets),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos0_bytes),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos0_packets),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos1_bytes),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos1_packets),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos2_bytes),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos2_packets),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos3_bytes),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos3_packets),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos4_bytes),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos4_packets),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos5_bytes),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos5_packets),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos6_bytes),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos6_packets),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos7_bytes),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_cos7_packets),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_qm_bytes),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_qm_packets),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_sc_bytes),
	new_xeth_sysfs_attr(tx_pipe_unicast_queue_sc_packets),
	new_xeth_sysfs_attr(tx_pipe_vlan_tagged_packets),
};

static size_t platina_mk1_ethtool_stat_attr_index(struct attribute *attr)
{
	return (size_t)(attr-platina_mk1_ethtool_stat_attrs);
}

#define platina_mk1_n_ethtool_stats					\
	sizeof(platina_mk1_ethtool_stat_attrs)/sizeof(struct attribute)

static struct attribute
*platina_mk1_ethtool_stat_ktype_default_attrs[1+platina_mk1_n_ethtool_stats];

/* FIXME reset from board version at init */
static bool platina_mk1_one_based = false;

static u64 platina_mk1_eth0_ea64;
static unsigned char platina_mk1_eth0_ea_assign_type;

static inline int _platina_mk1_assert_iflinks(void)
{
	struct net_device *eth1 = dev_get_by_name(&init_net, "eth1");
	struct net_device *eth2 = dev_get_by_name(&init_net, "eth2");
	int err;

	if (!eth1) {
		err = xeth_debug_val("%d, %s", -ENOENT, "eth1");
		goto egress;
	}
	if (!eth2) {
		err = xeth_debug_val("%d, %s", -ENOENT, "eth2");
		goto egress;
	}
	err = xeth_debug_val("%d, %s",
			     netdev_rx_handler_register(eth1,
							xeth.ops.rx_handler,
							&xeth),
			     "eth1");
	if (err)
		goto egress;
	err = xeth_debug_val("%d, %s",
			     netdev_rx_handler_register(eth2,
							xeth.ops.rx_handler,
							&xeth),
			     "eth2");
	if (err) {
		netdev_rx_handler_unregister(eth1);
		goto egress;
	}
	if (true) {	/* FIXME sort by bus address */
		xeth_set_iflinks(0, eth1);
		xeth_set_iflinks(1, eth2);
	} else {
		xeth_set_iflinks(1, eth1);
		xeth_set_iflinks(0, eth2);
	}
	return 0;
egress:
	if (eth2)
		dev_put(eth2);
	if (eth2)
		dev_put(eth2);
	return err;
}

static int platina_mk1_assert_iflinks(void)
{
	static struct mutex platina_mk1_iflinks_mutex;
	int err = 0;

	if (xeth_iflinks(0))
		return err;
	mutex_lock(&platina_mk1_iflinks_mutex);
	if (!xeth_iflinks(0))
		err = _platina_mk1_assert_iflinks();
	mutex_unlock(&platina_mk1_iflinks_mutex);
	return err;
}

static int platina_mk1_parse_name(struct xeth_priv *priv, const char *name)
{
	int base = platina_mk1_one_based ? 1 : 0;
	u16 port, subport;

	if (sscanf(name, "eth-%hu-%hu", &port, &subport) != 2)
		return -EINVAL;
	if ((port > (platina_mk1_n_ports + base)) || (port < base))
		return -EINVAL;
	port -= base;
	if ((subport > (platina_mk1_n_subports + base)) || (subport < base))
		return -EINVAL;
	subport -= base;
	priv->id = 1 + ((port ^ 1) * platina_mk1_n_subports) + subport + 1;
	priv->ndi = (port * platina_mk1_n_subports) + subport;
	priv->iflinki = port >= (platina_mk1_n_ports / 2) ? 1 : 0;
	return 0;
}

static int platina_mk1_set_lladdr(struct net_device *nd)
{
	struct xeth_priv *priv = netdev_priv(nd);

	if (!platina_mk1_eth0_ea64) {
		struct net_device *eth0 = dev_get_by_name(&init_net, "eth0");
		if (eth0 == NULL)
			return xeth_debug_netdev_val(nd, "%d, can't find eth0",
						     -ENOENT);
		platina_mk1_eth0_ea64 = ether_addr_to_u64(eth0->dev_addr);
		platina_mk1_eth0_ea_assign_type = eth0->addr_assign_type;
		dev_put(eth0);
	}
	u64_to_ether_addr(platina_mk1_eth0_ea64 + 3 + priv->ndi, nd->dev_addr);
	nd->addr_assign_type = platina_mk1_eth0_ea_assign_type;
	return 0;
}

static int __init platina_mk1_init(void)
{
	int i, err;
	
	xeth.n.ids = platina_mk1_n_ids;
	xeth.n.nds = platina_mk1_n_nds,
	xeth.n.iflinks = platina_mk1_n_iflinks;
	xeth.n.encap = platina_mk1_n_encap;
	err = xeth_init();
	if (err)
		return err;

	xeth.n.ethtool_stats = platina_mk1_n_ethtool_stats;
	for (i = 0; i < platina_mk1_n_ethtool_stats; i++)
		platina_mk1_ethtool_stat_ktype_default_attrs[i] =
			&platina_mk1_ethtool_stat_attrs[i];
	xeth.ethtool_stat_ktype.default_attrs =
		platina_mk1_ethtool_stat_ktype_default_attrs;

	xeth.ops.assert_iflinks = platina_mk1_assert_iflinks;
	xeth.ops.parse_name = platina_mk1_parse_name;
	xeth.ops.set_lladdr = platina_mk1_set_lladdr;
	xeth.ops.ethtool_stat_attr_index = platina_mk1_ethtool_stat_attr_index;
	err = xeth_link_init("platina-mk1");
	if (err) {
		xeth_exit();
		return err;
	}

	xeth_ndo_init();
	xeth_notifier_init();
	xeth_vlan_init();
	xeth_sysfs_init("platina-mk1");
	xeth_devfs_init("platina-mk1");

	return 0;
}

static void __exit platina_mk1_exit(void)
{
	xeth_notifier_exit();
	xeth_link_exit();
	xeth_ndo_exit();
	xeth_notifier_exit();
	xeth_vlan_exit();
	xeth_exit();
	xeth_sysfs_exit();
	xeth_devfs_exit();
}

module_init(platina_mk1_init);
module_exit(platina_mk1_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_DESCRIPTION("XETH for Platina Systems MK1 TOR Ethernet Switch");
