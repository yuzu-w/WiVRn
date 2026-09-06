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

#include "spatial_anchor.h"

#include "xr/instance.h"
#include "xr/session.h"
#include <algorithm>
#include <spdlog/spdlog.h>

xr::spatial_anchor::spatial_anchor(instance & inst, session & session)
{
	if (not(inst.has_extension(XR_FB_SCENE_EXTENSION_NAME) and
	        inst.has_extension(XR_FB_SPATIAL_ENTITY_EXTENSION_NAME) and
	        inst.has_extension(XR_FB_SPATIAL_ENTITY_QUERY_EXTENSION_NAME) and
	        inst.has_extension(XR_FB_SPATIAL_ENTITY_STORAGE_EXTENSION_NAME)))
		return;

	sess = session;
	xrQuerySpacesFB = inst.get_proc<PFN_xrQuerySpacesFB>("xrQuerySpacesFB");
	xrRetrieveSpaceQueryResultsFB = inst.get_proc<PFN_xrRetrieveSpaceQueryResultsFB>("xrRetrieveSpaceQueryResultsFB");
	xrEnumerateSpaceSupportedComponentsFB = inst.get_proc<PFN_xrEnumerateSpaceSupportedComponentsFB>("xrEnumerateSpaceSupportedComponentsFB");
	xrSetSpaceComponentStatusFB = inst.get_proc<PFN_xrSetSpaceComponentStatusFB>("xrSetSpaceComponentStatusFB");
	xrGetSpaceComponentStatusFB = inst.get_proc<PFN_xrGetSpaceComponentStatusFB>("xrGetSpaceComponentStatusFB");
	xrGetSpaceBoundingBox3DFB = inst.get_proc<PFN_xrGetSpaceBoundingBox3DFB>("xrGetSpaceBoundingBox3DFB");
}

bool xr::spatial_anchor::query_spaces_with_component(XrSpaceComponentTypeFB component)
{
	if (not supported() or query_request_id != 0)
		return false;

	XrSpaceStorageLocationFilterInfoFB storage_filter{
	        .type = XR_TYPE_SPACE_STORAGE_LOCATION_FILTER_INFO_FB,
	        .location = XR_SPACE_STORAGE_LOCATION_LOCAL_FB,
	};

	XrSpaceComponentFilterInfoFB component_filter{
	        .type = XR_TYPE_SPACE_COMPONENT_FILTER_INFO_FB,
	        .next = &storage_filter,
	        .componentType = component,
	};

	XrSpaceQueryInfoFB query_info{
	        .type = XR_TYPE_SPACE_QUERY_INFO_FB,
	        .queryAction = XR_SPACE_QUERY_ACTION_LOAD_FB,
	        .maxResultCount = 100,
	        .timeout = XR_NO_DURATION,
	        .filter = reinterpret_cast<XrSpaceFilterInfoBaseHeaderFB *>(&component_filter),
	};

	XrResult res = xrQuerySpacesFB(sess, reinterpret_cast<XrSpaceQueryInfoBaseHeaderFB *>(&query_info), &query_request_id);
	if (XR_FAILED(res))
	{
		spdlog::warn("xrQuerySpacesFB failed: {}", (int)res);
		return false;
	}

	return true;
}

bool xr::spatial_anchor::request_set_component_status(XrSpace space, XrSpaceComponentTypeFB type, bool enabled, XrAsyncRequestIdFB & out_request_id)
{
	if (not supported())
		return false;

	if (not is_component_supported(space, type))
	{
		spdlog::warn("request_set_component_status: component {} is not supported by this space", (int)type);
		return false;
	}

	XrSpaceComponentStatusSetInfoFB info{
	        .type = XR_TYPE_SPACE_COMPONENT_STATUS_SET_INFO_FB,
	        .componentType = type,
	        .enabled = enabled,
	        .timeout = XR_NO_DURATION,
	};

	XrAsyncRequestIdFB request_id;
	XrResult res = xrSetSpaceComponentStatusFB(space, &info, &request_id);
	if (XR_FAILED(res))
	{
		spdlog::warn("xrSetSpaceComponentStatusFB failed: {}", (int)res);
		return false;
	}

	pending_component_requests.insert(request_id);
	out_request_id = request_id;
	spdlog::info("xrSetSpaceComponentStatusFB issued: component {} -> {}, request id {}", (int)type, enabled, (uint64_t)request_id);
	return true;
}

bool xr::spatial_anchor::request_pending(XrAsyncRequestIdFB request_id) const
{
	// 0 means "never issued" (request_set_component_status() only ever
	// writes a real, non-zero id into locatable_request_id on success), so
	// this correctly returns false for it without any special-casing
	return pending_component_requests.contains(request_id);
}

