/**********************************************************************
Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
MIT License
********************************************************************/

#include "GI12Registers.hlsli"
#include "GI12Common.hlsli"

// Hash Grid Cache UAVs
RWStructuredBuffer<uint>   g_HashGridCache_HashBuffer                    : register(u0);
RWStructuredBuffer<uint>   g_HashGridCache_DecayTileBuffer               : register(u1);
RWStructuredBuffer<uint2>  g_HashGridCache_ValueBuffer                   : register(u2);
RWStructuredBuffer<uint2>  g_HashGridCache_ValueIndirectBuffer           : register(u3);
RWStructuredBuffer<uint>   g_HashGridCache_UpdateTileBuffer              : register(u4);
RWStructuredBuffer<uint>   g_HashGridCache_UpdateTileCountBuffer         : register(u5);
RWStructuredBuffer<float4> g_HashGridCache_VisibilityBuffer              : register(u6);
RWStructuredBuffer<uint>   g_HashGridCache_VisibilityCountBuffer0        : register(u7);
RWStructuredBuffer<uint>   g_HashGridCache_VisibilityCountBuffer1        : register(u8);
RWStructuredBuffer<uint>   g_HashGridCache_VisibilityCellBuffer          : register(u9);
RWStructuredBuffer<uint2>  g_HashGridCache_MultibounceInfoBuffer         : register(u10);
RWStructuredBuffer<uint2>  g_ScreenProbes_ProbeSpawnRadianceBuffer       : register(u11);
RWStructuredBuffer<uint2>  g_ScreenProbes_ProbeSpawnSampleBuffer         : register(u12);
RWStructuredBuffer<uint>   g_ScreenProbes_ProbeSpawnBuffer               : register(u13);
RWStructuredBuffer<uint>   g_ScreenProbes_EmptyTileCountBuffer           : register(u14);

struct HashGridCache_Data
{
    float3 eye_position;
    float3 hit_position;
    float3 direction;
    float  hit_distance;
};

struct HashGridCache_Desc
{
    uint  bucket_index;
    uint  tile_hash;
    uint2 cell_offset;
};

HashGridCache_Desc HashGridCache_GetDesc(in HashGridCache_Data data)
{
    float cell_size_step = max(distance(data.eye_position, data.hit_position) * HashGridCacheCB.cell_size, HashGridCacheCB.min_cell_size);
    float log_step_multiplier = floor(log2(HASHGRIDCACHE_STEP_FACTOR * cell_size_step));
    float hit_cell_size = HASHGRIDCACHE_SIZE_FACTOR * exp2(log_step_multiplier);
    float hit_tile_size = hit_cell_size * HashGridCacheCB.tile_cell_ratio;

    float3 signed_c = floor(data.hit_position / hit_tile_size);
    float3 signed_d = floor(0.5f + (0.5f * data.direction + 0.5f) * 4.0f);

    uint l = uint(log_step_multiplier);
    uint3 c = asuint(int3(signed_c));
    uint3 d = asuint(int3(signed_d));
    uint t = uint(data.hit_distance < hit_tile_size);

    uint bucket_index = pcgHash(l +
                        pcgHash(c.x + pcgHash(c.y + pcgHash(c.z +
                        pcgHash(d.x + pcgHash(d.y + pcgHash(d.z +
                        pcgHash(t)))))))) % HashGridCacheCB.num_buckets;

    uint tile_hash = max(1,
                     xxHash(l +
                     xxHash(c.x + xxHash(c.y + xxHash(c.z +
                     xxHash(d.x + xxHash(d.y + xxHash(d.z +
                     xxHash(t)))))))));

    float3 e = floor(data.hit_position / hit_cell_size) - floor(data.hit_position / hit_tile_size) * HashGridCacheCB.tile_cell_ratio;
    uint2 cell_offset;
    float3 abs_direction = abs(data.direction);
    float max_direction = max(max(abs_direction.x, abs_direction.y), abs_direction.z);
    if (abs_direction.x == max_direction)
        cell_offset = uint2(e.y, e.z);
    else if (abs_direction.y == max_direction)
        cell_offset = uint2(e.x, e.z);
    else
        cell_offset = uint2(e.x, e.y);

    HashGridCache_Desc desc;
    desc.bucket_index = bucket_index;
    desc.tile_hash = tile_hash;
    desc.cell_offset = cell_offset;
    return desc;
}

