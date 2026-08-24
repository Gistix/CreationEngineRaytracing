#ifdef SHARC

#   ifndef SHARC_HELPER_DEPENDENCY_HLSLI
#   define SHARC_HELPER_DEPENDENCY_HLSLI

#       include "include/Common/Game.hlsli"
#       include "include/Common.hlsli"
#       include "interop/SharcTypes.h"

uint Hash(uint2 idx)
{
    return (idx.x * 73856093u) ^ (idx.y * 19349663u);
}

HashGridParameters GetSharcGridParameters()
{
    HashGridParameters gridParameters;
    {
        gridParameters.cameraPosition = Camera.Position;
        gridParameters.sceneScale = SHaRC.SceneScale;
        gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
        gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;
    }

    return gridParameters;
}

SharcParameters GetSharcParameters()
{
    SharcParameters sharcParameters;
    {
        sharcParameters.hashGridParameters = GetSharcGridParameters();

        sharcParameters.hashGridData.capacity = SHARC_CAPACITY;
        sharcParameters.hashGridData.hashEntriesBuffer = SharcHashEntriesBuffer;

#if !HASH_GRID_ENABLE_64_BIT_ATOMICS && SHARC_UPDATE
        sharcParameters.hashGridData.lockBuffer = SharcLockBuffer;
#endif // !HASH_GRID_ENABLE_64_BIT_ATOMICS && SHARC_UPDATE

#if SHARC_UPDATE || SHARC_RESOLVE
        sharcParameters.accumulationBuffer = SharcAccumulationBuffer;
#endif // SHARC_UPDATE || SHARC_RESOLVE

        sharcParameters.resolvedBuffer = SharcResolvedBuffer;
        sharcParameters.radianceScale = SHaRC.RadianceScale;
    }

    return sharcParameters;
}

SharcResolveParameters GetSharcResolveParameters()
{
    SharcResolveParameters resolveParameters;
    {
        resolveParameters.accumulationFrameNum = SHaRC.AccumFrameNum;
        resolveParameters.responsiveFrameNum = SHaRC.AccumFrameNum;
        resolveParameters.staleFrameNumMax = SHaRC.StaleFrameNum;
        resolveParameters.cameraPositionPrev = Camera.PositionPrev;
        resolveParameters.frameIndex = SHaRC.FrameIndex;
    }

    return resolveParameters;
}

#   endif // SHARC_HELPER_DEPENDENCY_HLSLI

#endif // SHARC
