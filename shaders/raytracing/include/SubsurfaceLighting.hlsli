/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __SUBSURFACE_LIGHTING_HLSLI__
#define __SUBSURFACE_LIGHTING_HLSLI__

#define USE_DIFFUSE_MEAN_FREE_PATH 1

#define SSS_TRANSMISSION_BSDF_SAMPLE_COUNT 1
#define SSS_TRANSMISSION_PER_BSDF_SCATTERING_SAMPLE_COUNT 1

#include "Include/Lighting.hlsli"
#include "Raytracing/Include/Rays.hlsli"

#include "Raytracing/Include/Materials/LobeType.hlsli"
#include "Raytracing/Include/Materials/SubsurfaceScattering.hlsli"
#include "Raytracing/Include/Materials/Transmission.hlsli"

#define SSS_SETTINGS Raytracing.SubSurfaceScattering

float3 evalSingleScatteringTransmission(
    const Surface sourceSurface,
    const BRDFContext sourceBRDFContext,
    const Material sourceMaterial,
    const Instance sourceInstance,
    const SubsurfaceMaterialData subsurfaceMaterialData,
    const SubsurfaceInteraction subsurfaceInteraction,
    RayCone rayCone,
    inout uint randomSeed)
{
    float3 radiance = float3(0.0f, 0.0f, 0.0f);

    const SubsurfaceMaterialCoefficients sssMaterialCoefficients = ComputeSubsurfaceMaterialCoefficients(subsurfaceMaterialData);

    for (int bsdfSampleIndex = 0; bsdfSampleIndex < SSS_TRANSMISSION_BSDF_SAMPLE_COUNT; ++bsdfSampleIndex)
    {
        // Trace rays for diffuse transmittance into the volume
        const float3 refractedRayDirection = CalculateRefractionRay(subsurfaceInteraction, float2(Random(randomSeed), Random(randomSeed)));
        const float3 hitPos = subsurfaceInteraction.centerPosition;

        float thickness = 0.0f;
        float3 backPosition;
        {
            RayDesc transmissionRay;
            transmissionRay.Origin = OffsetRay(hitPos, -sourceSurface.FaceNormal);
            transmissionRay.Direction = refractedRayDirection;
            transmissionRay.TMin = 0.0f;
            transmissionRay.TMax = RAY_TMAX;

            Payload payload = TraceRayOpaque(Scene, transmissionRay, randomSeed);

            thickness = payload.hitDistance;
            backPosition = transmissionRay.Origin + thickness * transmissionRay.Direction;

            if (payload.Hit())
            {
                float3 localPosition = transmissionRay.Origin + refractedRayDirection * payload.hitDistance;

                Instance sampleInstance;
                Material sampleMaterial;
                Surface sampleSurface = SurfaceMaker::make(localPosition, payload, refractedRayDirection, rayCone, sampleInstance, sampleMaterial, false);

                const float3 sampleGeometryNormal = sampleSurface.FaceNormal;
                const float3 sampleShadingNormal = sampleSurface.Normal;
                backPosition = OffsetRay(backPosition, sampleGeometryNormal, sampleSurface.PositionError, false);

                // Prepare data needed to evaluate the light
                float3 incidentVector = 0.0f;
                float lightDistance = 0.0f;
                float3 irradiance = 0.0f;
                GetLightIrradianceMIS(sampleInstance, sampleSurface, irradiance, incidentVector, lightDistance, randomSeed);

                const float3 vectorToLight = normalize(incidentVector);

                if (any(irradiance > MIN_DIFFUSE_SHADOW))
                {
                    const float3 lightVisibility = TraceRayShadowFinite(Scene, sampleSurface, vectorToLight, lightDistance, randomSeed);
                    if (any(lightVisibility > 0.0f))
                    {
                        const float3 lightRadiance = irradiance * lightVisibility;
                        const float3 transmissionBsdf = EvaluateBoundaryTerm(sourceSurface.Normal,
                                                                             vectorToLight,
                                                                             refractedRayDirection,
                                                                             sampleShadingNormal,
                                                                             thickness,
                                                                             sssMaterialCoefficients);

                        // Li * bsdf * cosTheta / CosineLobePDF = Li * bsdf * cosTheta / (cosTheta / pi) = Li * bsdf * pi
                        radiance += lightRadiance * transmissionBsdf * K_PI;
                    }
                }
            }
        }

        // Trace rays along the scattering ray
        const float stepSize = thickness / (SSS_TRANSMISSION_PER_BSDF_SCATTERING_SAMPLE_COUNT + 1);
        float accumulatedT = 0.0f;
        float3 scatteringThroughput = float3(0.0f, 0.0f, 0.0f);

        for (int sampleIndex = 0; sampleIndex < SSS_TRANSMISSION_PER_BSDF_SCATTERING_SAMPLE_COUNT; ++sampleIndex)
        {
            // TODO: Important Sampling along the scattering ray direction
            const float currentT = accumulatedT + stepSize;
            accumulatedT = currentT;

            if (currentT >= thickness)
            {
                // TODO: Here should be continue if important sampling
                break;
            }

            const float3 samplePosition = hitPos + currentT * refractedRayDirection;
            const float2 hgRnd = float2(Random(randomSeed), Random(randomSeed));
            const float3 scatteringDirection = SampleDirectionHenyeyGreenstein(hgRnd, subsurfaceMaterialData.g, refractedRayDirection);

            RayDesc scatteringRay;
            scatteringRay.Origin = samplePosition;
            scatteringRay.Direction = scatteringDirection;
            scatteringRay.TMin = 0.0f;
            scatteringRay.TMax = RAY_TMAX;

            Payload scatteringPayload = TraceRayOpaque(Scene, scatteringRay, randomSeed);

            if (scatteringPayload.Hit())
            {
                float3 scatterLocalPosition = scatteringRay.Origin + scatteringDirection * scatteringPayload.hitDistance;

                Instance scatterInstance;
                Material scatterMaterial;
                Surface scatterSurface = SurfaceMaker::make(scatterLocalPosition, scatteringPayload, scatteringDirection, rayCone, scatterInstance, scatterMaterial, false);

                const float3 scatteringSampleGeometryNormal = scatterSurface.FaceNormal;

                float3 scatteringBoundaryPosition = samplePosition + scatteringPayload.hitDistance * scatteringDirection;
                scatteringBoundaryPosition = OffsetRay(scatteringBoundaryPosition, scatteringSampleGeometryNormal, scatterSurface.PositionError, false);

                // Prepare data needed to evaluate the light
                float3 incidentVector = 0.0f;
                float lightDistance = 0.0f;
                float3 irradiance = 0.0f;
                GetLightIrradianceMIS(scatterInstance, scatterSurface, irradiance, incidentVector, lightDistance, randomSeed);

                const float3 vectorToLight = normalize(incidentVector);

                if (any(irradiance > MIN_DIFFUSE_SHADOW))
                {
                    const float3 lightVisibility = TraceRayShadowFinite(Scene, scatterSurface, vectorToLight, lightDistance, randomSeed);
                    if (any(lightVisibility > 0.0f))
                    {
                        const float3 lightRadiance = irradiance * lightVisibility;
                        const float totalScatteringDistance = currentT + scatteringPayload.hitDistance;
                        const float3 ssTransmissionBsdf = EvaluateSingleScattering(vectorToLight,
                                                                                   scatterSurface.Normal,
                                                                                   totalScatteringDistance,
                                                                                   sssMaterialCoefficients);

                        scatteringThroughput += lightRadiance * ssTransmissionBsdf * stepSize; // Li * BSDF / PDF
                    }
                }
            }
        }

        radiance += scatteringThroughput / SSS_TRANSMISSION_PER_BSDF_SCATTERING_SAMPLE_COUNT;
    }

    radiance /= SSS_TRANSMISSION_BSDF_SAMPLE_COUNT;

    return radiance;
}

