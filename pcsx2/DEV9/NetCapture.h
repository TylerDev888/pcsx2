// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "net.h"

namespace DEV9
{
	/// Direction of a captured packet.
	enum struct CaptureDirection : u8
	{
		TX = 0, ///< PS2 → Host (outgoing)
		RX = 1, ///< Host → PS2 (incoming)
	};

	/// A single packet recorded by the capture subsystem.
	struct CapturedPacket
	{
		CaptureDirection direction;
		u64 timestamp_us; ///< Monotonic timestamp in microseconds.
		NetPacket packet;
	};

	/// Snapshot of capture statistics returned by MsgNetGetStats.
	struct NetCaptureStats
	{
		u32 tx_packets;
		u32 rx_packets;
		u64 tx_bytes;
		u64 rx_bytes;
		u16 capture_queue_size;
		u32 dropped_count;
	};

	/// Filter applied to the capture queue.
	struct NetCaptureFilter
	{
		/// Filter type:
		///   0 = none (capture all)
		///   1 = by L3/L4 protocol (protocol field below)
		///   2 = by TCP/UDP port (port field below)
		///   3 = by source or destination IP (ip field below)
		u8 type{0};

		/// For type 1: 0=all, 1=TCP, 2=UDP, 3=ICMP, 4=ARP, 5=DNS(UDP/53), 6=DHCP(UDP/67-68)
		u8 protocol{0};

		/// For type 2: port number (host byte order).
		u16 port{0};

		/// For type 3: IPv4 address (4 bytes, network byte order).
		u8 ip[4]{};
	};

	/**
	 * NetCapture – packet capture and dissection subsystem for PINE.
	 *
	 * OnTx() / OnRx() are called from the DEV9 TX/RX hot paths and must be
	 * lock-free when capture is disabled.  When enabled they acquire m_mutex
	 * only long enough to push into the deque.
	 */
	class NetCapture
	{
	public:
		/// Enable capture.  @p directionFilter: 0=both, 1=TX-only, 2=RX-only.
		void Enable(u8 directionFilter, u16 maxQueue);

		/// Disable capture and clear the queue.
		void Disable();

		/// Reset all counters and clear the queue (called on VM reset/init).
		void Reset();

		/// Called when the PS2 sends a packet (TX path).
		void OnTx(NetPacket* pkt);

		/// Called when the PS2 receives a packet (RX path).
		void OnRx(NetPacket* pkt);

		/// Pop up to @p maxPackets entries from the queue.
		std::vector<CapturedPacket> Drain(u16 maxPackets);

		/// Snapshot current counters.
		NetCaptureStats GetStats() const;

		/// Update the capture filter.
		void SetFilter(const NetCaptureFilter& filter);

		/// Dissect a raw Ethernet frame and return a human-readable text summary.
		static std::string DisassemblePacket(const u8* data, int size);

	private:
		bool PassesFilter(const NetPacket* pkt, CaptureDirection direction) const;

		std::atomic<bool> m_enabled{false};
		u8 m_directionFilter{0}; ///< 0=both, 1=TX-only, 2=RX-only
		u16 m_maxQueue{1000};

		mutable std::mutex m_mutex;
		std::deque<CapturedPacket> m_queue;
		NetCaptureFilter m_filter{};

		std::atomic<u32> m_txPackets{0};
		std::atomic<u32> m_rxPackets{0};
		std::atomic<u64> m_txBytes{0};
		std::atomic<u64> m_rxBytes{0};
		std::atomic<u32> m_dropped{0};
	};

	/// Global capture instance – initialised alongside the DEV9 network adapter.
	extern NetCapture g_netCapture;

} // namespace DEV9
