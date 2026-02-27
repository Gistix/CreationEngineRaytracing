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
        sharcParameters.gridParameters = GetSharcGridParameters();

        sharcParameters.hashMapData.capacity = SHARC_CAPACITY;
        sharcParameters.hashMapData.hashEntriesBuffer = SharcHashEntriesBuffer;

#if SHARC_UPDATE
#   if !SHARC_ENABLE_64_BIT_ATOMICS && !SHARC_RESOLVE
        sharcParameters.hashMapData.lockBuffer = SharcLockBuffer;
#   endif // !SHARC_ENABLE_64_BIT_ATOMICS && !SHARC_RESOLVE
#endif // SHARC_UPDATE

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
        resolveParameters.staleFrameNumMax = SHaRC.StaleFrameNum;
        resolveParameters.cameraPositionPrev = Camera.PositionPrev;
        resolveParameters.enableAntiFireflyFilter = SHaRC.AntifireflyFilter;
    }

    return resolveParameters;
}

#   endif // SHARC_HELPER_DEPENDENCY_HLSLI

#endif // SHARC