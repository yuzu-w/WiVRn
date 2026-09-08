/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include <functional>
#include <pthread.h>
#include <string>
#include <thread>
#include <utility>

#ifdef __linux__
#include <cerrno>
#include <cstring>
#include <spdlog/spdlog.h>
#include <sys/resource.h>
#endif

namespace utils
{
template <typename... Args>
std::thread named_thread(const std::string & name, Args &&... args)
{
	std::thread t{std::forward<Args>(args)...};
	pthread_setname_np(t.native_handle(), name.substr(0, 15).c_str());
	return t;
}

// Set the nice value of the calling thread, no-op on failure or on non-Linux platforms
inline void set_thread_nice(const std::string & name, int nice)
{
#ifdef __linux__
	// On Linux, PRIO_PROCESS with who = 0 applies to the calling thread only
	if (setpriority(PRIO_PROCESS, 0, nice) == 0)
		spdlog::info("Thread {}: nice value set to {}", name, nice);
	else
		spdlog::warn("Thread {}: failed to set nice value to {}: {}", name, nice, strerror(errno));
#endif
}

// Same as named_thread, with the nice value set from inside the thread before the function runs
template <typename F, typename... Args>
std::thread named_thread(const std::string & name, int nice, F && f, Args &&... args)
{
	return named_thread(
	        name,
	        [name, nice, f = std::forward<F>(f), ... args = std::forward<Args>(args)]() mutable {
		        set_thread_nice(name, nice);
		        std::invoke(std::move(f), std::move(args)...);
	        });
}
} // namespace utils
