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

#pragma once

#include "dynamic_object_tracker.h"
#include "passthrough.h"
#include "spatial_anchor.h"
#include <optional>
#include <openxr/openxr.h>

namespace xr
{
class instance;
class session;

// Shows a passthrough cutout over the physical (Bluetooth-paired) keyboard
// while otherwise fully immersed in VR: uses Meta's Dynamic Object Tracker
// (XR_META_dynamic_object_tracker/_keyboard) to find the keyboard's spatial
// anchor, then a plain quad passthrough geometry instance (XR_FB_passthrough
// + XR_FB_triangle_mesh) sized to its bounding box to punch a hole where it
// is. No feathered edges, corner markers or hand-proximity gating: the hole
// just tracks the keyboard's bounding box directly every frame.
// instance/session are never stored across calls (only taken as parameters
// where needed): this object is a member of xr::session, which is
// move-assigned into place at startup (see application.cpp), so any
// instance*/session* captured at construction time would end up dangling,
// pointing at the moved-from temporary rather than the final object.
class keyboard_passthrough
{
	dynamic_object_tracker tracker;
	spatial_anchor anchor;

	std::optional<passthrough_projected_fb> passthrough;
	std::optional<geometry_instance_fb> geometry;

	XrSpace keyboard_space = XR_NULL_HANDLE;
	XrAsyncRequestIdFB locatable_request_id = 0;
	XrTime next_query_time = 0;

	// throttles the diagnostic "what stage are we stuck at" log to ~1/s
	XrTime next_log_time = 0;

	// last time geometry's transform was actually updated (valid pose +
	// bounding box); the passthrough hole is torn down if this goes stale
	// for too long, see keyboard_lost_timeout in keyboard_passthrough.cpp
	XrTime last_visible_time = 0;

	bool enabled_ = false;

public:
	keyboard_passthrough() = default;
	keyboard_passthrough(instance &, session &);

	bool supported() const
	{
		return tracker.supported() and anchor.supported();
	}

	void enable();
	void disable();
	bool enabled() const
	{
		return enabled_;
	}

	// call once per frame while enabled; advances the (asynchronous) tracker
	// state machine, (re)locates the keyboard and updates the passthrough hole
	void update(instance &, session &, XrSpace base_space, XrTime predicted_display_time);

	// forward every polled XR event here
	void handle_event(const XrEventDataBaseHeader &);

	// null unless a passthrough layer is currently active
	XrCompositionLayerBaseHeader * layer()
	{
		return passthrough ? passthrough->layer() : nullptr;
	}
};
} // namespace xr