uint HashGridCache_CellIndex(uint2 cell_offset, uint tile_index)
{
    return tile_index * HashGridCacheCB.num_cells_per_tile +
           HashGridCacheCB.first_cell_offset_tile_mip0 +
           cell_offset.x + cell_offset.y * HashGridCacheCB.size_tile_mip0;
}

uint HashGridCache_InsertCell(in HashGridCache_Data data, out uint tile_index, out bool is_new_tile)
{
    is_new_tile = false;
    tile_index = kGI1_InvalidId;
    HashGridCache_Desc desc = HashGridCache_GetDesc(data);

    for (uint bucket_offset = 0; bucket_offset < HashGridCacheCB.num_tiles_per_bucket; ++bucket_offset)
    {
        tile_index = bucket_offset + desc.bucket_index * HashGridCacheCB.num_tiles_per_bucket;
        uint previous_hash;
        InterlockedCompareExchange(g_HashGridCache_HashBuffer[tile_index], 0, desc.tile_hash, previous_hash);
        if (previous_hash == 0)
        {
            is_new_tile = true;
            break;
        }
        if (previous_hash == desc.tile_hash)
            break;
    }

    if (tile_index == kGI1_InvalidId)
        return kGI1_InvalidId;

    return HashGridCache_CellIndex(desc.cell_offset, tile_index);
}

uint HashGridCache_FindCell(in HashGridCache_Data data, out uint tile_index)
{
    tile_index = kGI1_InvalidId;
    HashGridCache_Desc desc = HashGridCache_GetDesc(data);

    for (uint bucket_offset = 0; bucket_offset < HashGridCacheCB.num_tiles_per_bucket; ++bucket_offset)
    {
        tile_index = bucket_offset + desc.bucket_index * HashGridCacheCB.num_tiles_per_bucket;
        uint previous_hash = g_HashGridCache_HashBuffer[tile_index];
        if (previous_hash == 0)
            return kGI1_InvalidId;
        if (previous_hash == desc.tile_hash)
            break;
    }

    return HashGridCache_CellIndex(desc.cell_offset, tile_index);
}

[numthreads(64, 1, 1)]
void PurgeTiles(uint did : SV_DispatchThreadID)
{
    if (did >= HashGridCacheCB.num_tiles)
        return;

    uint lastUsed = g_HashGridCache_DecayTileBuffer[did];
    if (g_HashGridCache_HashBuffer[did] != 0 && (GI12CB.frame_index - lastUsed) > kHashGridCache_TileDecay)
    {
        g_HashGridCache_HashBuffer[did] = 0;
        g_HashGridCache_DecayTileBuffer[did] = 0;
    }
}

float3 SampleSky(float3 dir)
{
    float3 d = dir;
    d.z = max(d.z, 0.0f);
    float r = sqrt(max(1.0f - d.z, 0.0f));
    float phi = atan2(d.y, d.x);
    float2 uv = float2(cos(phi), sin(phi)) * r * 0.5f + 0.5f;
    return SkyHemisphere.SampleLevel(LinearClampSampler, uv, 0.0f).rgb * Raytracing.Sky;
}

