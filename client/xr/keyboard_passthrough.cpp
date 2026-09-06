/*
 * WiVRn VR streaming
 * Copyright (C) 2026  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2026  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "keyboard_passthrough.h"

#include "xr/instance.h"
#include "xr/session.h"
#include <spdlog/spdlog.h>
#include <string>

namespace
{
// re-issue the spatial anchor query at most this often while no keyboard has been found yet
constexpr XrDuration requery_interval = 1'000'000'000; // 1s, predictedDisplayTime is in ns

// tear down the passthrough hole if the keyboard hasn't been visible (valid
// pose + bounding box) for this long, so it doesn't sit there burning
// compositor/GPU resources for a keyboard that's out of view/turned off;
// it's recreated automatically once tracking recovers
constexpr XrDuration keyboard_lost_timeout = 60'000'000'000; // 60s
} // namespace

xr::keyboard_passthrough::keyboard_passthrough(instance & inst, session & session) :
        tracker(inst, session),
        anchor(inst, session)
{
}

void xr::keyboard_passthrough::enable()
{
	if (enabled_)
		return;

	if (not supported())
	{
		spdlog::warn("keyboard_passthrough: enable() called but not supported (missing extensions)");
		return;
	}

	enabled_ = true;
	if (not tracker.create())
		spdlog::warn("keyboard_passthrough: dynamic_object_tracker.create() failed");
}

void xr::keyboard_passthrough::disable()
{
	if (not enabled_)
		return;

	enabled_ = false;
	geometry.reset();
	passthrough.reset();
	keyboard_space = XR_NULL_HANDLE;
	locatable_request_id = 0;
	next_query_time = 0;
	next_log_time = 0;
	last_visible_time = 0;
	tracker.destroy();
}

void xr::keyboard_passthrough::handle_event(const XrEventDataBaseHeader & header)
{
	if (tracker.handle_event(header))
		return;
	anchor.handle_event(header);
}

void xr::keyboard_passthrough::update(instance & inst, session & sess, XrSpace base_space, XrTime predicted_display_time)
{
	if (not enabled_)
		return;

	// throttled "what stage are we stuck at" heartbeat, at most once a second,
	// since most of the early-return paths below would otherwise log every frame
	bool log_now = predicted_display_time > next_log_time;
	if (log_now)
		next_log_time = predicted_display_time + requery_interval;

	if (geometry and predicted_display_time - last_visible_time > keyboard_lost_timeout)
	{
		spdlog::info("keyboard_passthrough: keyboard not visible for a while, releasing the passthrough hole");
		geometry.reset();
		passthrough.reset();
		last_visible_time = 0;
	}

	switch (tracker.get_state())
	{
		case dynamic_object_tracker::state::empty:
			if (log_now)
				spdlog::info("keyboard_passthrough: tracker not created (creation failed earlier?)");
			return;
		case dynamic_object_tracker::state::creating:
			if (log_now)
				spdlog::info("keyboard_passthrough: waiting for the tracker to be created");
			return;
		case dynamic_object_tracker::state::initializing:
			if (log_now)
				spdlog::info("keyboard_passthrough: waiting for tracked classes to be set");
			return;
		case dynamic_object_tracker::state::created:
			tracker.set_tracked_classes_keyboard();
			return;
		case dynamic_object_tracker::state::initialized:
			break;
	}

	if (not passthrough)
		passthrough.emplace(inst, sess, sess.ensure_shared_passthrough_object());

	if (keyboard_space == XR_NULL_HANDLE)
	{
		if (not anchor.query_pending() and predicted_display_time > next_query_time)
		{
			anchor.query_spaces_with_component(XR_SPACE_COMPONENT_TYPE_DYNAMIC_OBJECT_DATA_META);
			next_query_time = predicted_display_time + requery_interval;
		}

		auto spaces = anchor.find_spaces([&](XrSpace space) {
			if (not anchor.is_component_supported(space, XR_SPACE_COMPONENT_TYPE_DYNAMIC_OBJECT_DATA_META))
				return false;

			XrDynamicObjectClassMETA object_class;
			return tracker.get_dynamic_object_class(space, object_class) and
			       object_class == XR_DYNAMIC_OBJECT_CLASS_KEYBOARD_META;
		});

		if (spaces.empty())
		{
			if (log_now)
				spdlog::info("keyboard_passthrough: no tracked keyboard found yet "
				              "(pair/enable tracking in the headset's Settings > Devices > Keyboard)");
			return;
		}

		keyboard_space = spaces.front();

		std::string components;
		for (auto c: anchor.list_supported_components(keyboard_space))
			components += std::to_string((int)c) + " ";
		spdlog::info("keyboard_passthrough: tracked keyboard spatial anchor found, supported components: {}", components);
	}

	bool locatable = false;
	if (not anchor.get_component_status(keyboard_space, XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB, locatable))
	{
		if (log_now)
			spdlog::warn("keyboard_passthrough: failed to query the LOCATABLE component status");
		return;
	}

	if (not locatable)
	{
		if (log_now)
			spdlog::info("keyboard_passthrough: keyboard anchor is not locatable yet, requesting it");
		if (not anchor.request_pending(locatable_request_id))
			anchor.request_set_component_status(keyboard_space, XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB, true, locatable_request_id);
		return;
	}

	XrSpaceLocation location{
	        .type = XR_TYPE_SPACE_LOCATION,
	};
	if (XR_FAILED(xrLocateSpace(keyboard_space, base_space, predicted_display_time, &location)))
	{
		if (log_now)
			spdlog::warn("keyboard_passthrough: xrLocateSpace failed for the keyboard anchor");
		return;
	}

	if (not(location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) or
	    not(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
	{
		if (log_now)
			spdlog::info("keyboard_passthrough: keyboard pose not valid yet");
		return;
	}

	XrRect3DfFB bounding_box{};
	if (not anchor.get_bounding_box_3d(keyboard_space, bounding_box))
	{
		if (log_now)
			spdlog::warn("keyboard_passthrough: failed to get the keyboard's bounding box");
		return;
	}

	if (not geometry)
	{
		geometry.emplace(inst, sess, passthrough->layer_handle(), base_space);
		spdlog::info("keyboard_passthrough: passthrough hole created over the tracked keyboard "
		              "({:.2f}x{:.2f} m)",
		              bounding_box.extent.width,
		              bounding_box.extent.height);
	}

	// the quad lies flat on the anchor's XY plane, so only width/height matter
	geometry->set_transform(
	        predicted_display_time,
	        location.pose,
	        XrVector3f{bounding_box.extent.width, bounding_box.extent.height, 1});
	last_visible_time = predicted_display_time;
}