float3 EvaluateSubsurfaceDiffuseNEE(
    const Surface surface,
    const BRDFContext brdfContext,
    const Material material,
    const Instance instance,
    const Payload initialPayload,
    RayCone rayCone,
    inout uint randomSeed,
    const bool primary)
{
    SubsurfaceMaterialData subsurfaceMaterialData = CreateDefaultSubsurfaceMaterialData();
    subsurfaceMaterialData.transmissionColor = surface.SubsurfaceData.TransmissionColor;
    subsurfaceMaterialData.scatteringColor = surface.SubsurfaceData.ScatteringColor;
    subsurfaceMaterialData.scale = surface.SubsurfaceData.Scale;
    subsurfaceMaterialData.g = surface.SubsurfaceData.Anisotropy;

    if (SSS_SETTINGS.MaterialOverride) {
        subsurfaceMaterialData.transmissionColor = SSS_SETTINGS.TransmissionColorOverride;
        subsurfaceMaterialData.scatteringColor = SSS_SETTINGS.ScatteringColorOverride;
        subsurfaceMaterialData.scale = SSS_SETTINGS.ScaleOverride;
        subsurfaceMaterialData.g = SSS_SETTINGS.AnisotropyOverride;
    }

    const float3 geometryNormal = surface.FaceNormal;
    const float3 shadingNormal = surface.Normal;

    const float3 tangentWorld = any(dot(surface.Tangent, surface.Tangent) > 1e-5f) ?
        normalize(surface.Tangent) :
        (dot(geometryNormal, float3(0.0f, 1.0f, 0.0f)) < 0.999f ? cross(geometryNormal, float3(0.0f, 1.0f, 0.0f)) :
                                                                  cross(geometryNormal, float3(1.0f, 0.0f, 0.0f)));

    const float3 biTangentWorld = cross(tangentWorld, geometryNormal);
    SubsurfaceInteraction subsurfaceInteraction =
        CreateSubsurfaceInteraction(surface.Position, shadingNormal, tangentWorld, biTangentWorld);

    float3 radiance = float3(0.0f, 0.0f, 0.0f);

    float3 incidentVector;
    float lightDistance;
    float3 irradiance;
    GetLightIrradianceMIS(instance, surface, irradiance, incidentVector, lightDistance, randomSeed);
    const float3 vectorToLight = normalize(incidentVector);
    const float3 lightVector = vectorToLight * lightDistance;

    if (any(irradiance) > MIN_DIFFUSE_SHADOW)
    {
        const float3 centerSpecularF0 = surface.F0;
        const float3 diffuseAlbedo = surface.DiffuseAlbedo;
        subsurfaceMaterialData.transmissionColor = SSS_SETTINGS.MaterialOverride ? subsurfaceMaterialData.transmissionColor : diffuseAlbedo;

        const float3 cameraUp = float3(
            Camera.ViewInverse[0][0],
            Camera.ViewInverse[1][0],
            Camera.ViewInverse[2][0]);

        const float3 cameraDirection = float3(
            Camera.ViewInverse[0][2],
            Camera.ViewInverse[1][2],
            Camera.ViewInverse[2][2]);

        if (Random(randomSeed) < 0.5f)
        {
            subsurfaceInteraction.normal = -cameraDirection;
            subsurfaceInteraction.tangent = cameraUp;
            subsurfaceInteraction.biTangent = cross(cameraUp, -cameraDirection);
        }

        uint effectiveSample = 0;

        for (uint sssSampleIndex = 0; sssSampleIndex < SSS_SETTINGS.SampleCount; ++sssSampleIndex)
        {
            SubsurfaceSample subsurfaceSample;

            const float2 rand2 = float2(Random(randomSeed), Random(randomSeed));
            EvalBurleyDiffusionProfile(subsurfaceMaterialData,
                                      subsurfaceInteraction,
                                      SSS_SETTINGS.MaxSampleRadius,
                                      SSS_SETTINGS.EnableTransmission && false, // disable normalization
                                      rand2,
                                      subsurfaceSample);

            Payload samplePayload = SampleSubsurface(Scene, subsurfaceSample.samplePosition, subsurfaceInteraction.normal, RAY_TMAX, randomSeed);

            if (samplePayload.Hit() && samplePayload.InstanceIndex() == initialPayload.InstanceIndex())
            {
                const float3 sampleLocalPosition = subsurfaceSample.samplePosition + samplePayload.hitDistance * (-subsurfaceInteraction.normal);
                Instance sampleInstance;
                Material sampleMaterial;
                Surface sampleSurface = SurfaceMaker::make(sampleLocalPosition, samplePayload, -subsurfaceInteraction.normal, rayCone, sampleInstance, sampleMaterial, primary);
                if (sampleSurface.SubsurfaceData.HasSubsurface == 0)
                {
                    continue;
                }

                const float3 sampleGeometryNormal = sampleSurface.FaceNormal;
                const float3 sampleShadingNormal = sampleSurface.Normal;
                const bool transition = dot(vectorToLight, sampleGeometryNormal) < 0.0f;
                const float3 samplePosition = subsurfaceSample.samplePosition - subsurfaceInteraction.normal * samplePayload.hitDistance;

                float3 sampleShadowHitPos = OffsetRay(samplePosition, sampleGeometryNormal, sampleSurface.PositionError, transition);

                // Prepare data needed to evaluate the sample light
                float3 sampleIncidentVector = float3(0.0f, 0.0f, 0.0f);
                float sampleLightDistance = 0.0f;
                float3 sampleLightIrradiance = 0.0f;

                GetLightIrradianceMIS(sampleInstance, sampleSurface, sampleLightIrradiance, sampleIncidentVector, sampleLightDistance, randomSeed);

                if (any(sampleLightIrradiance > MIN_DIFFUSE_SHADOW)) {
                    const float3 vectorToLight = normalize(sampleIncidentVector);

                    // Cast shadow ray towards the selected light for current SSS sample
                    const float3 sampleLightVisibility = TraceRayShadowFinite(Scene, sampleSurface, vectorToLight, sampleLightDistance, randomSeed);
                    if (any(sampleLightVisibility > 0.0f))
                    {
                        const float3 sampleLightRadiance = sampleLightIrradiance * sampleLightVisibility;
                        const float cosThetaI = min(max(0.00001f, dot(vectorToLight, sampleShadingNormal)), 1.0f);
                        radiance += max(EvalBssrdf(subsurfaceSample, sampleLightRadiance, cosThetaI), 0.0f);

                        ++effectiveSample;
                    }
                }
            }
        }

        radiance /= (float)SSS_SETTINGS.SampleCount;
    }

    if (SSS_SETTINGS.EnableTransmission)
    {
        radiance += max(evalSingleScatteringTransmission(
            surface,
            brdfContext,
            material,
            instance,
            subsurfaceMaterialData,
            subsurfaceInteraction,
            rayCone,
            randomSeed), 0.0f);
    }

    return radiance;
}

#endif // __SUBSURFACE_LIGHTING_HLSLI__