// Populate screen probe cells via RayQuery inline tracing
[numthreads(32, 1, 1)]
void PopulateScreenProbesCells(uint did : SV_DispatchThreadID)
{
    uint emptyCount = g_ScreenProbes_EmptyTileCountBuffer[0];
    uint totalRays = emptyCount * 64;
    if (did >= totalRays)
        return;

    uint probeIndex = did / 64;
    uint2 seed = ScreenProbes_UnpackSeed(g_ScreenProbes_ProbeSpawnBuffer[probeIndex]);
    float depth = Depth.Load(int3(seed, 0));
    if (depth >= 1.0f)
        return;

    float2 uv = (float2(seed) + 0.5f) / float2(GI12CB.buffer_dimensions);
    float4 clip = float4(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
    float4 world = mul(GI12CB.view_proj_inv, clip);
    float3 worldPos = world.xyz / world.w + GI12CB.eye;

    float3 normal = normalize(NormalRoughness.Load(int3(seed, 0)).xyz);
    float3 dir = ScreenProbes_UnpackSample(g_ScreenProbes_ProbeSpawnSampleBuffer[did]);

    // Ensure ray is in normal hemisphere
    if (dot(dir, normal) < 0.0f)
        dir = -dir;

    float farZ = max(GI12CB.near_far.y, 100000.0f);

    RayQuery<RAY_FLAG_NONE> rayQuery;
    RayDesc ray;
    ray.Origin = worldPos + normal * 0.5f;
    ray.Direction = dir;
    ray.TMin = 0.1f;
    ray.TMax = farZ;

    rayQuery.TraceRayInline(Scene, RAY_FLAG_NONE, 0xFF, ray);
    while (rayQuery.Proceed())
    {
        rayQuery.CommitNonOpaqueTriangleHit();
    }

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        float hitDist = rayQuery.CommittedRayT();
        float3 hitPos = ray.Origin + dir * hitDist;

        HashGridCache_Data data;
        data.eye_position = GI12CB.eye;
        data.hit_position = hitPos;
        data.direction = dir;
        data.hit_distance = hitDist;

        uint tileIndex;
        bool isNewTile;
        uint cellIndex = HashGridCache_InsertCell(data, tileIndex, isNewTile);

        if (cellIndex != kGI1_InvalidId)
        {
            g_HashGridCache_DecayTileBuffer[tileIndex] = GI12CB.frame_index;

            uint visIndex;
            InterlockedAdd(g_HashGridCache_VisibilityCountBuffer0[0], 1, visIndex);

            GI12Visibility vis;
            vis.is_front_face = rayQuery.CommittedTriangleFrontFace() ? 1 : 0;
            vis.instance_index = rayQuery.CommittedInstanceID();
            vis.geometry_index = rayQuery.CommittedGeometryIndex();
            vis.primitive_index = rayQuery.CommittedPrimitiveIndex();
            vis.barycentrics = rayQuery.CommittedTriangleBarycentrics();
            vis.pad = uint2(0, 0);

            g_HashGridCache_VisibilityBuffer[visIndex] = float4(
                asfloat(vis.instance_index | (vis.is_front_face ? 0 : 0x80000000u)),
                asfloat(vis.geometry_index),
                asfloat(vis.primitive_index),
                asfloat(packHalf2(vis.barycentrics))
            );
            g_HashGridCache_VisibilityCellBuffer[visIndex] = cellIndex;
        }

        float3 hitRadiance = float3(0.0f, 0.0f, 0.0f);

        // Directional Sun/Moon light with shadow ray
        float3 sunDir = normalize(Raytracing.DirectionalLight.Direction);
        float sunNdotL = max(0.0f, dot(-dir, sunDir));
        if (sunNdotL > 0.0f && (Raytracing.DirectionalLight.Flags & LightFlags::Active))
        {
            RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> shadowQuery;
            RayDesc shadowRay;
            shadowRay.Origin = hitPos + (-dir) * 0.5f;
            shadowRay.Direction = sunDir;
            shadowRay.TMin = 0.1f;
            shadowRay.TMax = 10000.0f;
            shadowQuery.TraceRayInline(Scene, RAY_FLAG_NONE, 0xFF, shadowRay);
            shadowQuery.Proceed();
            if (shadowQuery.CommittedStatus() == COMMITTED_NOTHING)
            {
                hitRadiance += Raytracing.DirectionalLight.Color.rgb * (sunNdotL * Raytracing.Directional * (1.0f / PI));
            }
        }

        // Local active lights
        uint numLights = min(Raytracing.NumLights, 16u);
        for (uint li = 0; li < numLights; ++li)
        {
            Light light = Lights[li];
            if (!(light.Flags & LightFlags::Active))
                continue;

            float3 toLight = light.Position.xyz - hitPos;
            float distSq = dot(toLight, toLight);
            if (distSq > light.Radius * light.Radius)
                continue;

            float dist = sqrt(distSq);
            float3 L = toLight / max(dist, 1e-4f);
            float atten = saturate(1.0f - dist * light.InvRadius);
            atten *= atten;

            float NdotL = max(0.0f, dot(-dir, L));
            if (NdotL > 0.0f)
            {
                hitRadiance += light.Color.rgb * (atten * NdotL * Raytracing.Point * (1.0f / PI));
            }
        }

        // Sky ambient bounce
        float3 sky = SampleSky(float3(0.0f, 0.0f, 1.0f));
        hitRadiance += sky * 0.15f;

        // Update cell with hit radiance
        if (cellIndex != kGI1_InvalidId)
        {
            float4 currentRad = HashGridCache_UnpackRadiance(g_HashGridCache_ValueBuffer[cellIndex]);
            float n = min(currentRad.w + 1.0f, HashGridCacheCB.max_sample_count);
            float3 newRad = lerp(currentRad.xyz, hitRadiance, 1.0f / n);
            g_HashGridCache_ValueBuffer[cellIndex] = HashGridCache_PackRadiance(float4(newRad, n));
        }

        g_ScreenProbes_ProbeSpawnRadianceBuffer[did] = ScreenProbes_PackRadiance(float4(hitRadiance, hitDist));
    }
    else
    {
        // Hit sky
        float3 sky = SampleSky(dir);
        g_ScreenProbes_ProbeSpawnRadianceBuffer[did] = ScreenProbes_PackRadiance(float4(sky, farZ));
    }
}

