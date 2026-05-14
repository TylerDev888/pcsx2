// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

// GSStreamServer
// --------------
// Push-only TCP server that streams the GS framebuffer as MJPEG to any
// connected subscribers. Designed to coexist with PINE without sharing its
// serial request/response pipe — frame delivery has its own thread, encoder,
// and socket so it cannot starve low-latency PINE traffic.
//
// Lifecycle: PINE-controlled. PCSX2 brings the listener up alongside PINE
// (when EnablePINE is set) and tears it down at VM shutdown. PINE opcodes
// 0x40 / 0x41 expose the bound port and runtime stats so the bridge can
// discover where to connect.
//
// Wire format, per frame, little-endian:
//   u32 size       — bytes following this field (header + payload)
//   u8  msg_type   — 1 = frame
//   u32 frame_idx
//   u16 width
//   u16 height
//   u8  codec      — 1 = MJPEG
//   u8  flags      — reserved, 0
//   bytes payload  — codec-specific (a complete JPEG for codec=1)

namespace GSStreamServer
{
	bool IsInitialized();
	int  GetPort();          // bound port (or -1 when not initialized)
	bool HasSubscribers();   // cheap, lock-free; safe to call from GS thread

	bool Initialize(int port = 0); // 0 = let the OS pick a free port
	void Deinitialize();

	// Called from the GS (MTGS) thread once per VSync when HasSubscribers() is true.
	// `pixels` points to width*height tightly-packed RGBA8 u32 values.
	// The buffer is consumed (copied) before this call returns.
	void DeliverRGBA(u32 width, u32 height, const u32* pixels);

	struct Stats
	{
		u32 frames_delivered;
		u32 frames_dropped;
		u32 subscribers;
		u32 bound_port;
	};
	Stats GetStats();
} // namespace GSStreamServer
