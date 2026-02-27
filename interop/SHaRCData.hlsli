#ifndef SHARCDATA_HLSLI
#define SHARCDATA_HLSLI

#include "Interop.h"

INTEROP_STRUCT(SHaRCData, 16)
{
    float SceneScale;
    uint AccumFrameNum;
    uint StaleFrameNum;
    float RadianceScale;
    uint AntifireflyFilter;
    uint3 Pad0;
};
VALIDATE_CBUFFER(SHaRCData, 16);

#endif