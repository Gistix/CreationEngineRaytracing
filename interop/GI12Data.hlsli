#ifndef GI12DATA_HLSLI
#define GI12DATA_HLSLI

#include "Interop.h"

// Port of Capsaicin GI-1.2 (GI1) shared constants, adapted to NVRHI bindings.
// See Capsaicin src/core/src/render_techniques/gi1/gi1_shared.h (MIT).

INTEROP_STRUCT(GI12ScreenProbesData, 16)
{
    float CellSize;
    uint ProbeSize;
    uint2 ProbeCount;
    uint ProbeSpawnTileSize;
    uint ProbeMaskMipCount;
    uint2 Pad0;
};
VALIDATE_CBUFFER(GI12ScreenProbesData, 16);

INTEROP_STRUCT(GI12HashGridData, 16)
{
    float CellSize;
    float MinCellSize;
    float TileSize;
    float TileCellRatio;
    uint NumBuckets;
    uint NumTiles;
    uint NumCells;
    uint NumTilesPerBucket;
    uint SizeTileMip0;
    uint SizeTileMip1;
    uint SizeTileMip2;
    uint SizeTileMip3;
    uint NumCellsPerTileMip0;
    uint NumCellsPerTileMip1;
    uint NumCellsPerTileMip2;
    uint NumCellsPerTileMip3;
    uint NumCellsPerTile;
    uint FirstCellOffsetTileMip0;
    uint FirstCellOffsetTileMip1;
    uint FirstCellOffsetTileMip2;
    uint FirstCellOffsetTileMip3;
    uint BufferPingPong;
    float MaxSampleCount;
    float DiscardMultibounceProbability;
    float MaxMultibounceSampleCount;
    uint DebugMipLevel;
    uint FrameIndex;
    uint Pad0;
    uint Pad1;
    uint Pad2;
    uint Pad3;
    uint Pad4;
};
VALIDATE_CBUFFER(GI12HashGridData, 16);

// Mirrors Capsaicin GlossyReflectionsConstants (gi1_shared.h).
INTEROP_STRUCT(GI12GlossyData, 16)
{
    uint2 FullRes;
    int FullRadius;
    int HalfRadius;
    float LowRoughnessThreshold;
    float HighRoughnessThreshold;
    uint HalfRes;
    uint AtrousPassCount;
    uint FrameIndex;
    int MarkFirefliesHalfRadius;
    int MarkFirefliesFullRadius;
    int CleanupFirefliesHalfRadius;
    int CleanupFirefliesFullRadius;
    float MarkFirefliesHalfLowThreshold;
    float MarkFirefliesFullLowThreshold;
    float MarkFirefliesHalfHighThreshold;
    float MarkFirefliesFullHighThreshold;
    uint CleanupFireflies;
    uint HistoryValid;
    float LuminanceDiffScale;
};
VALIDATE_CBUFFER(GI12GlossyData, 16);

// Per-pass a-trous data (push constants). ReadSlot selects which of the two
// ping-pong slots is read; the pass writes the other slot.
INTEROP_STRUCT(GI12GlossyAtrousData, 16)
{
    int FullStep;
    uint ReadSlot;
    uint PassIndex;
    uint Pad0;
};
VALIDATE_CBUFFER(GI12GlossyAtrousData, 16);

INTEROP_STRUCT(GI12Data, 16)
{
    uint Enabled;
    uint ScreenProbes;
    uint HashGridCache;
    uint GlossyReflections;
    uint FrameIndex;
    uint NumLights;
    uint2 Pad0;
};
VALIDATE_CBUFFER(GI12Data, 16);

#endif
