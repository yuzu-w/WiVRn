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

#include <functional>
#include <unordered_set>
#include <vector>
#include <openxr/openxr.h>

namespace xr
{
class instance;
class session;

// Minimal wrapper around XR_FB_scene / XR_FB_spatial_entity(_query/_storage):
// just enough to query spatial anchors filtered by a component type, enable a
// component on one of them, and read its 3D bounding box. Not a general
// purpose spatial anchor API, only what keyboard_passthrough needs.
class spatial_anchor
{
	XrSession sess = XR_NULL_HANDLE;

	PFN_xrQuerySpacesFB xrQuerySpacesFB{};
	PFN_xrRetrieveSpaceQueryResultsFB xrRetrieveSpaceQueryResultsFB{};
	PFN_xrEnumerateSpaceSupportedComponentsFB xrEnumerateSpaceSupportedComponentsFB{};
	PFN_xrSetSpaceComponentStatusFB xrSetSpaceComponentStatusFB{};
	PFN_xrGetSpaceComponentStatusFB xrGetSpaceComponentStatusFB{};
	PFN_xrGetSpaceBoundingBox3DFB xrGetSpaceBoundingBox3DFB{};

	// only one query in flight at a time, like the FB sample
	XrAsyncRequestIdFB query_request_id = 0;
	std::unordered_set<XrAsyncRequestIdFB> pending_component_requests;
	std::vector<XrSpaceQueryResultFB> last_query_results;

public:
	spatial_anchor() = default;
	spatial_anchor(instance &, session &);

	bool supported() const
	{
		return xrQuerySpacesFB != nullptr;
	}

	// false if a query is already pending
	bool query_spaces_with_component(XrSpaceComponentTypeFB component);
	bool query_pending() const
	{
		return query_request_id != 0;
	}

	bool request_set_component_status(XrSpace space, XrSpaceComponentTypeFB type, bool enabled, XrAsyncRequestIdFB & out_request_id);
	bool request_pending(XrAsyncRequestIdFB) const;

	// forward every polled XR event here; returns true if it was consumed
	bool handle_event(const XrEventDataBaseHeader &);

	bool is_component_supported(XrSpace space, XrSpaceComponentTypeFB type) const;
	// diagnostics: every component type the runtime reports for this space
	std::vector<XrSpaceComponentTypeFB> list_supported_components(XrSpace space) const;
	bool get_component_status(XrSpace space, XrSpaceComponentTypeFB type, bool & out_enabled) const;
	bool get_bounding_box_3d(XrSpace space, XrRect3DfFB & out) const;

	// spaces from the last completed query matching filter
	std::vector<XrSpace> find_spaces(const std::function<bool(XrSpace)> & filter) const;
};
} // namespace xr
