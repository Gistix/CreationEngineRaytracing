#ifndef GI12CACHE_HLSLI
#define GI12CACHE_HLSLI

// AMD Capsaicin GI-1.2 L2 hash-grid lookup + L1 screen-probe SH helpers,
// adapted to Creation Engine bindings. Source: src/core/src/render_techniques/gi1/
// hash_grid_cache.hlsl + screen_probes.hlsl (MIT, AMD).
// Quantization follows source: screen-probe reuse kGI1_FloatQuantize = 1e4,
// hash-grid cells kHashGridCache_FloatQuantize = 1e3 (UpdateCellValue scratch
// merged by UpdateTiles to avoid integer overflow). SH stored half-packed
// (packHalf4) per source, not integer-scaled.

#include "interop/GI12Data.hlsli"
#include "raytracing/include/Materials/Microfacet.hlsli"

static const float GI12_QUANT_INV = 1e-4f;
static const float GI12_HASH_QUANT = 1e3f;
static const float GI12_HASH_QUANT_INV = 1e-3f;
static const float GI12_PI = 3.14159265f;

// Exact ports of Capsaicin packHalf/unpackHalf + HashGridCache pack/unpack
// (math/pack.hlsl, hash_grid_cache.hlsl, MIT). Value buffers store
// premultiplied average radiance with the sample count in w.
uint2 GI12_PackHalf4(float4 value)
{
    return uint2(f32tof16(value.x) | (f32tof16(value.y) << 16),
        f32tof16(value.z) | (f32tof16(value.w) << 16));
}

float4 GI12_UnpackHalf4(uint2 packedValue)
{
    return float4(f16tof32(packedValue.x & 0xFFFFu), f16tof32(packedValue.x >> 16),
        f16tof32(packedValue.y & 0xFFFFu), f16tof32(packedValue.y >> 16));
}

uint2 GI12_PackValue(float4 radiance)
{
    return GI12_PackHalf4(radiance);
}

float4 GI12_UnpackValue(uint2 packedRadiance)
{
    return GI12_UnpackHalf4(packedRadiance);
}

// Exact port of Capsaicin HashGridCache_QuantizeRadiance: 1e3 scale, w = 1
// per sample. xyz accumulate only for nonzero lighting; w always accumulates.
uint4 GI12_QuantizeCellRadiance(float3 radiance)
{
    return uint4(uint(round(GI12_HASH_QUANT * radiance.x)),
        uint(round(GI12_HASH_QUANT * radiance.y)),
        uint(round(GI12_HASH_QUANT * radiance.z)), 1u);
}

float4 GI12_RecoverCellRadiance(uint4 quantizedRadiance)
{
    return float4(quantizedRadiance.x * GI12_HASH_QUANT_INV,
        quantizedRadiance.y * GI12_HASH_QUANT_INV,
        quantizedRadiance.z * GI12_HASH_QUANT_INV,
        quantizedRadiance.w);
}

uint GI12_Hash(uint3 v)
{
    uint h = v.x * 73856093u ^ v.y * 19349663u ^ v.z * 83492791u;
    return h;
}

// Removes TAA jitter (pixels) from a screen UV, mirroring the GI pass
// dynamicUVUnjittered. Without this, probe seeding, SH inputs and resolve
// lookups crawl ±1px per frame and shimmer while standing still.
float2 GI12_UnjitterUV(float2 uv, float2 jitter, uint2 screenSize)
{
    return uv - jitter / max(float2(screenSize), float2(1.0f, 1.0f));
}

uint GI12_Rand(inout uint s)
{
    s = s * 1664525u + 1013904223u;
    return s;
}

float GI12_Rand01(inout uint s)
{
    return (GI12_Rand(s) >> 8) * (1.0f / 16777216.0f);
}

float3 GI12_CosineSample(float2 u, float3 n)
{
    const float r = sqrt(u.x);
    const float phi = 6.2831853f * u.y;
    float3 t, b;
    CreateOrthonormalBasis(n, t, b);
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0f - u.x, 0.0f)));
}

