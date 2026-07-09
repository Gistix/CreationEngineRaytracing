#ifndef WATER_MATERIAL_FUNC_HLSL
#define WATER_MATERIAL_FUNC_HLSL

#include "include/Common.hlsli"
#include "include/ColorConversions.hlsli"
#include "include/Surface.hlsli"
#include "include/Material/Skyrim/Common.hlsli"
#include "interop/Material/Skyrim/WaterMaterialData.hlsli"
#include "include/FlowMap.hlsli"
#include "include/Wetness.hlsli"

void WaterMaterial(inout Surface surface, in float2 texCoord0, in float3 tangentWS, in float3 bitangentWS, in Mesh mesh)
{
    WaterMaterialData water = Materials[0].Load<WaterMaterialData>(mesh.GetMaterialOffset());
    const float mipLevel = surface.MipLevel;

    surface.Albedo = float3(1.0f, 1.0f, 1.0f);
    surface.Roughness = 0.0f;
    surface.Metallic = 0.0f;
    surface.F0 = 0.02f;
    surface.IOR = 1.33f;

    const bool hasFlowMap = (mesh.Properties.WaterFlags & WaterFlags::kEnableFlowmap) != 0;
    const bool hasBlendNormals = true;
    const bool hasNormalTexcoord = (mesh.Properties.WaterFlags & WaterFlags::kVertexUV) != 0;

    const bool hasVertexColor = false;

    const float scale = 0.001f;

    float2 normalScroll1 = water.NormalScroll1;
    float2 normalScroll2 = water.NormalScroll2;
    float2 normalScroll3 = water.NormalScroll3;

    float3 normalsScale = float3(water.UVScale1, water.UVScale2, water.UVScale3);

    float3 objectUV = Raytracing.WaterObjectUV;

    float4 cellTexCoordOffset = mesh.Properties.ProjectedUVParams0;

    float2 scrollAdjust1;
    float2 scrollAdjust2;
    float2 scrollAdjust3;

    if (hasNormalTexcoord && !hasFlowMap)
    {
        float3 scaledNormals = normalsScale * scale;

        scrollAdjust1 = texCoord0.xy / scaledNormals.xx;
        scrollAdjust2 = texCoord0.xy / scaledNormals.yy;
        scrollAdjust3 = texCoord0.xy / scaledNormals.zz;
    } else
    {
        scrollAdjust1 = surface.Position.xy / normalsScale.xx;
        scrollAdjust2 = surface.Position.xy / normalsScale.yy;
        scrollAdjust3 = surface.Position.xy / normalsScale.zz;
    }

    if (hasFlowMap)
    {

        float4 flowCoord;
        flowCoord.xy = (cellTexCoordOffset.xy + texCoord0.xy) / objectUV.xx;
        flowCoord.zw = (cellTexCoordOffset.zw + texCoord0.xy);

        const float flowScroll = Camera.Time;

        const float2 flowmapDimensions = objectUV.xx;
        float2 uvShift = 1 / (128 * flowmapDimensions);

        float2 normalMul = 0.5 + -(-0.5 + abs(frac(flowCoord.xy * (64 * flowmapDimensions)) * 2 - 1));

        Texture2D normals04Texture = Textures[NonUniformResourceIndex(water.NormalsTexture4)];

        float3 normals1 = GetFlowmapNormal(WaterFlowMap, PointWrapSampler, normals04Texture, DefaultSampler, flowCoord, uvShift, 9.92, 0, flowScroll, mipLevel);
        float3 normals2 = GetFlowmapNormal(WaterFlowMap, PointWrapSampler, normals04Texture, DefaultSampler, flowCoord, float2(0, uvShift.y), 10.64, 0.27, flowScroll, mipLevel);
        float3 normals3 = GetFlowmapNormal(WaterFlowMap, PointWrapSampler, normals04Texture, DefaultSampler, flowCoord, float2(0, 0), 8, 0, flowScroll, mipLevel);
        float3 normals4 = GetFlowmapNormal(WaterFlowMap, PointWrapSampler, normals04Texture, DefaultSampler, flowCoord, float2(uvShift.x, 0), 8.48, 0.62, flowScroll, mipLevel);

        float2 flowmapNormalWeighted =
            normalMul.y * (normalMul.x * normals3.xy + (1 - normalMul.x) * normals4.xy) +
            (1 - normalMul.y) *
                (normalMul.x * normals2.xy + (1 - normalMul.x) * normals1.xy);

        float2 flowmapDenominator = sqrt(normalMul * normalMul + (1 - normalMul) * (1 - normalMul));

        float3 flowmapNormal = float3(((-0.5 + flowmapNormalWeighted) / (flowmapDenominator.x * flowmapDenominator.y)), 0);
        flowmapNormal.z = sqrt(1 - flowmapNormal.x * flowmapNormal.x - flowmapNormal.y * flowmapNormal.y);

        // Always enabled for flowmapped water, since we do not render the displacement water mesh
        {   
            // Project UV from displacement mesh position and size
            float2 displacementUv = ((surface.Position.xy - Raytracing.WaterDisplacementPosition.xy) + 1024.0) / 2048.0;
            displacementUv.y = 1.0f - displacementUv.y;
            
            float3 displacement = normalize(float3(water.Amplitude4 * (-0.5 + WaterDisplacementMap.SampleLevel(ClampSampler, displacementUv, mipLevel).zw), 0.04));
            flowmapNormal = lerp(displacement, flowmapNormal, displacement.z);
        }

        surface.Normal = normalize(flowmapNormal);
    } else
    {
        float2 normalCoord1 = normalScroll1 + scrollAdjust1;
        Texture2D normals01Texture = Textures[NonUniformResourceIndex(water.NormalsTexture1)];
        float3 normals1 = normals01Texture.SampleLevel(DefaultSampler, normalCoord1, mipLevel).xyz * 2.0 + float3(-1, -1, -2);

        if (hasBlendNormals)
        {
            float2 normalCoord2 = normalScroll2 + scrollAdjust2;
            float2 normalCoord3 = normalScroll3 + scrollAdjust3;

            Texture2D normals02Texture = Textures[NonUniformResourceIndex(water.NormalsTexture2)];
            Texture2D normals03Texture = Textures[NonUniformResourceIndex(water.NormalsTexture3)];

            float3 normals2 = normals02Texture.SampleLevel(DefaultSampler, normalCoord2, mipLevel).xyz * 2.0 - 1.0;
            float3 normals3 = normals03Texture.SampleLevel(DefaultSampler, normalCoord3, mipLevel).xyz * 2.0 - 1.0;

            surface.Normal = normalize(
                float3(0, 0, 1) +
                water.Amplitude1 * normals1 +
                water.Amplitude2 * normals2 +
                water.Amplitude3 * normals3
            );
        }
        else
        {
            surface.Normal = normalize(
                float3(0, 0, 1) + normals1
            );
        }
    }

    // ---- Rain ripples on water surface ----
    if (surface.Primary && Features.WetnessEffects.Raining > 0.0 && Features.WetnessEffects.EnableRaindropFx)
    {
        float3 ripplePosition = surface.Position.xyz;
        float4 raindropInfo = Wetness::GetRainDrops(
            ripplePosition,
            Features.WetnessEffects.Time,
            float3(0, 0, 1),
            1.0,
            Features.WetnessEffects);
        float3 rippleNormal = normalize(raindropInfo.xyz);
        surface.Normal = ReorientNormal(rippleNormal, surface.Normal);
    }

    surface.Tangent = normalize(tangentWS - surface.Normal * dot(tangentWS, surface.Normal));
    surface.Bitangent = cross(surface.Normal, surface.Tangent);
    surface.Bitangent *= (dot(surface.Bitangent, bitangentWS) < 0.0f) ? -1.0f : 1.0f;

    // Distance-based absorption via Beer-Lambert law instead of flat surface tint.
    static const float WATER_ABSORPTION_REFERENCE_DEPTH = 600.0;
    float3 waterColor = saturate(water.ShallowColor.rgb);
    surface.VolumeAbsorption = -log(max(waterColor, 1e-4)) / WATER_ABSORPTION_REFERENCE_DEPTH * Raytracing.WaterAbsorptionScale;
    surface.TransmissionColor = float3(1.0f, 1.0f, 1.0f);
    surface.SpecTrans = 1.0f;
}

#endif // WATER_MATERIAL_FUNC_HLSL
