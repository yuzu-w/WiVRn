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

#include "passthrough.h"
#include "openxr/openxr.h"
#include "session.h"
#include "xr/check.h"
#include "xr/instance.h"
#include <iterator>

xr::passthrough_layer_fb::passthrough_layer_fb(instance & inst, session & s, const XrPassthroughLayerCreateInfoFB & info) :
        handle(inst.get_proc<PFN_xrDestroyPassthroughLayerFB>("xrDestroyPassthroughLayerFB"))
{
	PFN_xrCreatePassthroughLayerFB xrCreatePassthroughLayerFB = inst.get_proc<PFN_xrCreatePassthroughLayerFB>("xrCreatePassthroughLayerFB");
	CHECK_XR(xrCreatePassthroughLayerFB(s, &info, &id));
}

// XrPassthroughFB creation, shared by passthrough_object_fb, and by
// create_passthrough_fb() below for the same reason.
static XrPassthroughFB create_passthrough_fb(xr::instance & inst, xr::session & s)
{
	PFN_xrCreatePassthroughFB xrCreatePassthroughFB = inst.get_proc<PFN_xrCreatePassthroughFB>("xrCreatePassthroughFB");

	XrPassthroughFB id;
	XrPassthroughCreateInfoFB info{
	        .type = XR_TYPE_PASSTHROUGH_CREATE_INFO_FB,
	        .flags = 0,
	};

	CHECK_XR(xrCreatePassthroughFB(s, &info, &id));
	return id;
}

xr::passthrough_object_fb::passthrough_object_fb(instance & inst, session & s) :
        handle(create_passthrough_fb(inst, s), inst.get_proc<PFN_xrDestroyPassthroughFB>("xrDestroyPassthroughFB"))
{
	xrPassthroughStartFB = inst.get_proc<PFN_xrPassthroughStartFB>("xrPassthroughStartFB");
}

void xr::passthrough_object_fb::start()
{
	if (started)
		return;
	started = true;
	CHECK_XR(xrPassthroughStartFB(id));
}

xr::passthrough_fb::passthrough_fb(instance & inst, session & s, XrPassthroughFB passthrough) :
        passthrough_layer(inst, s, XrPassthroughLayerCreateInfoFB{
                                           .type = XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB,
                                           .passthrough = passthrough,
                                           .flags = 0,
                                           .purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB,
                                   })
{
	xrPassthroughLayerPauseFB = inst.get_proc<PFN_xrPassthroughLayerPauseFB>("xrPassthroughLayerPauseFB");
	xrPassthroughLayerResumeFB = inst.get_proc<PFN_xrPassthroughLayerResumeFB>("xrPassthroughLayerResumeFB");

	composition_layer = XrCompositionLayerPassthroughFB{
	        .type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB,
	        .flags = 0,
	        .space = XR_NULL_HANDLE,
	        .layerHandle = passthrough_layer,
	};

	// the shared passthrough object must not be started before this layer
	// exists, see passthrough_object_fb
	s.start_shared_passthrough_object();
	start();
}

void xr::passthrough_fb::start()
{
	CHECK_XR(xrPassthroughLayerResumeFB(passthrough_layer));
}

void xr::passthrough_fb::pause()
{
	CHECK_XR(xrPassthroughLayerPauseFB(passthrough_layer));
}

xr::passthrough_projected_fb::passthrough_projected_fb(instance & inst, session & s, XrPassthroughFB passthrough) :
        passthrough_layer(inst, s, XrPassthroughLayerCreateInfoFB{
                                           .type = XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB,
                                           .passthrough = passthrough,
                                           .flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB,
                                           .purpose = XR_PASSTHROUGH_LAYER_PURPOSE_PROJECTED_FB,
                                   })
{
	auto xrPassthroughLayerSetStyleFB = inst.get_proc<PFN_xrPassthroughLayerSetStyleFB>("xrPassthroughLayerSetStyleFB");

	// surface-projected passthrough is transparent outside the geometry
	// instance(s) via its alpha channel, but the runtime ignores that alpha
	// (treating the whole layer as opaque) unless this flag is set -
	// without it the layer blacks out the entire view, not just the hole
	composition_layer = XrCompositionLayerPassthroughFB{
	        .type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB,
	        .flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
	        .space = XR_NULL_HANDLE,
	        .layerHandle = passthrough_layer,
	};

	// fully opaque, no tinting: this layer only shows through the geometry
	// instance hole(s) added to it, not as a general background
	XrPassthroughStyleFB style{
	        .type = XR_TYPE_PASSTHROUGH_STYLE_FB,
	        .textureOpacityFactor = 1,
	        .edgeColor = {0, 0, 0, 0},
	};
	CHECK_XR(xrPassthroughLayerSetStyleFB(passthrough_layer, &style));

	// the shared passthrough object must not be started before this layer
	// exists, see passthrough_object_fb; this layer has
	// XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB so it needs no resume call
	s.start_shared_passthrough_object();
}