// Multi-bounce cell population (secondary bounce from first-bounce hits)
[numthreads(32, 1, 1)]
void PopulateMultibounceCells(uint did : SV_DispatchThreadID)
{
    if (GI12CB.use_multibounce == 0)
        return;

    uint count = g_HashGridCache_VisibilityCountBuffer0[0];
    if (did >= count)
        return;

    float4 packedVis = g_HashGridCache_VisibilityBuffer[did];
    uint cellIndex = g_HashGridCache_VisibilityCellBuffer[did];

    float2 bary = unpackHalf2(asuint(packedVis.w));
    uint instanceID = asuint(packedVis.x) & 0x7FFFFFFFu;
    uint primID = asuint(packedVis.z);

    // Random bounce direction using Halton
    float2 u = CalculateHaltonSequence(did + GI12CB.frame_index * 128);
    float z = 1.0f - 2.0f * u.x;
    float r = sqrt(saturate(1.0f - z * z));
    float phi = TWO_PI * u.y;
    float3 bounceDir = float3(r * cos(phi), r * sin(phi), z);

    // Fetch cell radiance from previous frame
    float4 prevRad = HashGridCache_UnpackRadiance(g_HashGridCache_ValueIndirectBuffer[cellIndex]);
    float3 bounceOrigin = GI12CB.eye + bounceDir * 1.0f; // approximate origin

    RayQuery<RAY_FLAG_NONE> rayQuery;
    RayDesc ray;
    ray.Origin = bounceOrigin;
    ray.Direction = bounceDir;
    ray.TMin = 0.05f;
    ray.TMax = 1000.0f;

    rayQuery.TraceRayInline(Scene, RAY_FLAG_NONE, 0xFF, ray);
    while (rayQuery.Proceed())
    {
        rayQuery.CommitNonOpaqueTriangleHit();
    }

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        float hitDist = rayQuery.CommittedRayT();
        float3 hitPos = ray.Origin + bounceDir * hitDist;

        HashGridCache_Data data;
        data.eye_position = GI12CB.eye;
        data.hit_position = hitPos;
        data.direction = bounceDir;
        data.hit_distance = hitDist;

        uint tileIndex;
        bool isNewTile;
        uint bounceCellIndex = HashGridCache_InsertCell(data, tileIndex, isNewTile);

        if (bounceCellIndex != kGI1_InvalidId)
        {
            uint mbIndex;
            InterlockedAdd(g_HashGridCache_VisibilityCountBuffer1[0], 1, mbIndex);
            g_HashGridCache_MultibounceInfoBuffer[mbIndex] = uint2(cellIndex, bounceCellIndex);
        }
    }
}