float3 GI12_HemiOctahedron(float2 uv)
{
    const float2 e = uv * 2.0f - 1.0f;
    const float2 p = float2(e.x + e.y, e.x - e.y) * 0.5f;
    return normalize(float3(p, 1.0f - abs(p.x) - abs(p.y)));
}

float2 GI12_HemiOctahedronInverse(float3 direction)
{
    const float3 d = normalize(direction);
    const float2 p = d.xy / max(abs(d.x) + abs(d.y) + d.z, 1e-6f);
    return saturate(float2(p.x + p.y, p.x - p.y) * 0.5f + 0.5f);
}

// Inverse solid-angle density for uniform sampling in hemi-octahedral UV.
float GI12_HemiOctahedronInversePdf(float2 uv)
{
    const float2 e = uv * 2.0f - 1.0f;
    const float2 p = float2(e.x + e.y, e.x - e.y) * 0.5f;
    const float3 unnormalized = float3(p, 1.0f - abs(p.x) - abs(p.y));
    return 2.0f * rsqrt(dot(unnormalized, unnormalized)) /
        dot(unnormalized, unnormalized);
}

float3 GI12_ProbeRayDirection(float2 uv, float3 normal)
{
    const float3 localDirection = GI12_HemiOctahedron(uv);
    float3 tangent, bitangent;
    CreateOrthonormalBasis(normal, tangent, bitangent);
    return normalize(localDirection.x * tangent + localDirection.y * bitangent + localDirection.z * normal);
}

float GI12_SpecularSamplingProbability(float3 albedo, float metalness, float3 normal, float3 viewDirection)
{
    const float3 f0 = saturate(lerp(0.04f.xxx, albedo, metalness));
    const float f0Luminance = dot(f0, float3(0.2126f, 0.7152f, 0.0722f));
    const float diffuseLuminance = dot(albedo * (1.0f - metalness), float3(0.2126f, 0.7152f, 0.0722f));
    const float fresnel = saturate(f0Luminance + (1.0f - f0Luminance) *
        pow(1.0f - saturate(dot(normal, viewDirection)), 5.0f));
    const float specularWeight = fresnel;
    const float diffuseWeight = diffuseLuminance * (1.0f - fresnel);
    return clamp(specularWeight / max(specularWeight + diffuseWeight, 1e-4f), 0.1f, 0.9f);
}

float3 GI12_SampleGGXReflection(float roughness, float3 normal, float3 viewDirection, float2 u)
{
    float3 tangent, bitangent;
    CreateOrthonormalBasis(normal, tangent, bitangent);
    const float3 localView = float3(dot(viewDirection, tangent), dot(viewDirection, bitangent), dot(viewDirection, normal));
    const float alpha = max(roughness * roughness, 1e-3f);
    const float3 halfVector = sampleGGX_VNDF(alpha, localView, u);
    const float3 localDirection = reflect(-localView, halfVector);
    return normalize(localDirection.x * tangent + localDirection.y * bitangent + localDirection.z * normal);
}

float GI12_GGXReflectionPdf(float roughness, float3 normal, float3 viewDirection, float3 direction)
{
    float3 tangent, bitangent;
    CreateOrthonormalBasis(normal, tangent, bitangent);
    const float3 localView = float3(dot(viewDirection, tangent), dot(viewDirection, bitangent), dot(viewDirection, normal));
    const float3 localDirection = float3(dot(direction, tangent), dot(direction, bitangent), dot(direction, normal));
    if (localView.z <= 0.0f || localDirection.z <= 0.0f)
        return 0.0f;
    const float3 halfVector = normalize(localView + localDirection);
    return evalPdfGGX_VNDF(max(roughness * roughness, 1e-3f), localView, halfVector);
}