bool xr::spatial_anchor::handle_event(const XrEventDataBaseHeader & header)
{
	switch (header.type)
	{
		case XR_TYPE_EVENT_DATA_SPACE_QUERY_RESULTS_AVAILABLE_FB: {
			const auto & event = reinterpret_cast<const XrEventDataSpaceQueryResultsAvailableFB &>(header);
			if (event.requestId != query_request_id)
				return false;

			XrSpaceQueryResultsFB results{
			        .type = XR_TYPE_SPACE_QUERY_RESULTS_FB,
			};
			XrResult res = xrRetrieveSpaceQueryResultsFB(sess, event.requestId, &results);
			if (XR_FAILED(res))
			{
				spdlog::warn("xrRetrieveSpaceQueryResultsFB (count) failed: {}", (int)res);
				return true;
			}

			last_query_results.clear();
			last_query_results.resize(results.resultCountOutput);
			results.resultCapacityInput = last_query_results.size();
			results.resultCountOutput = 0;
			results.results = last_query_results.data();

			res = xrRetrieveSpaceQueryResultsFB(sess, event.requestId, &results);
			if (XR_FAILED(res))
			{
				spdlog::warn("xrRetrieveSpaceQueryResultsFB failed: {}", (int)res);
				last_query_results.clear();
			}

			return true;
		}
		case XR_TYPE_EVENT_DATA_SPACE_QUERY_COMPLETE_FB: {
			const auto & event = reinterpret_cast<const XrEventDataSpaceQueryCompleteFB &>(header);
			if (event.requestId != query_request_id)
				return false;

			query_request_id = 0;
			return true;
		}
		case XR_TYPE_EVENT_DATA_SPACE_SET_STATUS_COMPLETE_FB: {
			const auto & event = reinterpret_cast<const XrEventDataSpaceSetStatusCompleteFB &>(header);
			spdlog::info("XR_TYPE_EVENT_DATA_SPACE_SET_STATUS_COMPLETE_FB: request id {}, component {} -> {}, result {} (pending: {})",
			             (uint64_t)event.requestId,
			             (int)event.componentType,
			             (bool)event.enabled,
			             (int)event.result,
			             pending_component_requests.contains(event.requestId));

			if (not pending_component_requests.contains(event.requestId))
				return false;

			pending_component_requests.erase(event.requestId);
			return true;
		}
		default:
			return false;
	}
}

bool xr::spatial_anchor::is_component_supported(XrSpace space, XrSpaceComponentTypeFB type) const
{
	auto components = list_supported_components(space);
	return std::ranges::find(components, type) != components.end();
}

std::vector<XrSpaceComponentTypeFB> xr::spatial_anchor::list_supported_components(XrSpace space) const
{
	uint32_t count = 0;
	XrResult res = xrEnumerateSpaceSupportedComponentsFB(space, 0, &count, nullptr);
	if (XR_FAILED(res))
	{
		spdlog::warn("xrEnumerateSpaceSupportedComponentsFB (count) failed: {}", (int)res);
		return {};
	}

	std::vector<XrSpaceComponentTypeFB> components(count);
	res = xrEnumerateSpaceSupportedComponentsFB(space, count, &count, components.data());
	if (XR_FAILED(res))
	{
		spdlog::warn("xrEnumerateSpaceSupportedComponentsFB failed: {}", (int)res);
		return {};
	}

	return components;
}

bool xr::spatial_anchor::get_component_status(XrSpace space, XrSpaceComponentTypeFB type, bool & out_enabled) const
{
	if (not is_component_supported(space, type))
		return false;

	XrSpaceComponentStatusFB status{
	        .type = XR_TYPE_SPACE_COMPONENT_STATUS_FB,
	};

	if (XR_FAILED(xrGetSpaceComponentStatusFB(space, type, &status)))
		return false;

	out_enabled = status.enabled and not status.changePending;
	return true;
}

bool xr::spatial_anchor::get_bounding_box_3d(XrSpace space, XrRect3DfFB & out) const
{
	if (not is_component_supported(space, XR_SPACE_COMPONENT_TYPE_BOUNDED_3D_FB))
		return false;

	return XR_SUCCEEDED(xrGetSpaceBoundingBox3DFB(sess, space, &out));
}

std::vector<XrSpace> xr::spatial_anchor::find_spaces(const std::function<bool(XrSpace)> & filter) const
{
	std::vector<XrSpace> result;
	for (const auto & anchor: last_query_results)
	{
		if (not filter or filter(anchor.space))
			result.push_back(anchor.space);
	}
	return result;
}
