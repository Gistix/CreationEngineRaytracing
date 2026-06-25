#ifndef WATER_MATERIAL_DATA_HLSL
#define WATER_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

INTEROP_STRUCT(WaterMaterialDataExtra, 4)
{
    half4 Vector0;
    half4 Vector1;
    half4 Vector2;
    half4 Vector3;
    half Scalar0;
    half Scalar1;
    half Scalar2;
    half4 Color0;
    uint16_t Texture0;
    uint16_t Texture1;
    uint16_t Texture2;
    uint16_t Texture3;
    uint16_t Pad;
};
VALIDATE_ALIGNMENT(WaterMaterialDataExtra, 4);

INTEROP_STRUCT(WaterMaterialData : LightingMaterialData, 4)
{
    half4 Vector0;
    half4 Vector1;
    half4 Vector2;
    half4 Vector3;
    half Scalar0;
    half Scalar1;
    half Scalar2;
    half4 Color0;
    uint16_t Texture0;
    uint16_t Texture1;
    uint16_t Texture2;
    uint16_t Texture3;
    uint16_t Pad;
};
VALIDATE_ALIGNMENT(WaterMaterialData, 4);
VALIDATE_SIZE(WaterMaterialData, sizeof(LightingMaterialData) + sizeof(WaterMaterialDataExtra));

#endif // WATER_MATERIAL_DATA_HLSL
