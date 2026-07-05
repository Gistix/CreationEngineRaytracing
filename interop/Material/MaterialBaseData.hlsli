#ifndef MATERIAL_BASE_DATA_HLSL
#define MATERIAL_BASE_DATA_HLSL

#include "interop/Interop.h"

#ifndef __cplusplus
namespace Type
{
    static const uint16_t Lighting = 0;
    static const uint16_t Effect = 1;
    static const uint16_t Grass = 2;
    static const uint16_t Water = 3;
    static const uint16_t BloodSplatter = 4;
    static const uint16_t DistantTree = 5;
    static const uint16_t Particle = 6;
    static const uint16_t TruePBR = 7;
}

namespace Feature
{
    static const uint16_t kDefault = 0;
    static const uint16_t kEnvironmentMap = 1;
    static const uint16_t kGlowMap = 2;
    static const uint16_t kParallax = 3;
    static const uint16_t kFaceGen = 4;
    static const uint16_t kSkinTint = 5;
    static const uint16_t kHairTint = 6;
    static const uint16_t kParallaxOcc = 7;
    static const uint16_t kMultiTexLand = 8;
    static const uint16_t kLODLand = 9;
    static const uint16_t kUnknown = 10;
    static const uint16_t kMultilayerParallax = 11;
    static const uint16_t kTreeAnim = 12;
    static const uint16_t kMultiIndexTriShapeSnow = 14;
    static const uint16_t kLODObjectsHD = 15;
    static const uint16_t kEye = 16;
    static const uint16_t kCloud = 17;
    static const uint16_t kLODLandNoise = 18;
    static const uint16_t kMultiTexLandLODBlend = 19;
}
#endif

INTEROP_STRUCT(MaterialBaseData, 4)
{
    uint16_t Type;
    uint16_t Feature;
    half2 TexCoordOffset;
    half2 TexCoordScale;
    
#ifndef __cplusplus
    float2 TexCoord(float2 texCoord)
    {
        return texCoord * TexCoordScale + TexCoordOffset;
    }
#endif    
};
VALIDATE_ALIGNMENT(MaterialBaseData, 4);

#endif // MATERIAL_BASE_DATA_HLSL