uint GI12_ProbeCellForDirection(float3 direction, float3 normal, uint probeSize)
{
    float3 tangent, bitangent;
    CreateOrthonormalBasis(normal, tangent, bitangent);
    const float3 localDirection = float3(dot(direction, tangent), dot(direction, bitangent), dot(direction, normal));
    const float2 uv = GI12_HemiOctahedronInverse(localDirection);
    const uint2 cell = min((uint2)(uv * float(probeSize)), probeSize - 1u);
    return cell.y * probeSize + cell.x;
}

// SH basis + clamped-cosine convolution below follow Capsaicin
// src/core/src/math/spherical_harmonics.hlsl (MIT, AMD).

void GI12_SHBasis(in float3 dir, out float sh[9])
{
    float fC0, fC1, fS0, fS1, fTmpA, fTmpB, fTmpC;
    float pz2 = dir.z * dir.z;
    sh[0] = 0.2820947917738781f;
    sh[2] = 0.4886025119029199f * dir.z;
    sh[6] = 0.9461746957575601f * pz2 + -0.3153915652525201f;
    fC0 = dir.x;
    fS0 = dir.y;
    fTmpA = -0.48860251190292f;
    sh[3] = fTmpA * fC0;
    sh[1] = fTmpA * fS0;
    fTmpB = -1.092548430592079f * dir.z;
    sh[7] = fTmpB * fC0;
    sh[5] = fTmpB * fS0;
    fC1 = dir.x * fC0 - dir.y * fS0;
    fS1 = dir.x * fS0 + dir.y * fC0;
    fTmpC = 0.5462742152960395f;
    sh[8] = fTmpC * fC1;
    sh[4] = fTmpC * fS1;
}

// Clamped-cosine lobe convolved with the SH basis: dot(irradianceSH, these)
// evaluates diffuse irradiance for the lobe direction.
void GI12_SHClampedCosine(in float3 dir, out float sh[9])
{
    const float A0 = GI12_PI;
    const float A1 = (2.0f * GI12_PI) / 3.0f;
    const float A2 = GI12_PI / 4.0f;
    float fC0, fC1, fS0, fS1, fTmpA, fTmpB, fTmpC;
    float pz2 = dir.z * dir.z;
    sh[0] = 0.2820947917738781f * A0;
    sh[2] = 0.4886025119029199f * dir.z * A1;
    sh[6] = 0.7431238683011271f * pz2 + -0.2477079561003757f;
    fC0 = dir.x;
    fS0 = dir.y;
    fTmpA = -0.48860251190292f;
    sh[3] = fTmpA * fC0 * A1;
    sh[1] = fTmpA * fS0 * A1;
    fTmpB = -1.092548430592079f * dir.z;
    sh[7] = fTmpB * fC0 * A2;
    sh[5] = fTmpB * fS0 * A2;
    fC1 = dir.x * fC0 - dir.y * fS0;
    fS1 = dir.x * fS0 + dir.y * fC0;
    fTmpC = 0.5462742152960395f;
    sh[8] = fTmpC * fC1 * A2;
    sh[4] = fTmpC * fS1 * A2;
}

// Clamped-cosine-cone lobe (bent cone for AO): same source as above.
void GI12_SHClampedCosineCone(in float3 dir, in float thetaMax, out float sh[9])
{
    float sinThetaMax;
    float cosThetaMax;
    sincos(thetaMax, sinThetaMax, cosThetaMax);
    float sin2 = sinThetaMax * sinThetaMax;
    float sin3 = sin2 * sinThetaMax;
    float cos3 = cosThetaMax * cosThetaMax * cosThetaMax;

    float band1 = 1.023326707946489f * (1.0f - cos3);
    float band2 = (4.0f - 3.0f * sin3) * sin2;

    sh[0] = 0.886226925452758f * sin2;

    sh[1] = -band1 * dir.y;
    sh[2] = +band1 * dir.z;
    sh[3] = -band1 * dir.x;

    sh[4] = +0.8580855308097834f * band2 * dir.x * dir.y;
    sh[5] = -0.8580855308097834f * band2 * dir.y * dir.z;
    sh[6] = +0.2477079561003757f * band2 * (3.0f * dir.z * dir.z - 1.0f);
    sh[7] = -0.8580855308097834f * band2 * dir.x * dir.z;
    sh[8] = +0.4290427654048917f * band2 * (dir.x * dir.x - dir.y * dir.y);
}

