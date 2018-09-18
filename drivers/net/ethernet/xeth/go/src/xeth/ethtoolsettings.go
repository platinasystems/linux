/* Copyright(c) 2018 Platina Systems, Inc.
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

package xeth

import "fmt"

func (msg *MsgEthtoolSettings) String() string {
	return fmt.Sprintln(Kind(msg.Kind),
		Interface.Indexed(msg.Ifindex).Name) +
		fmt.Sprintln("\tspeed:", Mbps(msg.Speed)) +
		fmt.Sprintln("\tduplex:", Duplex(msg.Duplex)) +
		fmt.Sprintln("\tport:", Port(msg.Port)) +
		fmt.Sprintln("\tautoneg:", Autoneg(msg.Autoneg)) +
		fmt.Sprintln("\tsupported:",
			(*EthtoolLinkModeBits)(&msg.Link_modes_supported)) +
		fmt.Sprintln("\tadvertising:",
			(*EthtoolLinkModeBits)(&msg.Link_modes_advertising)) +
		fmt.Sprintln("\tpartner:",
			(*EthtoolLinkModeBits)(&msg.Link_modes_lp_advertising))
}
