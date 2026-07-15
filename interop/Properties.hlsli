#ifndef PROPERTIES_SKYRIM_HLSL
#define PROPERTIES_SKYRIM_HLSL

#include "Interop.h"

#ifndef __cplusplus
namespace ShaderFlags
{
    static const uint kSpecular = (1 << 0);
    static const uint kVertexAlpha = (1 << 2);
    static const uint kGrayscaleToPaletteColor = (1 << 3);
    static const uint kGrayscaleToPaletteAlpha = (1 << 4);
    static const uint kFalloff = (1 << 5);
    static const uint kEnvMap = (1 << 6);
    static const uint kFace = (1 << 7);
    static const uint kModelSpaceNormals = (1 << 8);
    static const uint kRefraction = (1 << 9);
    static const uint kProjectedUV = (1 << 10);
    static const uint kExternalEmittance = (1 << 11);
    static const uint kVertexColors = (1 << 12);
    static const uint kMultiTextureLandscape = (1 << 13);
    static const uint kEyeReflect = (1 << 14);
    static const uint kHairTint = (1 << 15);
    static const uint kTwoSided = (1 << 16);
    static const uint kAssumeShadowmask = (1 << 17);
    static const uint kBackLighting = (1 << 18);
    static const uint kTreeAnim = (1 << 19);
    static const uint kSoftLighting = (1 << 20);
    static const uint kLODLandscape = (1 << 21);
    static const uint kLODObjects = (1 << 22);
    static const uint kHDLODObjects = (1 << 23);
    static const uint kSnow = (1 << 24);
}

namespace AlphaFlags
{
    static const uint16_t None = 0;
    static const uint16_t Blend = (1 << 0);
    static const uint16_t Test = (1 << 1);
    static const uint16_t Transmission = (1 << 2);
    static const uint16_t Additive = (1 << 3);
}

namespace WaterFlags
{
    static const uint16_t kActorInWater = (1 << 0);
    static const uint16_t kActorMovingInWater = (1 << 1);
    static const uint16_t kVertexUV = (1 << 2);
    static const uint16_t kEnableFlowmap = (1 << 3);
    static const uint16_t kBlendNormals = (1 << 4);
    static const uint16_t kDisplacement = (1 << 5);
    static const uint16_t kVertexAlphaDepth = (1 << 6);
    static const uint16_t kDepth = (1 << 7);
}
#endif

INTEROP_DATA_STRUCT(Properties, 4)
{ 
    uint ShaderFlags;
    uint16_t AlphaFlags;
    uint16_t WaterFlags;
    half AlphaThreshold;
    half Alpha;
    half4 EmissiveColor;
    half4 ProjectedUVParams;
    half4 ProjectedUVParams2;
    half4 ProjectedUVParams3;
    half4 TextureProj;
};
VALIDATE_ALIGNMENT(PropertiesData, 4);

#endif // PROPERTIES_SKYRIM_HLSL