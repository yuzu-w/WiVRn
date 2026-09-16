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

#include "external_gaze.h"

#include "compositor/compositor.h"
#include "os/os_time.h"
#include "util/u_logging.h"

#include <bit>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>

static_assert(std::endian::native == std::endian::little, "external gaze packets are little endian");

namespace wivrn
{

external_gaze_receiver::external_gaze_receiver(compositor & comp, int port, std::chrono::milliseconds timeout) :
        comp(comp), timeout(timeout)
{
	fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		throw std::system_error(errno, std::generic_category(), "socket");

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
	{
		int err = errno;
		close(fd);
		throw std::system_error(err, std::generic_category(), "bind 127.0.0.1:" + std::to_string(port));
	}

	U_LOG_I("External gaze receiver listening on 127.0.0.1:%d, timeout %ld ms", port, long(timeout.count()));
	thread = std::jthread([this](std::stop_token stop) { run(stop); });
}

external_gaze_receiver::~external_gaze_receiver()
{
	thread.request_stop();
	thread.join();
	close(fd);
}

void external_gaze_receiver::run(std::stop_token stop)
{
	pthread_setname_np(pthread_self(), "external_gaze");

	using clock = std::chrono::steady_clock;
	const auto log_period = std::chrono::seconds(5);

	bool have_gaze = false;
	clock::time_point deadline{};

	// statistics for the debug log
	int packets = 0;
	int rejected = 0;
	int64_t age_sum_ns = 0;
	packet last{};
	auto next_log = clock::now() + log_period;

	while (not stop.stop_requested())
	{
		auto now = clock::now();
		int wait_ms = 200;
		if (have_gaze)
			wait_ms = std::clamp<int>(std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count(), 0, 200);

		pollfd p{.fd = fd, .events = POLLIN, .revents = 0};
		int r = poll(&p, 1, wait_ms);
		now = clock::now();

		if (r > 0)
		{
			uint8_t buffer[64];
			ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
			if (n == sizeof(packet))
			{
				packet pkt;
				std::memcpy(&pkt, buffer, sizeof(pkt));
				if (pkt.magic == magic and pkt.version == version)
				{
					packets++;
					last = pkt;
					age_sum_ns += os_monotonic_get_ns() - pkt.timestamp_ns;
					if (pkt.flags & flag_valid)
					{
						comp.set_external_gaze(xrt_quat{pkt.x, pkt.y, pkt.z, pkt.w});
						if (not have_gaze)
							U_LOG_I("External gaze: receiving, foveation follows the external eye tracker");
						have_gaze = true;
						deadline = now + timeout;
					}
					else if (have_gaze)
					{
						U_LOG_I("External gaze: sender reports no gaze, back to default foveation center");
						comp.set_external_gaze(xrt_quat{});
						have_gaze = false;
					}
				}
				else
					rejected++;
			}
			else if (n >= 0)
				rejected++;
		}

		if (have_gaze and now >= deadline)
		{
			U_LOG_I("External gaze: timed out, back to default foveation center");
			comp.set_external_gaze(xrt_quat{});
			have_gaze = false;
		}

		if (now >= next_log)
		{
			if (packets or rejected)
				U_LOG_D("External gaze: %.0f packets/s, %d rejected, mean age %.1f ms, last %s (%.3f, %.3f, %.3f, %.3f)",
				        packets / std::chrono::duration<double>(log_period).count(),
				        rejected,
				        packets ? age_sum_ns / packets / 1e6 : 0.0,
				        (last.flags & flag_valid) ? "valid" : "invalid",
				        last.x,
				        last.y,
				        last.z,
				        last.w);
			packets = rejected = 0;
			age_sum_ns = 0;
			next_log = now + log_period;
		}
	}
}

} // namespace wivrn