// a flat unit quad, actual size/pose come from set_transform() every frame
static XrTriangleMeshFB create_quad_mesh(xr::instance & inst, xr::session & s)
{
	auto xrCreateTriangleMeshFB = inst.get_proc<PFN_xrCreateTriangleMeshFB>("xrCreateTriangleMeshFB");

	static constexpr XrVector3f vertices[] = {
	        {-0.5f, -0.5f, 0.0f},
	        {-0.5f, 0.5f, 0.0f},
	        {0.5f, 0.5f, 0.0f},
	        {0.5f, -0.5f, 0.0f},
	};
	static constexpr uint32_t indices[] = {0, 1, 2, 2, 3, 0};

	XrTriangleMeshCreateInfoFB mesh_info{
	        .type = XR_TYPE_TRIANGLE_MESH_CREATE_INFO_FB,
	        .flags = 0,
	        .windingOrder = XR_WINDING_ORDER_UNKNOWN_FB,
	        .vertexCount = std::size(vertices),
	        .vertexBuffer = vertices,
	        .triangleCount = std::size(indices) / 3,
	        .indexBuffer = indices,
	};

	XrTriangleMeshFB mesh_id;
	CHECK_XR(xrCreateTriangleMeshFB(s, &mesh_info, &mesh_id));
	return mesh_id;
}

static XrGeometryInstanceFB create_geometry_instance(xr::instance & inst, xr::session & s, XrPassthroughLayerFB layer, XrTriangleMeshFB mesh, XrSpace base_space)
{
	auto xrCreateGeometryInstanceFB = inst.get_proc<PFN_xrCreateGeometryInstanceFB>("xrCreateGeometryInstanceFB");

	XrGeometryInstanceCreateInfoFB geometry_info{
	        .type = XR_TYPE_GEOMETRY_INSTANCE_CREATE_INFO_FB,
	        .layer = layer,
	        .mesh = mesh,
	        .baseSpace = base_space,
	        .pose = {{0, 0, 0, 1}, {0, 0, 0}},
	        .scale = {1, 1, 1},
	};

	XrGeometryInstanceFB instance_id;
	CHECK_XR(xrCreateGeometryInstanceFB(s, &geometry_info, &instance_id));
	return instance_id;
}

xr::geometry_instance_fb::geometry_instance_fb(instance & inst, session & s, XrPassthroughLayerFB layer, XrSpace base_space) :
        mesh(create_quad_mesh(inst, s), inst.get_proc<PFN_xrDestroyTriangleMeshFB>("xrDestroyTriangleMeshFB")),
        instance_(create_geometry_instance(inst, s, layer, mesh, base_space), inst.get_proc<PFN_xrDestroyGeometryInstanceFB>("xrDestroyGeometryInstanceFB"))
{
	xrGeometryInstanceSetTransformFB = inst.get_proc<PFN_xrGeometryInstanceSetTransformFB>("xrGeometryInstanceSetTransformFB");

	transform.baseSpace = base_space;
	transform.pose = {{0, 0, 0, 1}, {0, 0, 0}};
	transform.scale = {1, 1, 1};
}

void xr::geometry_instance_fb::set_transform(XrTime time, const XrPosef & pose, const XrVector3f & scale)
{
	if (not instance_)
		return;

	transform.time = time;
	transform.pose = pose;
	transform.scale = scale;
	CHECK_XR(xrGeometryInstanceSetTransformFB(instance_, &transform));
}

xr::passthrough_htc::passthrough_htc(instance & inst, session & s) :
        handle(inst.get_proc<PFN_xrDestroyPassthroughHTC>("xrDestroyPassthroughHTC"))
{
	PFN_xrCreatePassthroughHTC xrCreatePassthroughHTC = inst.get_proc<PFN_xrCreatePassthroughHTC>("xrCreatePassthroughHTC");

	XrPassthroughCreateInfoHTC info{
	        .type = XR_TYPE_PASSTHROUGH_CREATE_INFO_HTC,
	        .form = XR_PASSTHROUGH_FORM_PLANAR_HTC,
	};

	CHECK_XR(xrCreatePassthroughHTC(s, &info, &id));

	composition_layer = XrCompositionLayerPassthroughHTC{
	        .type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_HTC,
	        .layerFlags = 0,
	        .space = XR_NULL_HANDLE,
	        .passthrough = id,
	        .color = {
	                .type = XR_TYPE_PASSTHROUGH_COLOR_HTC,
	                .alpha = 1,
	        },
	};
}