// Diffuse irradiance with bent-cone AO: ao=1 fully open, ao=0 fully closed.
float3 GI12_SHEvaluateBentCone(float3 normal, float ao, float4 probeSH[9])
{
    float thetaMax = acos(sqrt(saturate(1.0f - ao)));
    float lobe[9];
    GI12_SHClampedCosineCone(normal, thetaMax, lobe);
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < 9; ++i)
        irradiance += probeSH[i].rgb * lobe[i];
    return irradiance / max(probeSH[0].w, 1.0f);
}

// Diffuse irradiance from per-probe SH (9x float4 rgb + weight in w).
float3 GI12_SHEvaluate(float3 normal, float4 probeSH[9])
{
    float lobe[9];
    GI12_SHClampedCosine(normal, lobe);
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < 9; ++i)
        irradiance += probeSH[i].rgb * lobe[i];
    return irradiance / max(probeSH[0].w, 1.0f);
}

uint GI12_CellLevel(GI12HashGridData grid, float3 position, float3 eye)
{
    const float requestedCellSize = max(distance(eye, position) * grid.CellSize, grid.MinCellSize);
    return (uint)clamp(floor(log2(max(1000.0f * requestedCellSize, 1.0f))), 0.0f, 31.0f);
}

float GI12_CellSize(GI12HashGridData grid, float3 position, float3 eye, uint mipLevel)
{
    return exp2(float(GI12_CellLevel(grid, position, eye) + mipLevel)) * 1e-3f;
}