// Update tiles and aggregate radiance
[numthreads(64, 1, 1)]
void UpdateTiles(uint did : SV_DispatchThreadID)
{
    // Radiance is accumulated directly in PopulateScreenProbesCells
    return;
}

// Chain multibounce cells into first-bounce cells
[numthreads(32, 1, 1)]
void UpdateMultibounceCells(uint did : SV_DispatchThreadID)
{
    if (GI12CB.use_multibounce == 0)
        return;

    uint count = g_HashGridCache_VisibilityCountBuffer1[0];
    if (did >= count)
        return;

    uint2 pair = g_HashGridCache_MultibounceInfoBuffer[did];
    uint primaryCell = pair.x;
    uint secondaryCell = pair.y;

    float4 secRad = HashGridCache_UnpackRadiance(g_HashGridCache_ValueBuffer[secondaryCell]);
    float4 primRad = HashGridCache_UnpackRadiance(g_HashGridCache_ValueIndirectBuffer[primaryCell]);

    float n = min(primRad.w + 1.0f, HashGridCacheCB.max_multibounce_sample_count);
    float3 combined = lerp(primRad.xyz, secRad.xyz * 0.7f, 1.0f / n);

    g_HashGridCache_ValueIndirectBuffer[primaryCell] = HashGridCache_PackRadiance(float4(combined, n));
}

// Transfer cell radiance back into screen probes
[numthreads(64, 1, 1)]
void ResolveCells(uint did : SV_DispatchThreadID)
{
    uint emptyCount = g_ScreenProbes_EmptyTileCountBuffer[0];
    uint totalRays = emptyCount * 64;
    if (did >= totalRays)
        return;

    float4 spawnRad = ScreenProbes_UnpackRadiance(g_ScreenProbes_ProbeSpawnRadianceBuffer[did]);
    float hitDist = spawnRad.w;
    float farZ = max(GI12CB.near_far.y, 100000.0f);
    if (hitDist < (farZ - 1.0f))
    {
        uint probeIndex = did / 64;
        uint2 seed = ScreenProbes_UnpackSeed(g_ScreenProbes_ProbeSpawnBuffer[probeIndex]);
        float depth = Depth.Load(int3(seed, 0));
        float2 uv = (float2(seed) + 0.5f) / float2(GI12CB.buffer_dimensions);
        float4 clip = float4(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
        float4 world = mul(GI12CB.view_proj_inv, clip);
        float3 worldPos = world.xyz / world.w + GI12CB.eye;

        float3 dir = ScreenProbes_UnpackSample(g_ScreenProbes_ProbeSpawnSampleBuffer[did]);
        float3 hitPos = worldPos + dir * hitDist;

        HashGridCache_Data data;
        data.eye_position = GI12CB.eye;
        data.hit_position = hitPos;
        data.direction = dir;
        data.hit_distance = hitDist;

        uint tileIndex;
        uint cellIndex = HashGridCache_FindCell(data, tileIndex);

        if (cellIndex != kGI1_InvalidId)
        {
            float4 direct = HashGridCache_UnpackRadiance(g_HashGridCache_ValueBuffer[cellIndex]);
            float4 indirect = HashGridCache_UnpackRadiance(g_HashGridCache_ValueIndirectBuffer[cellIndex]);
            float3 cacheRadiance = direct.xyz + (GI12CB.use_multibounce ? indirect.xyz : float3(0.0f, 0.0f, 0.0f));
            float3 totalRadiance = lerp(spawnRad.xyz, cacheRadiance, saturate(direct.w / 16.0f));

            g_ScreenProbes_ProbeSpawnRadianceBuffer[did] = ScreenProbes_PackRadiance(float4(totalRadiance, hitDist));
        }
    }
}
