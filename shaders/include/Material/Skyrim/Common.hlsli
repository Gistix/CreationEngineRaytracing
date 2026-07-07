#ifndef SKYRIM_MATERIAL_COMMON_HLSL
#define SKYRIM_MATERIAL_COMMON_HLSL

#include "interop/Material/MaterialBaseData.hlsli"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

#define LIGHTINGSETTINGS Raytracing
#define HAIRSETTINGS Features.HairSpecular
#define SKINSETTINGS Features.Skin

static const uint kBaseSize = sizeof(MaterialBaseData);
static const uint kLightingSize = sizeof(LightingMaterialData);

#endif
