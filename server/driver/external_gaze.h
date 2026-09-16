/*
 * WiVRn VR streaming
 * Copyright (C) 2026  yuzu-w
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

namespace wivrn
{

class compositor;

// Receives a gaze direction from an external eye tracker over loopback UDP
// and feeds it to the compositor's foveation.
//
// Packet (little endian, 32 bytes):
//   u32 magic "WVGZ" (0x5A475657), u16 version = 1, u16 flags (bit 0: valid),
//   i64 timestamp_ns (CLOCK_MONOTONIC of the sender), f32 quaternion x, y, z, w
//   in view (head) space, -Z forward.
// A packet without the valid flag, or no packet for `timeout`, clears the gaze
// so foveation falls back to its default center.
class external_gaze_receiver
{
public:
	struct packet
	{
		uint32_t magic;
		uint16_t version;
		uint16_t flags;
		int64_t timestamp_ns;
		float x, y, z, w;
	};
	static_assert(sizeof(packet) == 32);

	static constexpr uint32_t magic = 0x5A475657; // "WVGZ"
	static constexpr uint16_t version = 1;
	static constexpr uint16_t flag_valid = 1;

	external_gaze_receiver(compositor &, int port, std::chrono::milliseconds timeout);
	~external_gaze_receiver();

	external_gaze_receiver(const external_gaze_receiver &) = delete;
	external_gaze_receiver & operator=(const external_gaze_receiver &) = delete;

private:
	compositor & comp;
	std::chrono::milliseconds timeout;
	int fd = -1;
	std::jthread thread;

	void run(std::stop_token);
};

} // namespace wivrn
