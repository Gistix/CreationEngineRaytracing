#ifndef RTXDI_PT_PATH_TRACE_BRIDGE_HLSLI
#define RTXDI_PT_PATH_TRACE_BRIDGE_HLSLI

#include "Rtxdi/PT/PathTracerContext.hlsli"
#include "Rtxdi/PT/PathTracerRandomContext.hlsli"

uint RAB_RngToUint(inout RTXDI_RandomSamplerState rng)
{
    return RTXDI_murmur3(rng);
}

float RAB_RngNext(inout RTXDI_RandomSamplerState rng)
{
    return RTXDI_GetNextRandom(rng);
}

void RAB_SetBrdfRaySampleFromBSDF(inout RTXDI_PathTracerContext ctx, BSDFSample bsdfSample)
{
    RTXDI_BrdfRaySample brs = RTXDI_EmptyBrdfRaySample();
    brs.OutDirection = bsdfSample.wo;
    brs.OutPdf = max(bsdfSample.pdf, 1e-8f);
    brs.BrdfTimesNoL = bsdfSample.weight * brs.OutPdf;

    if (bsdfSample.isLobe(LobeType::Delta)) brs.properties.SetDelta(); else brs.properties.SetContinuous();
    if (bsdfSample.isLobe(LobeType::Specular)) brs.properties.SetSpecular(); else brs.properties.SetDiffuse();
    if (bsdfSample.isLobe(LobeType::Transmission)) brs.properties.SetTransmission(); else brs.properties.SetReflection();

    ctx.SetBrdfRaySample(brs);
}

bool RAB_RecordDirectionalNee(
    inout RTXDI_PathTracerContext ctx,
    RAB_Surface rab,
    Material material,
    StandardBSDF bsdf,
    inout RTXDI_PathTracerRandomContext ptRandContext)
{
    RAB_LightInfo lightInfo;
    lightInfo.lightType = RAB_LIGHT_TYPE_DIRECTIONAL;
    lightInfo.lightIndex = 0;
    RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, rab, float2(0.5, 0.5));
    if (!any(lightSample.radiance > 0.0f))
        return false;

    float3 lightDir = normalize(lightSample.position - rab.surface.Position);
    float3 reflected = EvalLight(lightDir, material, rab.surface, rab.brdfContext, bsdf) * lightSample.radiance;
    if (!any(reflected > MIN_DIFFUSE_SHADOW))
        return false;

    if (!RAB_GetConservativeVisibility(rab, lightSample.position))
        reflected = 0.0f;

    RTXDI_SampledLightData sampledLightData = RTXDI_SampledLightData_CreateInvalidData();
    RTXDI_SampledLightData_SetLightData(sampledLightData, RAB_PackDirectionalLightIndex());
    RTXDI_SampledLightData_SetUVData(sampledLightData, float2(0.5, 0.5));

    float scatterPdf = RAB_SurfaceEvaluateBrdfPdf(rab, lightDir);
    return ctx.RecordNeeLightSample(sampledLightData, reflected, lightSample.solidAnglePdf, scatterPdf, lightSample, ptRandContext.initialRandomSamplerState);
}

bool RAB_RecordPointNee(
    inout RTXDI_PathTracerContext ctx,
    RAB_Surface rab,
    Material material,
    Instance instance,
    StandardBSDF bsdf,
    inout RTXDI_PathTracerRandomContext ptRandContext)
{
    if (instance.LightData.Count == 0)
        return false;

    uint lightSlot = min(uint(RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState) * instance.LightData.Count), instance.LightData.Count - 1);
    uint lightIndex = instance.LightData.GetID(lightSlot);
    RAB_LightInfo lightInfo;
    lightInfo.lightType = RAB_LIGHT_TYPE_POINT;
    lightInfo.lightIndex = lightIndex;
    RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, rab, float2(0.5, 0.5));
    if (!any(lightSample.radiance > 0.0f))
        return false;

    float3 lightDir = normalize(lightSample.position - rab.surface.Position);
    float3 reflected = EvalLight(lightDir, material, rab.surface, rab.brdfContext, bsdf) * lightSample.radiance * instance.LightData.Count;
    if (!any(reflected > MIN_DIFFUSE_SHADOW))
        return false;

    if (!RAB_GetConservativeVisibility(rab, lightSample.position))
        reflected = 0.0f;

    RTXDI_SampledLightData sampledLightData = RTXDI_SampledLightData_CreateInvalidData();
    RTXDI_SampledLightData_SetLightData(sampledLightData, RAB_PackPointLightIndex(lightIndex));
    RTXDI_SampledLightData_SetUVData(sampledLightData, float2(0.5, 0.5));

    float neePdf = max(lightSample.solidAnglePdf / max(float(instance.LightData.Count), 1.0f), 1e-8f);
    float scatterPdf = RAB_SurfaceEvaluateBrdfPdf(rab, lightDir);
    return ctx.RecordNeeLightSample(sampledLightData, reflected, neePdf, scatterPdf, lightSample, ptRandContext.initialRandomSamplerState);
}

