/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "openxr/openxr.h"
#include "utils/handle.h"
#include <variant>

namespace xr
{
class instance;
class session;

class passthrough_layer_fb : public utils::handle<XrPassthroughLayerFB>
{
public:
	passthrough_layer_fb(instance &, session &, const XrPassthroughLayerCreateInfoFB &);
};

// The FB passthrough feature object itself (XrPassthroughFB). The runtime
// only allows a single one of these per session, so it is owned by
// xr::session (session::ensure_shared_passthrough_object()) and shared by
// every passthrough_*_fb class below, each of which only owns its own layer.
//
// Must only be started (start()) once at least one layer exists on it: the
// runtime rejects xrCreatePassthroughLayerFB on an already-started object
// that has no layer yet, so start() is deferred and each passthrough_*_fb
// class calls it (idempotent) right after creating its own layer, via
// session::start_shared_passthrough_object().
class passthrough_object_fb : public utils::handle<XrPassthroughFB>
{
	PFN_xrPassthroughStartFB xrPassthroughStartFB{};
	bool started = false;

public:
	passthrough_object_fb(instance &, session &);

	void start();
};

class passthrough_fb
{
	PFN_xrPassthroughLayerPauseFB xrPassthroughLayerPauseFB{};
	PFN_xrPassthroughLayerResumeFB xrPassthroughLayerResumeFB{};

	passthrough_layer_fb passthrough_layer;
	XrCompositionLayerPassthroughFB composition_layer;

public:
	passthrough_fb(instance &, session &, XrPassthroughFB);

	void start();
	void pause();
	XrCompositionLayerBaseHeader * layer()
	{
		return (XrCompositionLayerBaseHeader *)&composition_layer;
	}
};

class passthrough_htc : public utils::handle<XrPassthroughHTC>
{
	XrCompositionLayerPassthroughHTC composition_layer;

public:
	passthrough_htc(instance &, session &);
	XrCompositionLayerBaseHeader * layer()
	{
		return (XrCompositionLayerBaseHeader *)&composition_layer;
	}
};

class passthrough_alpha_blend
{
};

using passthrough = std::variant<std::monostate, passthrough_fb, passthrough_htc, passthrough_alpha_blend>;

// A passthrough layer with a manually placed hole (geometry_instance_fb
// below), used to punch through only where the tracked keyboard is,
// independently of the background passthrough handled by the "passthrough"
// variant above: it is an overlay on top of an otherwise fully opaque VR
// scene, not a background replacement, and can be toggled on its own. Shares
// the session's single XrPassthroughFB object (see passthrough_object_fb).
class passthrough_projected_fb
{
	passthrough_layer_fb passthrough_layer;
	XrCompositionLayerPassthroughFB composition_layer;

public:
	passthrough_projected_fb(instance &, session &, XrPassthroughFB);

	XrPassthroughLayerFB layer_handle() const
	{
		return passthrough_layer;
	}

	XrCompositionLayerBaseHeader * layer()
	{
		return (XrCompositionLayerBaseHeader *)&composition_layer;
	}
};

// A single quad-shaped passthrough hole (XR_FB_triangle_mesh + geometry
// instance) inside a passthrough_projected_fb layer. Reused frame to frame,
// only its pose/scale are updated via set_transform().
class geometry_instance_fb
{
	utils::handle<XrTriangleMeshFB> mesh;
	utils::handle<XrGeometryInstanceFB> instance_;
	PFN_xrGeometryInstanceSetTransformFB xrGeometryInstanceSetTransformFB{};
	XrGeometryInstanceTransformFB transform{
	        .type = XR_TYPE_GEOMETRY_INSTANCE_TRANSFORM_FB,
	};

public:
	// no default constructor: utils::handle has none with a runtime deleter,
	// and this class is only ever used through std::optional::emplace()
	geometry_instance_fb(instance &, session &, XrPassthroughLayerFB layer, XrSpace base_space);

	void set_transform(XrTime time, const XrPosef & pose, const XrVector3f & scale);

	operator bool() const
	{
		return (bool)instance_;
	}
};

} // namespace xr
