// GI-1.2 UpdateTiles: exact port of Capsaicin gi1.comp UpdateTiles (MIT, AMD).
//
// Merges per-frame quantized scratch (UpdateCellValue*, 1e3 scale, w = sample
// count) into the persistent half-packed Value* buffers with a capped running
// mean (MaxSampleCount / MaxMultibounceSampleCount), then box-filters mips
// 1-3 from LDS and clears the scratch. One 8x8 group covers each tile in the
// per-frame UpdateTileBuffer list; groups past UpdateTileCount exit early.

#include "interop/CameraData.hlsli"
#include "interop/GI12Data.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<GI12Data> GI12Params : register(b1);
ConstantBuffer<GI12ScreenProbesData> ScreenProbes : register(b2);
ConstantBuffer<GI12HashGridData> HashGrid : register(b3);

#include "include/Common.hlsli"
#include "raytracing/include/GI12Cache.hlsli"

RWStructuredBuffer<uint> TileUpdate : register(u6);
RWStructuredBuffer<uint> TileUpdateCount : register(u7);
RWStructuredBuffer<uint2> Value : register(u10);
RWStructuredBuffer<uint2> ValueIndirect : register(u11);
RWStructuredBuffer<uint> UpdateCellValue : register(u12);
RWStructuredBuffer<uint> UpdateCellValueIndirect : register(u13);

#define GI12_UPDATE_TILES_GROUP_X 8
#define GI12_UPDATE_TILES_GROUP_Y 8

groupshared uint2 GI12_UpdateTilesLDS[GI12_UPDATE_TILES_GROUP_X][GI12_UPDATE_TILES_GROUP_Y];
#ifdef GI12_MULTIBOUNCE
groupshared uint2 GI12_UpdateTilesLDSIndirect[GI12_UPDATE_TILES_GROUP_X][GI12_UPDATE_TILES_GROUP_Y];
#endif

