#pragma once

/// Everything Ernie offers, in one include: the scene and its tensors, the GPU layer, the
/// rasterizer and its render delegate. Include the individual headers instead to keep a
/// translation unit's dependencies narrow, for example `ernie/scene/scene.h` alone needs no Vulkan.

// Core: images, rasters, geometry, cameras, materials; no GPU.
#include "ernie/camera/orbit_camera.h"
#include "ernie/core/image.h"
#include "ernie/core/raster_view.h"
#include "ernie/core/shader_source.h"
#include "ernie/geometry/frustum.h"
#include "ernie/geometry/lod_quadtree.h"
#include "ernie/geometry/mesh_data.h"
#include "ernie/geometry/primitives.h"
#include "ernie/material/brdf.h"

// Scene: USD-style prims, values, schema tokens and helpers, and DLPack tensors.
#include "ernie/interop/tensor.h"
#include "ernie/scene/path.h"
#include "ernie/scene/scene.h"
#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"
#include "ernie/scene/value.h"

// GPU: the Vulkan device, memory, pipelines, the Slang compiler, presentation and CUDA interop.
#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/gpu_image.h"
#include "ernie/gpu/offscreen_target.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/shader_compiler.h"
#include "ernie/gpu/swapchain.h"
#include "ernie/gpu/vulkan_context.h"
#include "ernie/interop/cuda_interop.h"

// Rasterizer: the renderer, its parts and materials, and the scene render delegate.
#include "ernie/raster/ground_part.h"
#include "ernie/raster/mesh_part.h"
#include "ernie/raster/poisson_disk.h"
#include "ernie/raster/raster_delegate.h"
#include "ernie/raster/raster_material.h"
#include "ernie/raster/raster_part.h"
#include "ernie/raster/raster_renderer.h"
#include "ernie/raster/shadow_cascades.h"
#include "ernie/raster/terrain_part.h"
