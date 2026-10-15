/**********************************************************************
Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
MIT License
********************************************************************/

#ifndef GI12DATA_HLSLI
#define GI12DATA_HLSLI

#include "Interop.h"

INTEROP_STRUCT(GI12Constants, 16)
{
    float4x4 view_proj;
    float4x4 view_proj_prev;
    float4x4 view_proj_inv;
    float4x4 view_proj_inv_prev;
    float4x4 reprojection;
    float3   eye;
    uint     frame_index;
    float3   eye_prev;
    uint     use_direct_lighting;
    uint2    buffer_dimensions;
    float2   near_far;
    uint     use_multibounce;
    uint     use_temporal_feedback;
    uint     pad0;
    uint     pad1;
};
VALIDATE_CBUFFER(GI12Constants, 16);

INTEROP_STRUCT(ScreenProbesConstants, 16)
{
    float cell_size;
    uint  probe_size;
    uint2 probe_count;
    uint  probe_mask_mip_count;
    uint  probe_spawn_tile_size;
    uint  debug_mode;
    uint  pad0;
};
VALIDATE_CBUFFER(ScreenProbesConstants, 16);

INTEROP_STRUCT(HashGridCacheConstants, 16)
{
    float cell_size;
    float min_cell_size;
    float tile_size;
    float tile_cell_ratio;
    uint  num_buckets;
    uint  num_tiles;
    uint  num_cells;
    uint  num_tiles_per_bucket;
    uint  size_tile_mip0;
    uint  size_tile_mip1;
    uint  size_tile_mip2;
    uint  size_tile_mip3;
    uint  num_cells_per_tile_mip0;
    uint  num_cells_per_tile_mip1;
    uint  num_cells_per_tile_mip2;
    uint  num_cells_per_tile_mip3;
    uint  num_cells_per_tile;
    uint  first_cell_offset_tile_mip0;
    uint  first_cell_offset_tile_mip1;
    uint  first_cell_offset_tile_mip2;
    uint  first_cell_offset_tile_mip3;
    uint  buffer_ping_pong;
    float max_sample_count;
    float discard_multibounce_ray_probability;
    float max_multibounce_sample_count;
    uint  debug_mip_level;
    uint  debug_propagate;
    uint  debug_max_cell_decay;
    uint  debug_bucket_occupancy_histogram_size;
    uint  debug_bucket_overflow_histogram_size;
    uint  debug_mode;
    uint  pad0;
};
VALIDATE_CBUFFER(HashGridCacheConstants, 16);

INTEROP_STRUCT(WorldSpaceReSTIRConstants, 16)
{
    float cell_size;
    uint  num_cells;
    uint  num_entries_per_cell;
    uint  pad0;
};
VALIDATE_CBUFFER(WorldSpaceReSTIRConstants, 16);

INTEROP_STRUCT(GlossyReflectionsConstants, 16)
{
    uint2 full_res;
    int   full_radius;
    int   half_radius;
    int   mark_fireflies_half_radius;
    int   mark_fireflies_full_radius;
    float mark_fireflies_half_low_threshold;
    float mark_fireflies_full_low_threshold;
    float mark_fireflies_half_high_threshold;
    float mark_fireflies_full_high_threshold;
    int   cleanup_fireflies_half_radius;
    int   cleanup_fireflies_full_radius;
    float low_roughness_threshold;
    float high_roughness_threshold;
    uint  half_res;
    uint  pad0;
};
VALIDATE_CBUFFER(GlossyReflectionsConstants, 16);

INTEROP_STRUCT(GlossyReflectionsAtrousConstants, 16)
{
    int ping_pong;
    int full_step;
    int pass_index;
    int pad0;
};
VALIDATE_CBUFFER(GlossyReflectionsAtrousConstants, 16);

INTEROP_STRUCT(GIDenoiserConstants, 16)
{
    int blur_direction_x;
    int blur_direction_y;
    int pass_index;
    int pad0;
};
VALIDATE_CBUFFER(GIDenoiserConstants, 16);

INTEROP_STRUCT(GI12Visibility, 16)
{
    uint   is_front_face;
    uint   instance_index;
    uint   geometry_index;
    uint   primitive_index;
    float2 barycentrics;
    uint2  pad;
};
VALIDATE_CBUFFER(GI12Visibility, 16);

#endif // GI12DATA_HLSLI