[numthreads(GI12_UPDATE_TILES_GROUP_X, GI12_UPDATE_TILES_GROUP_Y, 1)]
void Main(uint groupId : SV_GroupID, uint2 groupThreadId : SV_GroupThreadID)
{
    // A group covers 1 tile (8x8 mip0) up to 64 tiles (1x1), matching source.
    const uint2 subgroupId = groupThreadId / max(HashGrid.SizeTileMip0, 1u);
    const uint2 subgroupThreadId = groupThreadId % max(HashGrid.SizeTileMip0, 1u);
    const uint2 subgroupSize = uint2(GI12_UPDATE_TILES_GROUP_X, GI12_UPDATE_TILES_GROUP_Y) /
        max(HashGrid.SizeTileMip0, 1u);
    const uint updateTileIndex = groupId * subgroupSize.x * subgroupSize.y +
        subgroupId.y * subgroupSize.x + subgroupId.x;

    if (updateTileIndex >= TileUpdateCount[0])
        return;

    const uint tileIndex = TileUpdate[updateTileIndex];

    // MIP 0: capped running-mean merge.
    {
        const uint cellIndex = GI12_CellIndexMip(HashGrid, subgroupThreadId, tileIndex, 0u);

        float4 directRadiance = GI12_UnpackValue(Value[cellIndex]);
        const float4 newDirectRadiance = GI12_RecoverCellRadiance(uint4(
            UpdateCellValue[4u * cellIndex + 0u], UpdateCellValue[4u * cellIndex + 1u],
            UpdateCellValue[4u * cellIndex + 2u], UpdateCellValue[4u * cellIndex + 3u]));
        float oldDirectSampleCount = directRadiance.w;
        float directSampleCount = min(oldDirectSampleCount + newDirectRadiance.w, HashGrid.MaxSampleCount);
        directRadiance /= max(oldDirectSampleCount, 1.0f);
        float4 newDirectAvg = newDirectRadiance / max(newDirectRadiance.w, 1.0f);
        if (oldDirectSampleCount <= 0.0f)
            directRadiance = newDirectAvg;
        else
            directRadiance = lerp(directRadiance, newDirectAvg,
                min(newDirectRadiance.w, directSampleCount - oldDirectSampleCount) / max(directSampleCount, 1.0f));
        directRadiance *= directSampleCount;

#ifdef GI12_MULTIBOUNCE
        float4 indirectRadiance = GI12_UnpackValue(ValueIndirect[cellIndex]);
        const float4 newIndirectRadiance = GI12_RecoverCellRadiance(uint4(
            UpdateCellValueIndirect[4u * cellIndex + 0u], UpdateCellValueIndirect[4u * cellIndex + 1u],
            UpdateCellValueIndirect[4u * cellIndex + 2u], UpdateCellValueIndirect[4u * cellIndex + 3u]));
        float oldIndirectSampleCount = indirectRadiance.w;
        float indirectSampleCount = min(oldIndirectSampleCount + newIndirectRadiance.w,
            HashGrid.MaxMultibounceSampleCount);
        indirectRadiance /= max(oldIndirectSampleCount, 1.0f);
        float4 newIndirectAvg = newIndirectRadiance / max(newIndirectRadiance.w, 1.0f);
        if (oldIndirectSampleCount <= 0.0f)
            indirectRadiance = newIndirectAvg;
        else
            indirectRadiance = lerp(indirectRadiance, newIndirectAvg,
                min(newIndirectRadiance.w, indirectSampleCount - oldIndirectSampleCount) /
                    max(indirectSampleCount, 1.0f));
        indirectRadiance *= indirectSampleCount;
#endif

        const uint2 packedDirect = GI12_PackValue(directRadiance);
        GI12_UpdateTilesLDS[groupThreadId.x][groupThreadId.y] = packedDirect;
        Value[cellIndex] = packedDirect;

#ifdef GI12_MULTIBOUNCE
        const uint2 packedIndirect = GI12_PackValue(indirectRadiance);
        GI12_UpdateTilesLDSIndirect[groupThreadId.x][groupThreadId.y] = packedIndirect;
        ValueIndirect[cellIndex] = packedIndirect;
#endif

        UpdateCellValue[4u * cellIndex + 0u] = 0u;
        UpdateCellValue[4u * cellIndex + 1u] = 0u;
        UpdateCellValue[4u * cellIndex + 2u] = 0u;
        UpdateCellValue[4u * cellIndex + 3u] = 0u;
#ifdef GI12_MULTIBOUNCE
        UpdateCellValueIndirect[4u * cellIndex + 0u] = 0u;
        UpdateCellValueIndirect[4u * cellIndex + 1u] = 0u;
        UpdateCellValueIndirect[4u * cellIndex + 2u] = 0u;
        UpdateCellValueIndirect[4u * cellIndex + 3u] = 0u;
#endif
    }

    // MIP 1 box filter from LDS mip0.
    GroupMemoryBarrierWithGroupSync();
    [branch]
    if (HashGrid.SizeTileMip1 > 0u && all((groupThreadId % 2u) == 0u)) {
        const uint cellIndex = GI12_CellIndexMip(HashGrid, subgroupThreadId, tileIndex, 1u);
        float4 radiance = float4(0.0f, 0.0f, 0.0f, 0.0f);
        radiance += GI12_UnpackValue(GI12_UpdateTilesLDS[groupThreadId.x + 0u][groupThreadId.y + 0u]);
        radiance += GI12_UnpackValue(GI12_UpdateTilesLDS[groupThreadId.x + 1u][groupThreadId.y + 0u]);
        radiance += GI12_UnpackValue(GI12_UpdateTilesLDS[groupThreadId.x + 0u][groupThreadId.y + 1u]);
        radiance += GI12_UnpackValue(GI12_UpdateTilesLDS[groupThreadId.x + 1u][groupThreadId.y + 1u]);
        const uint2 packed = GI12_PackValue(radiance);
        GI12_UpdateTilesLDS[groupThreadId.x][groupThreadId.y] = packed;
        Value[cellIndex] = packed;
#ifdef GI12_MULTIBOUNCE
        float4 indirect = float4(0.0f, 0.0f, 0.0f, 0.0f);
        indirect += GI12_UnpackValue(GI12_UpdateTilesLDSIndirect[groupThreadId.x + 0u][groupThreadId.y + 0u]);
        indirect += GI12_UnpackValue(GI12_UpdateTilesLDSIndirect[groupThreadId.x + 1u][groupThreadId.y + 0u]);
        indirect += GI12_UnpackValue(GI12_UpdateTilesLDSIndirect[groupThreadId.x + 0u][groupThreadId.y + 1u]);
        indirect += GI12_UnpackValue(GI12_UpdateTilesLDSIndirect[groupThreadId.x + 1u][groupThreadId.y + 1u]);
        const uint2 packedIndirect = GI12_PackValue(indirect);
        GI12_UpdateTilesLDSIndirect[groupThreadId.x][groupThreadId.y] = packedIndirect;
        ValueIndirect[cellIndex] = packedIndirect;
#endif
    }

    // MIP 2 box filter from LDS mip1.
    GroupMemoryBarrierWithGroupSync();
    [branch]
    if (HashGrid.SizeTileMip2 > 0u && all((groupThreadId % 4u) == 0u)) {
        const uint cellIndex = GI12_CellIndexMip(HashGrid, subgroupThreadId, tileIndex, 2u);
        float4 radiance = float4(0.0f, 0.0f, 0.0f, 0.0f);
        radiance += GI12_UnpackValue(GI12_UpdateTilesLDS[groupThreadId.x + 0u][groupThreadId.y + 0u]);
        radiance += GI12_UnpackValue(GI12_UpdateTilesLDS[groupThreadId.x + 2u][groupThreadId.y + 0u]);
        radiance += GI12_UnpackValue(GI12_UpdateTilesLDS[groupThreadId.x + 0u][groupThreadId.y + 2u]);
        radiance += GI12_UnpackValue(GI12_UpdateTilesLDS[groupThreadId.x + 2u][groupThreadId.y + 2u]);
        const uint2 packed = GI12_PackValue(radiance);
        GI12_UpdateTilesLDS[groupThreadId.x][groupThreadId.y] = packed;
        Value[cellIndex] = packed;
#ifdef GI12_MULTIBOUNCE
        float4 indirect = float4(0.0f, 0.0f, 0.0f, 0.0f);
        indirect += GI12_UnpackValue(GI12_UpdateTilesLDSIndirect[groupThreadId.x + 0u][groupThreadId.y + 0u]);
        indirect += GI12_UnpackValue(GI12_UpdateTilesLDSIndirect[groupThreadId.x + 2u][groupThreadId.y + 0u]);
        indirect += GI12_UnpackValue(GI12_UpdateTilesLDSIndirect[groupThreadId.x + 0u][groupThreadId.y + 2u]);
        indirect += GI12_UnpackValue(GI12_UpdateTilesLDSIndirect[groupThreadId.x + 2u][groupThreadId.y + 2u]);
        const uint2 packedIndirect = GI12_PackValue(indirect);
        GI12_UpdateTilesLDSIndirect[groupThreadId.x][groupThreadId.y] = packedIndirect;
        ValueIndirect[cellIndex] = packedIndirect;
#endif
    }

    // MIP 3 box filter from LDS mip2.
    GroupMemoryBarrierWithGroupSync();
    [branch]
    if (HashGrid.SizeTileMip3 > 0u && all((groupThreadId % 8u) == 0u)) {
        const uint cellIndex = GI12_CellIndexMip(HashGrid, subgroupThreadId, tileIndex, 3u);
        float4 radiance = float4(0.0f, 0.0f, 0.0f, 0.0f);
        radiance += GI12_UnpackValue(GI12_UpdateTilesLDS[groupThreadId.x + 0u][groupThreadId.y + 0u]);
        radiance += GI12_UnpackValue(GI12_UpdateTilesLDS[groupThreadId.x + 4u][groupThreadId.y + 0u]);
        radiance += GI12_UnpackValue(GI12_UpdateTilesLDS[groupThreadId.x + 0u][groupThreadId.y + 4u]);
        radiance += GI12_UnpackValue(GI12_UpdateTilesLDS[groupThreadId.x + 4u][groupThreadId.y + 4u]);
        Value[cellIndex] = GI12_PackValue(radiance);
#ifdef GI12_MULTIBOUNCE
        float4 indirect = float4(0.0f, 0.0f, 0.0f, 0.0f);
        indirect += GI12_UnpackValue(GI12_UpdateTilesLDSIndirect[groupThreadId.x + 0u][groupThreadId.y + 0u]);
        indirect += GI12_UnpackValue(GI12_UpdateTilesLDSIndirect[groupThreadId.x + 4u][groupThreadId.y + 0u]);
        indirect += GI12_UnpackValue(GI12_UpdateTilesLDSIndirect[groupThreadId.x + 0u][groupThreadId.y + 4u]);
        indirect += GI12_UnpackValue(GI12_UpdateTilesLDSIndirect[groupThreadId.x + 4u][groupThreadId.y + 4u]);
        ValueIndirect[cellIndex] = GI12_PackValue(indirect);
#endif
    }
}
