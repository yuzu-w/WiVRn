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

// meta_dynamic_object_keyboard.h uses XrDynamicObjectClassMETA from
// meta_dynamic_object_tracker.h without including it itself, so this order matters
#include "meta_dynamic_object_tracker.h"
#include "meta_dynamic_object_keyboard.h"
#include <openxr/openxr.h>

namespace xr
{
class instance;
class session;

// Wraps XR_META_dynamic_object_tracker + XR_META_dynamic_object_keyboard
// (Meta's replacement for the deprecated XR_FB_keyboard_tracking, as of
// Quest OS v72). Tracker creation and setting which object classes to track
// are both asynchronous: the runtime replies with an XrEventData*ResultMETA
// event, which the owner must forward to handle_event().
class dynamic_object_tracker
{
public:
	enum class state
	{
		empty,        // not created
		creating,     // xrCreateDynamicObjectTrackerMETA called, waiting for the create event
		created,      // created, tracked classes not set yet
		initializing, // xrSetDynamicObjectTrackedClassesMETA called, waiting for its event
		initialized,  // ready: query spatial anchors for XR_SPACE_COMPONENT_TYPE_DYNAMIC_OBJECT_DATA_META
	};

private:
	XrSession sess = XR_NULL_HANDLE;
	XrDynamicObjectTrackerMETA tracker = XR_NULL_HANDLE;

	PFN_xrCreateDynamicObjectTrackerMETA xrCreateDynamicObjectTrackerMETA{};
	PFN_xrDestroyDynamicObjectTrackerMETA xrDestroyDynamicObjectTrackerMETA{};
	PFN_xrSetDynamicObjectTrackedClassesMETA xrSetDynamicObjectTrackedClassesMETA{};
	PFN_xrGetSpaceDynamicObjectDataMETA xrGetSpaceDynamicObjectDataMETA{};

	state state_ = state::empty;

public:
	dynamic_object_tracker() = default;
	dynamic_object_tracker(instance &, session &);

	// no-op (and returns false) if the extensions are unavailable, or a tracker already exists
	bool create();
	void destroy();

	// only valid once get_state() == created
	bool set_tracked_classes_keyboard();

	// forward every polled XR event here; returns true if it was consumed
	bool handle_event(const XrEventDataBaseHeader &);

	state get_state() const
	{
		return state_;
	}

	bool supported() const
	{
		return xrCreateDynamicObjectTrackerMETA != nullptr;
	}

	bool get_dynamic_object_class(XrSpace space, XrDynamicObjectClassMETA & out) const;
};
} // namespace xr