void RAB_PathTrace(inout RTXDI_PathTracerContext ctx, inout RTXDI_PathTracerRandomContext ptRandContext, inout RAB_PathTracerUserData ptud)
{
    RAB_Surface rab = ctx.GetIntersectionSurface();
    if (!RAB_IsSurfaceValid(rab))
        return;

    Surface surface = rab.surface;
    Material material = Materials[NonUniformResourceIndex(rab.materialIndex)];
    Instance instance = Instances[NonUniformResourceIndex(rab.instanceIndex)];
    BRDFContext brdfContext = rab.brdfContext;
    bool isEnter = dot(surface.FaceNormal, brdfContext.ViewDirection) >= 0.0f;
    StandardBSDF bsdf = StandardBSDF::make(surface, isEnter);
    RayCone rayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * max(rab.viewDepth, 1.0f), Raytracing.PixelConeSpreadAngle);

    [loop]
    while (ctx.GetBounceDepth() <= ctx.GetMaxPathBounce())
    {
        ctx.BeginPathState();
        if (ctx.IsPathTerminated())
            break;

        RAB_Surface currentRab = rab;
        currentRab.surface = surface;
        currentRab.brdfContext = brdfContext;
        currentRab.materialFeature = material.Feature;
        currentRab.hasSpecularTransmission = surface.SpecTrans > 0.0f;
        currentRab.materialIndex = rab.materialIndex;
        currentRab.instanceIndex = rab.instanceIndex;

        if (ctx.ShouldSampleEmissiveSurfaces() && any(surface.Emissive > 0.0f))
            ctx.RecordEmissiveLightSample(surface.Emissive, currentRab, ptRandContext.initialRandomSamplerState);

        if (ctx.ShouldSampleNee())
        {
            RAB_RecordDirectionalNee(ctx, currentRab, material, bsdf, ptRandContext);
            RAB_RecordPointNee(ctx, currentRab, material, instance, bsdf, ptRandContext);
        }

        BSDFSample bsdfSample;
        uint bsdfSeed = RAB_RngToUint(ptRandContext.replayRandomSamplerState);
        bool validSample = bsdf.SampleBSDF(brdfContext, material, surface, bsdfSample, bsdfSeed);
        if (!validSample || bsdfSample.pdf <= 0.0f)
            break;

        RAB_SetBrdfRaySampleFromBSDF(ctx, bsdfSample);

        if (ctx.ShouldRunRussianRoulette() && Raytracing.RussianRoulette == 1 && ctx.GetBounceDepth() > 3)
        {
            float rrProb = saturate(1.0f - Color::RGBToLuminance(ctx.GetPathThroughput()));
            rrProb *= rrProb;
            ctx.RecordRussianRouletteProbability(1.0f - rrProb);
            if (RAB_RngNext(ptRandContext.initialRandomSamplerState) < rrProb)
                break;
            ctx.MultiplyPathThroughput(1.0f / max(1.0f - rrProb, 1e-4f));
        }

        float3 faceNormalOriented = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.0f ? surface.FaceNormal : -surface.FaceNormal;
        RayDesc ray;
#if USE_SIA_INTERPOLATION
        ray.Origin = OffsetRaySIA(surface.Position, faceNormalOriented, surface.SIAOffset, bsdfSample.isLobe(LobeType::Transmission));
#else
        ray.Origin = OffsetRay(surface.Position, faceNormalOriented, surface.PositionError, bsdfSample.isLobe(LobeType::Transmission));
#endif
        ray.Direction = bsdfSample.wo;
        ray.TMin = 0.0f;
        ray.TMax = RAY_TMAX;
        ctx.SetContinuationRay(ray);

        if (!ctx.AnalyzePathReconnectibilityBeforeTrace())
            break;

        Payload payload = TraceRayStandard(Scene, ray, bsdfSeed);
        ctx.SetTraceResult(RAB_MakeRayPayload(payload));

        if (!payload.Hit())
        {
            ctx.RecordPathRadianceMiss(ptRandContext.initialRandomSamplerState);
            float3 skyRadiance = SampleSky(SkyHemisphere, ray.Direction) * Raytracing.Sky;
            ctx.RecordEnvironmentMapLightSample(skyRadiance, currentRab, ptRandContext.initialRandomSamplerState);
            break;
        }

        float3 hitPos = ray.Origin + ray.Direction * payload.hitDistance;
        rayCone = rayCone.propagateDistance(payload.hitDistance);
        if (!bsdfSample.isLobe(LobeType::Delta))
            rayCone = RayCone::make(rayCone.getWidth(), min(rayCone.getSpreadAngle() + ComputeRayConeSpreadAngleExpansionByScatterPDF(bsdfSample.pdf), 2.0 * K_PI));

        rab = RAB_MakeSurfaceFromPayload(hitPos, payload, ray.Direction, rayCone, false);
        ctx.RecordPathIntersection(rab);

        surface = rab.surface;
        material = Materials[NonUniformResourceIndex(rab.materialIndex)];
        instance = Instances[NonUniformResourceIndex(rab.instanceIndex)];
        brdfContext = rab.brdfContext;
        isEnter = dot(surface.FaceNormal, brdfContext.ViewDirection) >= 0.0f;
        bsdf = StandardBSDF::make(surface, isEnter);
        ctx.IncreaseBounceDepth();
    }
}

#endif // RTXDI_PT_PATH_TRACE_BRIDGE_HLSLI
