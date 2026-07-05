#ifndef MULTI_LAYER_PARALLAX_MATERIAL_DATA_HLSL
#define MULTI_LAYER_PARALLAX_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

INTEROP_STRUCT(MultiLayerParallaxMaterialDataExtra, 4)
{
    uint16_t LayerTexture;
    uint16_t EnvironmentTexture;
    uint16_t EnvironmentMaskTexture;
    half LayerThickness;
    half RefractionScale;
    half InnerLayerUScale;
    half InnerLayerVScale;
    half EnvironmentScale;
};
VALIDATE_ALIGNMENT(MultiLayerParallaxMaterialDataExtra, 4);

INTEROP_STRUCT(MultiLayerParallaxMaterialData : LightingMaterialData, 4)
{
    uint16_t LayerTexture;
    uint16_t EnvironmentTexture;
    uint16_t EnvironmentMaskTexture;
    half LayerThickness;
    half RefractionScale;
    half InnerLayerUScale;
    half InnerLayerVScale;
    half EnvironmentScale;
};
VALIDATE_ALIGNMENT(MultiLayerParallaxMaterialData, 4);
VALIDATE_SIZE(MultiLayerParallaxMaterialData, sizeof(LightingMaterialData) + sizeof(MultiLayerParallaxMaterialDataExtra));

#endif // MULTI_LAYER_PARALLAX_MATERIAL_DATA_HLSL
