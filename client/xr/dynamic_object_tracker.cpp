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

#include "dynamic_object_tracker.h"

#include "xr/instance.h"
#include "xr/session.h"
#include <iterator>
#include <spdlog/spdlog.h>

xr::dynamic_object_tracker::dynamic_object_tracker(instance & inst, session & session)
{
	if (not(inst.has_extension(XR_META_DYNAMIC_OBJECT_TRACKER_EXTENSION_NAME) and
	        inst.has_extension(XR_META_DYNAMIC_OBJECT_KEYBOARD_EXTENSION_NAME)))
		return;

	sess = session;
	xrCreateDynamicObjectTrackerMETA = inst.get_proc<PFN_xrCreateDynamicObjectTrackerMETA>("xrCreateDynamicObjectTrackerMETA");
	xrDestroyDynamicObjectTrackerMETA = inst.get_proc<PFN_xrDestroyDynamicObjectTrackerMETA>("xrDestroyDynamicObjectTrackerMETA");
	xrSetDynamicObjectTrackedClassesMETA = inst.get_proc<PFN_xrSetDynamicObjectTrackedClassesMETA>("xrSetDynamicObjectTrackedClassesMETA");
	xrGetSpaceDynamicObjectDataMETA = inst.get_proc<PFN_xrGetSpaceDynamicObjectDataMETA>("xrGetSpaceDynamicObjectDataMETA");
}

bool xr::dynamic_object_tracker::create()
{
	if (not supported() or state_ != state::empty)
		return false;

	XrDynamicObjectTrackerCreateInfoMETA info{
	        .type = XR_TYPE_DYNAMIC_OBJECT_TRACKER_CREATE_INFO_META,
	};

	XrResult res = xrCreateDynamicObjectTrackerMETA(sess, &info, &tracker);
	if (XR_FAILED(res))
	{
		spdlog::warn("xrCreateDynamicObjectTrackerMETA failed: {}", (int)res);
		return false;
	}

	state_ = state::creating;
	return true;
}

void xr::dynamic_object_tracker::destroy()
{
	if (state_ == state::empty)
		return;

	if (tracker != XR_NULL_HANDLE)
		xrDestroyDynamicObjectTrackerMETA(tracker);

	tracker = XR_NULL_HANDLE;
	state_ = state::empty;
}

bool xr::dynamic_object_tracker::set_tracked_classes_keyboard()
{
	if (state_ != state::created)
		return false;

	static constexpr XrDynamicObjectClassMETA classes[] = {XR_DYNAMIC_OBJECT_CLASS_KEYBOARD_META};

	XrDynamicObjectTrackedClassesSetInfoMETA info{
	        .type = XR_TYPE_DYNAMIC_OBJECT_TRACKED_CLASSES_SET_INFO_META,
	        .classCount = std::size(classes),
	        .classes = classes,
	};

	XrResult res = xrSetDynamicObjectTrackedClassesMETA(tracker, &info);
	if (XR_FAILED(res))
	{
		spdlog::warn("xrSetDynamicObjectTrackedClassesMETA failed: {}", (int)res);
		return false;
	}

	state_ = state::initializing;
	return true;
}

bool xr::dynamic_object_tracker::handle_event(const XrEventDataBaseHeader & header)
{
	// The META event types are not part of the Khronos XrStructureType enum: switch on the raw
	// value so GCC -Wswitch does not reject them
	switch (static_cast<uint32_t>(header.type))
	{
		case XR_TYPE_EVENT_DATA_DYNAMIC_OBJECT_TRACKER_CREATE_RESULT_META: {
			const auto & event = reinterpret_cast<const XrEventDataDynamicObjectTrackerCreateResultMETA &>(header);
			if (event.handle != tracker)
				return false;

			if (XR_FAILED(event.result))
			{
				spdlog::warn("dynamic object tracker creation failed: {}", (int)event.result);
				tracker = XR_NULL_HANDLE;
				state_ = state::empty;
			}
			else
			{
				spdlog::info("dynamic object tracker created");
				state_ = state::created;
			}
			return true;
		}
		case XR_TYPE_EVENT_DATA_DYNAMIC_OBJECT_SET_TRACKED_CLASSES_RESULT_META: {
			const auto & event = reinterpret_cast<const XrEventDataDynamicObjectSetTrackedClassesResultMETA &>(header);
			if (event.handle != tracker)
				return false;

			if (XR_FAILED(event.result))
			{
				spdlog::warn("dynamic object set tracked classes failed: {}", (int)event.result);
				state_ = state::created;
			}
			else
			{
				spdlog::info("dynamic object tracker is now tracking the keyboard class");
				state_ = state::initialized;
			}
			return true;
		}
		default:
			return false;
	}
}

bool xr::dynamic_object_tracker::get_dynamic_object_class(XrSpace space, XrDynamicObjectClassMETA & out) const
{
	if (not xrGetSpaceDynamicObjectDataMETA)
		return false;

	XrDynamicObjectDataMETA data{
	        .type = XR_TYPE_DYNAMIC_OBJECT_DATA_META,
	};

	if (XR_FAILED(xrGetSpaceDynamicObjectDataMETA(space, &data)))
		return false;

	out = data.classType;
	return true;
}