// Exact port of Capsaicin src/core/src/math/hash.hlsl (MIT, AMD).
uint GI12_PcgHash(uint value)
{
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint GI12_XxHash(uint value)
{
    const uint prime32_2 = 2246822519u, prime32_3 = 3266489917u;
    const uint prime32_4 = 668265263u, prime32_5 = 374761393u;
    uint ret = value + prime32_5;
    ret = prime32_4 * ((ret << 17) | (ret >> (32 - 17)));
    ret = prime32_2 * (ret ^ (ret >> 15));
    ret = prime32_3 * (ret ^ (ret >> 13));
    return ret ^ (ret >> 16);
}

// Exact port of Capsaicin HashGridCache_GetDesc (hash_grid_cache.hlsl, MIT).
// tileCoord = floor(position / tileSize); direction bins d = floor(0.5 + (0.5*dir+0.5)*4);
// bucket = pcgHash chain % NumBuckets; hashTag = max(1, xxHash chain).
// cellLevel l = floor(log2(1e3 * max(dist*CellSize, MinCellSize))).
void GI12_GetTileKey(
    GI12HashGridData grid,
    float3 position,
    float3 direction,
    float3 eye,
    float hitDistance,
    out int3 tileCoord,
    out uint bucket,
    out uint hashTag)
{
    const float cellSizeStep = max(distance(eye, position) * grid.CellSize, grid.MinCellSize);
    const float logStep = floor(log2(max(1000.0f * cellSizeStep, 1.0f)));
    const uint l = (uint)max(logStep, 0.0f);
    const float baseCellSize = exp2(logStep) * 1e-3f;
    const float tileSize = baseCellSize * grid.TileCellRatio;
    tileCoord = (int3)floor(position / tileSize);
    const float3 signedD = floor(0.5f + (0.5f * direction + 0.5f) * 4.0f);
    const uint3 c = asuint(tileCoord);
    const uint3 d = asuint(int3(signedD));
    const uint t = (hitDistance < tileSize) ? 1u : 0u;
    bucket = GI12_PcgHash(l +
        GI12_PcgHash(c.x + GI12_PcgHash(c.y + GI12_PcgHash(c.z +
        GI12_PcgHash(d.x + GI12_PcgHash(d.y + GI12_PcgHash(d.z +
        GI12_PcgHash(t)))))))) % max(grid.NumBuckets, 1u);
    hashTag = max(1u,
        GI12_XxHash(l +
        GI12_XxHash(c.x + GI12_XxHash(c.y + GI12_XxHash(c.z +
        GI12_XxHash(d.x + GI12_XxHash(d.y + GI12_XxHash(d.z +
        GI12_XxHash(t)))))))));
}

uint GI12_TileCellIndex(GI12HashGridData grid, int3 tileCoord, float3 position, float3 direction,
    float3 eye, uint tileIndex, uint mipLevel)
{
    // Exact port of Capsaicin HashGridCache_GetDesc cell-offset + HashGridCache_CellIndex.
    const float cellSize = GI12_CellSize(grid, position, eye, 0u);
    const float tileSize = cellSize * grid.TileCellRatio;
    const float3 cellInTileF = floor(position / cellSize) - floor(position / tileSize) * grid.TileCellRatio;
    const uint3 cellInTile = (uint3)clamp(cellInTileF, 0.0f, grid.TileCellRatio - 1.0f);
    const float3 absDirection = abs(direction);
    const float maxDirection = max(max(absDirection.x, absDirection.y), absDirection.z);
    uint2 cellOffset;
    if (absDirection.x == maxDirection)
        cellOffset = cellInTile.yz;
    else if (absDirection.y == maxDirection)
        cellOffset = cellInTile.xz;
    else
        cellOffset = cellInTile.xy;

    const uint firstOffset[4] = {
        grid.FirstCellOffsetTileMip0, grid.FirstCellOffsetTileMip1,
        grid.FirstCellOffsetTileMip2, grid.FirstCellOffsetTileMip3 };
    const uint mipSize = max(grid.SizeTileMip0 >> mipLevel, 1u);
    const uint2 mipOffset = cellOffset >> mipLevel;
    return tileIndex * grid.NumCellsPerTile + firstOffset[mipLevel & 3u] +
        mipOffset.x + mipOffset.y * mipSize;
}

static const uint GI12_INVALID_ID = 0xFFFFFFFFu;
static const uint GI12_TILE_DECAY = 50u;

// Exact port of Capsaicin HashGridCache_CellIndex (hash_grid_cache.hlsl, MIT).
uint GI12_CellIndex(GI12HashGridData grid, uint cellOffsetMip0, uint tileIndex)
{
    return tileIndex * grid.NumCellsPerTile + grid.FirstCellOffsetTileMip0 + cellOffsetMip0;
}

uint GI12_CellIndexMip(GI12HashGridData grid, uint2 cellOffsetMip0, uint tileIndex, uint mipLevel)
{
    const uint firstOffset[4] = {
        grid.FirstCellOffsetTileMip0, grid.FirstCellOffsetTileMip1,
        grid.FirstCellOffsetTileMip2, grid.FirstCellOffsetTileMip3 };
    const uint mipSize = max(grid.SizeTileMip0 >> (mipLevel & 3u), 1u);
    const uint2 mipOffset = cellOffsetMip0 >> (mipLevel & 3u);
    return tileIndex * grid.NumCellsPerTile + firstOffset[mipLevel & 3u] +
        mipOffset.x + mipOffset.y * mipSize;
}

// Exact port of Capsaicin HashGridCache_InsertCell: transient per-frame hash,
// InterlockedCompareExchange(0 -> tileHash). No eviction; full bucket returns
// INVALID (overflow counted by caller). isNewTile signals packed-list append
// + mip0 clear by the caller (cf. PopulateScreenProbesHandleHit).
uint GI12_InsertCell(
    GI12HashGridData grid,
    RWStructuredBuffer<uint> hashTable,
    float3 position,
    float3 direction,
    float3 eye,
    float hitDistance,
    out uint tileIndex,
    out bool isNewTile)
{
    tileIndex = GI12_INVALID_ID;
    isNewTile = false;
    if (grid.NumTiles == 0u || grid.NumTilesPerBucket == 0u)
        return GI12_INVALID_ID;
    int3 tileCoord;
    uint bucket, hashTag;
    GI12_GetTileKey(grid, position, direction, eye, hitDistance, tileCoord, bucket, hashTag);
    const float cellSize = GI12_CellSize(grid, position, eye, 0u);
    const float tileSize = cellSize * grid.TileCellRatio;
    const float3 cellInTileF = floor(position / cellSize) - floor(position / tileSize) * grid.TileCellRatio;
    const uint3 cellInTile = (uint3)clamp(cellInTileF, 0.0f, grid.TileCellRatio - 1.0f);
    const float3 absDirection = abs(direction);
    const float maxDirection = max(max(absDirection.x, absDirection.y), absDirection.z);
    uint2 cellOffset;
    if (absDirection.x == maxDirection)
        cellOffset = cellInTile.yz;
    else if (absDirection.y == maxDirection)
        cellOffset = cellInTile.xz;
    else
        cellOffset = cellInTile.xy;
    uint bucketOffset = 0u;
    [loop]
    for (; bucketOffset < grid.NumTilesPerBucket; ++bucketOffset) {
        uint previousHash;
        tileIndex = bucketOffset + bucket * grid.NumTilesPerBucket;
        InterlockedCompareExchange(hashTable[tileIndex], 0u, hashTag, previousHash);
        if (previousHash == 0u) {
            isNewTile = true;
            break;
        }
        if (previousHash == hashTag)
            break;
    }
    if (bucketOffset >= grid.NumTilesPerBucket)
        return GI12_INVALID_ID;
    return GI12_CellIndexMip(grid, cellOffset, tileIndex, 0u);
}

// Exact port of Capsaicin HashGridCache_FindCell.
uint GI12_FindCell(
    GI12HashGridData grid,
    StructuredBuffer<uint> hashTable,
    float3 position,
    float3 direction,
    float3 eye,
    float hitDistance)
{
    if (grid.NumTiles == 0u || grid.NumTilesPerBucket == 0u)
        return GI12_INVALID_ID;
    int3 tileCoord;
    uint bucket, hashTag;
    GI12_GetTileKey(grid, position, direction, eye, hitDistance, tileCoord, bucket, hashTag);
    const float cellSize = GI12_CellSize(grid, position, eye, 0u);
    const float tileSize = cellSize * grid.TileCellRatio;
    const float3 cellInTileF = floor(position / cellSize) - floor(position / tileSize) * grid.TileCellRatio;
    const uint3 cellInTile = (uint3)clamp(cellInTileF, 0.0f, grid.TileCellRatio - 1.0f);
    const float3 absDirection = abs(direction);
    const float maxDirection = max(max(absDirection.x, absDirection.y), absDirection.z);
    uint2 cellOffset;
    if (absDirection.x == maxDirection)
        cellOffset = cellInTile.yz;
    else if (absDirection.y == maxDirection)
        cellOffset = cellInTile.xz;
    else
        cellOffset = cellInTile.xy;
    [loop]
    for (uint o = 0u; o < grid.NumTilesPerBucket; ++o) {
        const uint slot = o + bucket * grid.NumTilesPerBucket;
        const uint h = hashTable[slot];
        if (h == 0u)
            return GI12_INVALID_ID;
        if (h == hashTag)
            return GI12_CellIndexMip(grid, cellOffset, slot, 0u);
    }
    return GI12_INVALID_ID;
}

uint2 GI12_CellOffsetMip0(GI12HashGridData grid, float3 position, float3 direction, float3 eye, uint tileIndex)
{
    const float cellSize = GI12_CellSize(grid, position, eye, 0u);
    const float tileSize = cellSize * grid.TileCellRatio;
    const float3 cellInTileF = floor(position / cellSize) - floor(position / tileSize) * grid.TileCellRatio;
    const uint3 cellInTile = (uint3)clamp(cellInTileF, 0.0f, grid.TileCellRatio - 1.0f);
    const float3 absDirection = abs(direction);
    const float maxDirection = max(max(absDirection.x, absDirection.y), absDirection.z);
    if (absDirection.x == maxDirection)
        return cellInTile.yz;
    if (absDirection.y == maxDirection)
        return cellInTile.xz;
    return cellInTile.xy;
}

// Mip-aware find for the transient uint hash table (Capsaicin CellIndex per mip).
uint GI12_FindCellMip(
    GI12HashGridData grid,
    StructuredBuffer<uint> hashTable,
    float3 pos, float3 direction, float3 eye, float hitDistance, uint mipLevel)
{
    if (grid.NumTiles == 0u || grid.NumTilesPerBucket == 0u)
        return GI12_INVALID_ID;
    int3 tileCoord;
    uint bucket, hashTag;
    GI12_GetTileKey(grid, pos, direction, eye, hitDistance, tileCoord, bucket, hashTag);
    const float cellSize = GI12_CellSize(grid, pos, eye, 0u);
    const float tileSize = cellSize * grid.TileCellRatio;
    const float3 cellInTileF = floor(pos / cellSize) - floor(pos / tileSize) * grid.TileCellRatio;
    const uint3 cellInTile = (uint3)clamp(cellInTileF, 0.0f, grid.TileCellRatio - 1.0f);
    const float3 absDirection = abs(direction);
    const float maxDirection = max(max(absDirection.x, absDirection.y), absDirection.z);
    uint2 cellOffset;
    if (absDirection.x == maxDirection)
        cellOffset = cellInTile.yz;
    else if (absDirection.y == maxDirection)
        cellOffset = cellInTile.xz;
    else
        cellOffset = cellInTile.xy;
    [loop]
    for (uint o = 0u; o < grid.NumTilesPerBucket; ++o) {
        const uint slot = o + bucket * grid.NumTilesPerBucket;
        if (hashTable[slot] == 0u)
            return GI12_INVALID_ID;
        if (hashTable[slot] == hashTag)
            return GI12_CellIndexMip(grid, cellOffset, slot, mipLevel);
    }
    return GI12_INVALID_ID;
}

// Exact port of Capsaicin HashGridCache_CellOffsetMip0: linear mip0 cell
// index back to (tile, 2D offset). Mirrors CellIndexMip layout.
uint GI12_TileOfCell(GI12HashGridData grid, uint cellIndexMip0, out uint2 cellOffsetMip0)
{
    const uint tileIndex = cellIndexMip0 / max(grid.NumCellsPerTile, 1u);
    uint intra = cellIndexMip0 % max(grid.NumCellsPerTile, 1u);
    intra -= min(intra, grid.FirstCellOffsetTileMip0);
    const uint mipSize = max(grid.SizeTileMip0, 1u);
    cellOffsetMip0.y = intra / mipSize;
    cellOffsetMip0.x = intra - cellOffsetMip0.y * mipSize;
    return tileIndex;
}

uint2 GI12_CellOffsetMip0FromCell(uint cellIndexMip0, GI12HashGridData grid)
{
    uint2 cellOffset;
    GI12_TileOfCell(grid, cellIndexMip0, cellOffset);
    return cellOffset;
}

// Exact ports of Capsaicin HashGridCache_FilteredRadianceDirect/Indirect:
// walk mips 0-3, falling through while the sample count is below the
// (multi)bounce cap. Returns premultiplied average; caller divides rgb by w.
void GI12_FilteredRadianceDirect(GI12HashGridData grid, StructuredBuffer<uint2> valueBuffer,
    uint cellIndexMip0, out float4 directRadiance)
{
    const uint2 cellOffset = GI12_CellOffsetMip0FromCell(cellIndexMip0, grid);
    const uint tileIndex = cellIndexMip0 / max(grid.NumCellsPerTile, 1u);
    directRadiance = GI12_UnpackValue(valueBuffer[cellIndexMip0]);
    [unroll]
    for (uint mip = 1u; mip < 4u; ++mip) {
        const uint mipSize = max(grid.SizeTileMip0 >> mip, 1u);
        if (mipSize <= 0u)
            break;
        const bool useMip = directRadiance.w < grid.MaxSampleCount;
        if (!useMip)
            break;
        const uint cell = GI12_CellIndexMip(grid, cellOffset, tileIndex, mip);
        if (cell >= grid.NumCells)
            break;
        directRadiance = GI12_UnpackValue(valueBuffer[cell]);
    }
}

void GI12_FilteredRadianceIndirect(GI12HashGridData grid, StructuredBuffer<uint2> valueIndirectBuffer,
    uint cellIndexMip0, out float4 indirectRadiance)
{
    const uint2 cellOffset = GI12_CellOffsetMip0FromCell(cellIndexMip0, grid);
    const uint tileIndex = cellIndexMip0 / max(grid.NumCellsPerTile, 1u);
    indirectRadiance = GI12_UnpackValue(valueIndirectBuffer[cellIndexMip0]);
    [unroll]
    for (uint mip = 1u; mip < 4u; ++mip) {
        const uint mipSize = max(grid.SizeTileMip0 >> mip, 1u);
        if (mipSize <= 0u)
            break;
        const bool useMip = indirectRadiance.w < grid.MaxMultibounceSampleCount;
        if (!useMip)
            break;
        const uint cell = GI12_CellIndexMip(grid, cellOffset, tileIndex, mip);
        if (cell >= grid.NumCells)
            break;
        indirectRadiance = GI12_UnpackValue(valueIndirectBuffer[cell]);
    }
}

bool GI12_GetCachedRadiance(
    GI12HashGridData grid,
    StructuredBuffer<uint> hashTable,
    StructuredBuffer<uint4> cellRadiance,
    StructuredBuffer<uint2> cellData,
    float3 position,
    float3 direction,
    float3 eye,
    float3 normal,
    float hitDistance,
    float roughness,
    out float3 radiance,
    out uint cellIndex)
{
    radiance = float3(0.0f, 0.0f, 0.0f);
    cellIndex = grid.NumCells;
    if (grid.NumCells == 0 || grid.NumBuckets == 0)
        return false;

    // Mirror SHaRC validity: skip voxels too close to the query point.
    const float baseCellSize = GI12_CellSize(grid, position, eye, 0u);
    if (hitDistance < baseCellSize * 1.7320508f)
        return false;

    [unroll]
    for (uint mipLevel = 0u; mipLevel < 4u; ++mipLevel)
    {
        const uint cell = GI12_FindCellMip(grid, hashTable, position, direction, eye, hitDistance, mipLevel);
        if (cell >= grid.NumCells)
            continue;

        const uint2 data = cellData[cell];
        const uint sampleCount = data.x;
        if (sampleCount < 4u)
            continue;

        const float voxelSize = GI12_CellSize(grid, position, eye, mipLevel);
        const float cellRoughness = float(data.y) / (max(float(sampleCount), 1.0f) * 1000.0f);
        const float queryRoughness = max(roughness, cellRoughness);
        const float r2 = min(queryRoughness, 0.99f);
        const float a2 = r2 * r2 * r2 * r2;
        const float footprint = hitDistance * sqrt(0.5f * a2 / max(1.0f - a2, 1e-4f));
        if (footprint < voxelSize && queryRoughness > 0.02f)
            continue;

        const uint4 q = cellRadiance[cell];
        const float3 sum = float3(q.xyz) * GI12_QUANT_INV;
        if (dot(sum, sum) <= 0.0f)
            continue;

        radiance = sum / max(float(sampleCount), 1.0f);
        cellIndex = cell;
        return true;
    }
    return false;
}

#endif
