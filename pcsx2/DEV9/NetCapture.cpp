// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "NetCapture.h"

#include "PacketReader/EthernetFrame.h"
#include "PacketReader/ARP/ARP_Packet.h"
#include "PacketReader/IP/IP_Packet.h"
#include "PacketReader/IP/IP_Payload.h"
#include "PacketReader/IP/ICMP/ICMP_Packet.h"
#include "PacketReader/IP/TCP/TCP_Packet.h"
#include "PacketReader/IP/UDP/UDP_Packet.h"

#include "common/Console.h"

#include "fmt/format.h"

#include <chrono>
#include <cstring>

using namespace PacketReader;
using namespace PacketReader::IP;
using namespace PacketReader::IP::TCP;
using namespace PacketReader::IP::UDP;
using namespace PacketReader::IP::ICMP;
using namespace PacketReader::ARP;

namespace DEV9
{
	NetCapture g_netCapture;

	// -----------------------------------------------------------------------
	// Helpers
	// -----------------------------------------------------------------------

	static u64 CurrentTimestampUs()
	{
		using namespace std::chrono;
		return static_cast<u64>(
			duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
	}

	static std::string FormatMAC(const u8* mac)
	{
		return fmt::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	}

	static std::string FormatIP(const u8* ip)
	{
		return fmt::format("{}.{}.{}.{}", ip[0], ip[1], ip[2], ip[3]);
	}

	// -----------------------------------------------------------------------
	// NetCapture – public interface
	// -----------------------------------------------------------------------

	void NetCapture::Enable(u8 directionFilter, u16 maxQueue)
	{
		std::lock_guard lock(m_mutex);
		m_directionFilter = directionFilter;
		m_maxQueue = (maxQueue == 0) ? 1000 : maxQueue;
		m_enabled.store(true, std::memory_order_release);
	}

	void NetCapture::Disable()
	{
		m_enabled.store(false, std::memory_order_release);
		std::lock_guard lock(m_mutex);
		m_queue.clear();
	}

	void NetCapture::Reset()
	{
		m_enabled.store(false, std::memory_order_release);
		{
			std::lock_guard lock(m_mutex);
			m_queue.clear();
			m_filter = {};
		}
		m_txPackets.store(0, std::memory_order_relaxed);
		m_rxPackets.store(0, std::memory_order_relaxed);
		m_txBytes.store(0, std::memory_order_relaxed);
		m_rxBytes.store(0, std::memory_order_relaxed);
		m_dropped.store(0, std::memory_order_relaxed);
	}

	void NetCapture::OnTx(NetPacket* pkt)
	{
		if (pkt == nullptr || pkt->size <= 0)
			return;

		m_txPackets.fetch_add(1, std::memory_order_relaxed);
		m_txBytes.fetch_add(static_cast<u64>(pkt->size), std::memory_order_relaxed);

		if (!m_enabled.load(std::memory_order_acquire))
			return;
		if (m_directionFilter == 2) // RX-only
			return;
		if (!PassesFilter(pkt, CaptureDirection::TX))
			return;

		CapturedPacket cp;
		cp.direction = CaptureDirection::TX;
		cp.timestamp_us = CurrentTimestampUs();
		cp.packet = *pkt;

		std::lock_guard lock(m_mutex);
		if (static_cast<u16>(m_queue.size()) >= m_maxQueue)
		{
			m_dropped.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		m_queue.push_back(std::move(cp));
	}

	void NetCapture::OnRx(NetPacket* pkt)
	{
		if (pkt == nullptr || pkt->size <= 0)
			return;

		m_rxPackets.fetch_add(1, std::memory_order_relaxed);
		m_rxBytes.fetch_add(static_cast<u64>(pkt->size), std::memory_order_relaxed);

		if (!m_enabled.load(std::memory_order_acquire))
			return;
		if (m_directionFilter == 1) // TX-only
			return;
		if (!PassesFilter(pkt, CaptureDirection::RX))
			return;

		CapturedPacket cp;
		cp.direction = CaptureDirection::RX;
		cp.timestamp_us = CurrentTimestampUs();
		cp.packet = *pkt;

		std::lock_guard lock(m_mutex);
		if (static_cast<u16>(m_queue.size()) >= m_maxQueue)
		{
			m_dropped.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		m_queue.push_back(std::move(cp));
	}

	std::vector<CapturedPacket> NetCapture::Drain(u16 maxPackets)
	{
		std::lock_guard lock(m_mutex);
		std::vector<CapturedPacket> result;
		result.reserve(std::min<size_t>(maxPackets, m_queue.size()));
		while (!m_queue.empty() && result.size() < maxPackets)
		{
			result.push_back(std::move(m_queue.front()));
			m_queue.pop_front();
		}
		return result;
	}

	NetCaptureStats NetCapture::GetStats() const
	{
		NetCaptureStats s;
		s.tx_packets = m_txPackets.load(std::memory_order_relaxed);
		s.rx_packets = m_rxPackets.load(std::memory_order_relaxed);
		s.tx_bytes = m_txBytes.load(std::memory_order_relaxed);
		s.rx_bytes = m_rxBytes.load(std::memory_order_relaxed);
		s.dropped_count = m_dropped.load(std::memory_order_relaxed);
		{
			std::lock_guard lock(m_mutex);
			s.capture_queue_size = static_cast<u16>(
				std::min<size_t>(m_queue.size(), 0xFFFFu));
		}
		return s;
	}

	void NetCapture::SetFilter(const NetCaptureFilter& filter)
	{
		std::lock_guard lock(m_mutex);
		m_filter = filter;
	}

	// -----------------------------------------------------------------------
	// Filter logic
	// -----------------------------------------------------------------------

	bool NetCapture::PassesFilter(const NetPacket* pkt, CaptureDirection /*direction*/) const
	{
		// m_mutex is already held by the caller when this is invoked from OnTx/OnRx.
		// We access m_filter without an extra lock since we hold m_mutex.
		// (SetFilter also holds m_mutex, so there is no data race.)
		if (m_filter.type == 0)
			return true;

		if (pkt->size < 14)
			return true; // Too short to parse; pass through.

		const auto* buf = reinterpret_cast<const u8*>(pkt->buffer);
		const u16 etherType = static_cast<u16>((buf[12] << 8) | buf[13]);

		if (m_filter.type == 1) // by protocol
		{
			switch (m_filter.protocol)
			{
				case 0: return true; // all
				case 4: // ARP
					return etherType == 0x0806;
				default:
					break;
			}
			if (etherType != 0x0800) // IPv4
				return false;
			if (pkt->size < 20)
				return false;
			const u8 ipProto = buf[14 + 9]; // IP header protocol byte
			switch (m_filter.protocol)
			{
				case 1: return ipProto == 0x06; // TCP
				case 2: return ipProto == 0x11; // UDP
				case 3: return ipProto == 0x01; // ICMP
				case 5: // DNS (UDP port 53)
				case 6: // DHCP (UDP port 67/68)
				{
					if (ipProto != 0x11)
						return false;
					const int ihl = (buf[14] & 0x0F) * 4;
					if (pkt->size < 14 + ihl + 4)
						return false;
					const u16 dport = static_cast<u16>(
						(buf[14 + ihl + 2] << 8) | buf[14 + ihl + 3]);
					const u16 sport = static_cast<u16>(
						(buf[14 + ihl + 0] << 8) | buf[14 + ihl + 1]);
					if (m_filter.protocol == 5)
						return sport == 53 || dport == 53;
					return sport == 67 || sport == 68 || dport == 67 || dport == 68;
				}
				default:
					return true;
			}
		}
		else if (m_filter.type == 2) // by port
		{
			if (etherType != 0x0800 || pkt->size < 34)
				return false;
			const u8 ipProto = buf[14 + 9];
			if (ipProto != 0x06 && ipProto != 0x11)
				return false;
			const int ihl = (buf[14] & 0x0F) * 4;
			if (pkt->size < 14 + ihl + 4)
				return false;
			const u16 sport = static_cast<u16>((buf[14 + ihl + 0] << 8) | buf[14 + ihl + 1]);
			const u16 dport = static_cast<u16>((buf[14 + ihl + 2] << 8) | buf[14 + ihl + 3]);
			return sport == m_filter.port || dport == m_filter.port;
		}
		else if (m_filter.type == 3) // by IP
		{
			if (etherType != 0x0800 || pkt->size < 34)
				return false;
			const u8* src = buf + 14 + 12;
			const u8* dst = buf + 14 + 16;
			return std::memcmp(src, m_filter.ip, 4) == 0 ||
			       std::memcmp(dst, m_filter.ip, 4) == 0;
		}
		return true;
	}

	// -----------------------------------------------------------------------
	// Packet dissection
	// -----------------------------------------------------------------------

	std::string NetCapture::DisassemblePacket(const u8* data, int size)
	{
		if (data == nullptr || size < 14)
			return "[Packet too short to dissect]";

		// We need a NetPacket to hand to EthernetFrame's constructor.
		NetPacket pkt;
		pkt.size = std::min(size, static_cast<int>(sizeof(pkt.buffer)));
		std::memcpy(pkt.buffer, data, pkt.size);

		std::string out;
		out.reserve(512);

		try
		{
			EthernetFrame frame(&pkt);
			out += fmt::format(
				"[Ethernet] dst={} src={} type=0x{:04X}\n",
				FormatMAC(frame.destinationMAC.bytes),
				FormatMAC(frame.sourceMAC.bytes),
				frame.protocol);

			const auto etherType = static_cast<EtherType>(frame.protocol);

			if (etherType == EtherType::ARP)
			{
				PayloadPtr* pl = static_cast<PayloadPtr*>(frame.GetPayload());
				if (pl->GetLength() >= 8)
				{
					ARP_Packet arp(pl->data, pl->GetLength());
					const char* opStr = (arp.op == 1) ? "request" : (arp.op == 2) ? "reply" : "?";
					std::string sha = (arp.hardwareAddressLength == 6) ? FormatMAC(arp.senderHardwareAddress.get()) : "?";
					std::string spa = (arp.protocolAddressLength == 4) ? FormatIP(arp.senderProtocolAddress.get()) : "?";
					std::string tha = (arp.hardwareAddressLength == 6) ? FormatMAC(arp.targetHardwareAddress.get()) : "?";
					std::string tpa = (arp.protocolAddressLength == 4) ? FormatIP(arp.targetProtocolAddress.get()) : "?";
					out += fmt::format(
						"[ARP] op={} ({}) sender={}/{} target={}/{}\n",
						arp.op, opStr, sha, spa, tha, tpa);
				}
			}
			else if (etherType == EtherType::IPv4)
			{
				PayloadPtr* pl = static_cast<PayloadPtr*>(frame.GetPayload());
				if (pl->GetLength() < 20)
				{
					out += "[IPv4] (header too short)\n";
					return out;
				}
				IP_Packet ippkt(pl->data, pl->GetLength());

				const char* protoStr = "?";
				const u8 proto = ippkt.protocol;
				if (proto == static_cast<u8>(IP_Type::ICMP)) protoStr = "ICMP";
				else if (proto == static_cast<u8>(IP_Type::TCP)) protoStr = "TCP";
				else if (proto == static_cast<u8>(IP_Type::UDP)) protoStr = "UDP";
				else if (proto == static_cast<u8>(IP_Type::IGMP)) protoStr = "IGMP";

				out += fmt::format(
					"[IPv4] src={} dst={} proto={} ({}) ttl={} id=0x{:04X} len={}\n",
					FormatIP(ippkt.sourceIP.bytes),
					FormatIP(ippkt.destinationIP.bytes),
					proto, protoStr,
					ippkt.timeToLive,
					// id is private; just report protocol-level info without it
					0,
					pl->GetLength());

				IP_Payload* ipPay = ippkt.GetPayload();
				if (ipPay == nullptr)
					return out;

				if (proto == static_cast<u8>(IP_Type::ICMP))
				{
					IP_PayloadPtr* ipp = static_cast<IP_PayloadPtr*>(ipPay);
					if (ipp->GetLength() >= 4)
					{
						ICMP_Packet icmp(ipp->data, ipp->GetLength());
						out += fmt::format(
							"[ICMP] type={} code={}\n",
							icmp.type, icmp.code);
					}
				}
				else if (proto == static_cast<u8>(IP_Type::TCP))
				{
					IP_PayloadPtr* ipp = static_cast<IP_PayloadPtr*>(ipPay);
					if (ipp->GetLength() >= 20)
					{
						TCP_Packet tcp(ipp->data, ipp->GetLength());

						std::string flags;
						if (tcp.GetSYN()) flags += "SYN ";
						if (tcp.GetACK()) flags += "ACK ";
						if (tcp.GetFIN()) flags += "FIN ";
						if (tcp.GetRST()) flags += "RST ";
						if (tcp.GetPSH()) flags += "PSH ";
						if (tcp.GetURG()) flags += "URG ";
						if (flags.empty()) flags = "none";
						else flags.pop_back(); // remove trailing space

						out += fmt::format(
							"[TCP] src={} dst={} seq={} ack={} flags=[{}] win={} payload={}B\n",
							tcp.sourcePort, tcp.destinationPort,
							tcp.sequenceNumber, tcp.acknowledgementNumber,
							flags, tcp.windowSize,
							tcp.GetPayload() ? tcp.GetPayload()->GetLength() : 0);
					}
				}
				else if (proto == static_cast<u8>(IP_Type::UDP))
				{
					IP_PayloadPtr* ipp = static_cast<IP_PayloadPtr*>(ipPay);
					if (ipp->GetLength() >= 8)
					{
						UDP_Packet udp(ipp->data, ipp->GetLength());
						out += fmt::format(
							"[UDP] src={} dst={} payload={}B\n",
							udp.sourcePort, udp.destinationPort,
							udp.GetPayload() ? udp.GetPayload()->GetLength() : 0);
					}
				}
			}
			else
			{
				out += fmt::format("[Unknown EtherType 0x{:04X}] {} bytes payload\n",
					frame.protocol,
					frame.GetPayload() ? frame.GetPayload()->GetLength() : 0);
			}
		}
		catch (...)
		{
			out += "[Parse error]\n";
		}

		return out;
	}

} // namespace DEV9
