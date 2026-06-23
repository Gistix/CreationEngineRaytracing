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

struct RAB_PTTraceResult
{
    Payload payload;
    bool frontFace;
};

RAB_PTTraceResult RAB_TraceRayStandardForPT(RaytracingAccelerationStructure scene, RayDesc ray, inout uint randomSeed)
{
    RAB_PTTraceResult result;
    result.payload.Init(randomSeed);
    result.frontFace = true;

#if USE_RAY_QUERY
    RayQuery<RAY_FLAGS> rayQuery;
    rayQuery.TraceRayInline(scene, RAY_FLAG_NONE, INSTANCE_MASK, ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (ConsiderTransparentMaterial(
                rayQuery.CandidateInstanceIndex(),
                rayQuery.CandidateGeometryIndex(),
                rayQuery.CandidatePrimitiveIndex(),
                rayQuery.CandidateTriangleBarycentrics(),
                randomSeed))
            {
                rayQuery.CommitNonOpaqueTriangleHit();
            }
        }
    }

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        result.payload.SetCommittedHit(
            rayQuery.CommittedRayT(),
            rayQuery.CommittedPrimitiveIndex(),
            rayQuery.CommittedTriangleBarycentrics(),
            rayQuery.CommittedInstanceIndex(),
            rayQuery.CommittedGeometryIndex());
        result.frontFace = rayQuery.CommittedTriangleFrontFace();
    }
#else
    result.payload = TraceRayStandard(scene, ray, randomSeed);
#endif

    randomSeed = result.payload.randomSeed;
    return result;
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

bool RAB_SampleBSDFWithRtxdiRng(
    StandardBSDF standardBsdf,
    BRDFContext brdfContext,
    Material material,
    Surface surface,
    out BSDFSample result,
    inout RTXDI_RandomSamplerState rng)
{
    float4 preGeneratedSamples = float4(
        RTXDI_GetNextRandom(rng),
        RTXDI_GetNextRandom(rng),
        RTXDI_GetNextRandom(rng),
        RTXDI_GetNextRandom(rng));

    float3 wi = brdfContext.ViewDirection;
    float3 N = surface.Normal;
    float3 wiLocal = surface.ToLocal(wi);

#if HAIR_MODE == HAIR_MODE_CHIANG_BSDF
    if (material.Feature == Feature::kHairTint)
    {
        HairChiangBSDF bsdf = HairChiangBSDF::make(wi, surface);

        float3 woLocal;
        bool valid = bsdf.SampleBSDF(wiLocal, woLocal, result.pdf, result.weight, result.lobe, result.lobeP, preGeneratedSamples);

        result.wo = surface.FromLocal(woLocal);
        return valid;
    } else
#elif HAIR_MODE == HAIR_MODE_FARFIELD_BCSDF
    if (material.Feature == Feature::kHairTint)
    {
        HairFarFieldBCSDF bsdf = HairFarFieldBCSDF::make(wi, surface);
        const float h = 2.0f * RTXDI_GetNextRandom(rng) - 1.0f;
        float lobeRandom = RTXDI_GetNextRandom(rng);

        float3 woLocal;
        bool valid = bsdf.SampleBSDF(wiLocal, h, woLocal, result.pdf, result.weight, result.lobe, result.lobeP, lobeRandom, preGeneratedSamples);

        result.wo = surface.FromLocal(woLocal);
        return valid;
    } else
#endif
    {
        DefaultBSDF bsdf = DefaultBSDF::make(N, wi, surface, standardBsdf.isEnter);

        float3 woLocal;
        bool valid = bsdf.SampleBSDF(wiLocal, woLocal, result.pdf, result.weight, result.lobe, result.lobeP, preGeneratedSamples);

        result.wo = surface.FromLocal(woLocal);
        return valid;
    }
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
    if (!RAB_IsDirectionUsableForOpaqueReflection(rab, lightDir))
        return false;

    float3 reflected = EvalLight(lightDir, material, rab.surface, rab.brdfContext, bsdf) * lightSample.radiance;
    if (!any(reflected > MIN_DIFFUSE_SHADOW))
        return false;

    reflected *= RAB_ApplyLightVisibility(rab, lightSample);

    RTXDI_SampledLightData sampledLightData = RTXDI_SampledLightData_CreateInvalidData();
    RTXDI_SampledLightData_SetLightData(sampledLightData, RAB_PackDirectionalLightIndex());
    RTXDI_SampledLightData_SetUVData(sampledLightData, float2(0.5, 0.5));

    float solidAnglePdf = max(lightSample.solidAnglePdf, 1e-8f);
    float selectionPdf = RAB_LightSampleSelectionPdf(lightSample, rab);
    float neePdf = max(selectionPdf * solidAnglePdf, 1e-8f);
    float scatterPdf = RAB_SurfaceEvaluateBrdfPdf(rab, lightDir);
    float3 contributionOverPdf = reflected / neePdf;
    contributionOverPdf *= RAB_GetMISWeightForNEE(RAB_PackDirectionalLightIndex(), lightSample, lightDir, neePdf, scatterPdf);
    return ctx.RecordNeeLightSample(sampledLightData, contributionOverPdf, neePdf, scatterPdf, lightSample, ptRandContext.initialRandomSamplerState);
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
    if (!RAB_IsDirectionUsableForOpaqueReflection(rab, lightDir))
        return false;

    float3 reflected = EvalLight(lightDir, material, rab.surface, rab.brdfContext, bsdf) * lightSample.radiance;
    if (!any(reflected > MIN_DIFFUSE_SHADOW))
        return false;

    reflected *= RAB_ApplyLightVisibility(rab, lightSample);

    RTXDI_SampledLightData sampledLightData = RTXDI_SampledLightData_CreateInvalidData();
    RTXDI_SampledLightData_SetLightData(sampledLightData, RAB_PackPointLightIndex(lightIndex));
    RTXDI_SampledLightData_SetUVData(sampledLightData, float2(0.5, 0.5));

    float solidAnglePdf = max(lightSample.solidAnglePdf, 1e-8f);
    float selectionPdf = RAB_LightSampleSelectionPdf(lightSample, rab);
    float neePdf = max(selectionPdf * solidAnglePdf, 1e-8f);
    float scatterPdf = RAB_SurfaceEvaluateBrdfPdf(rab, lightDir);
    float3 contributionOverPdf = reflected / neePdf;
    contributionOverPdf *= RAB_GetMISWeightForNEE(RAB_PackPointLightIndex(lightIndex), lightSample, lightDir, neePdf, scatterPdf);
    return ctx.RecordNeeLightSample(sampledLightData, contributionOverPdf, neePdf, scatterPdf, lightSample, ptRandContext.initialRandomSamplerState);
}

bool RAB_RecordSkyNee(
    inout RTXDI_PathTracerContext ctx,
    RAB_Surface rab,
    Material material,
    StandardBSDF bsdf,
    inout RTXDI_PathTracerRandomContext ptRandContext)
{
    float2 uv = float2(
        RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState),
        RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState));

    RAB_LightInfo lightInfo;
    lightInfo.lightType = RAB_LIGHT_TYPE_SKY;
    lightInfo.lightIndex = 0;
    RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, rab, uv);
    if (!any(lightSample.radiance > 0.0f) || lightSample.solidAnglePdf <= 0.0f)
        return false;

    float3 lightDir = normalize(lightSample.position - rab.surface.Position);
    if (!RAB_IsDirectionUsableForOpaqueReflection(rab, lightDir))
        return false;

    float3 reflected = EvalLight(lightDir, material, rab.surface, rab.brdfContext, bsdf) * lightSample.radiance;
    if (!any(reflected > MIN_DIFFUSE_SHADOW))
        return false;

    reflected *= RAB_ApplyLightVisibility(rab, lightSample);

    RTXDI_SampledLightData sampledLightData = RTXDI_SampledLightData_CreateInvalidData();
    RTXDI_SampledLightData_SetLightData(sampledLightData, RAB_PackSkyLightIndex());
    RTXDI_SampledLightData_SetUVData(sampledLightData, uv);

    float neePdf = max(lightSample.solidAnglePdf, 1e-8f);
    float scatterPdf = RAB_SurfaceEvaluateBrdfPdf(rab, lightDir);
    float3 contributionOverPdf = reflected / neePdf;
    contributionOverPdf *= RAB_GetMISWeightForNEE(RAB_PackSkyLightIndex(), lightSample, lightDir, neePdf, scatterPdf);
    return ctx.RecordNeeLightSample(sampledLightData, contributionOverPdf, neePdf, scatterPdf, lightSample, ptRandContext.initialRandomSamplerState);
}

void RAB_PathTrace(inout RTXDI_PathTracerContext ctx, inout RTXDI_PathTracerRandomContext ptRandContext, inout RAB_PathTracerUserData ptud)
{
    RAB_Surface rab = ctx.GetIntersectionSurface();
    if (!RAB_IsSurfaceValid(rab))
        return;

    ctx.MultiplyPathThroughput(rab.pathThroughput);

    RayCone rayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * max(rab.viewDepth, 1.0f), Raytracing.PixelConeSpreadAngle);

    [loop]
    while (ctx.GetBounceDepth() <= ctx.GetMaxPathBounce())
    {
        ctx.BeginPathState();
        if (ctx.IsPathTerminated())
            break;

        RAB_Surface currentRab = rab;
        Surface surface = currentRab.surface;
        Material material = GetMaterial(currentRab.materialIndex);
        BRDFContext brdfContext = currentRab.brdfContext;
        StandardBSDF bsdf = StandardBSDF::make(surface, currentRab.isEnter);

        currentRab.surface = surface;
        currentRab.brdfContext = brdfContext;
        currentRab.materialFeature = material.Feature;
        currentRab.hasSpecularTransmission = surface.SpecTrans > 0.0f;
        currentRab.isEnter = rab.isEnter;

        if (ctx.GetBounceDepth() == 2 && ctx.ShouldSampleNee())
            RAB_RecordSkyNee(ctx, currentRab, material, bsdf, ptRandContext);

        BSDFSample bsdfSample;
        bool validSample = RAB_SampleBSDFWithRtxdiRng(bsdf, brdfContext, material, surface, bsdfSample, ptRandContext.replayRandomSamplerState);
        bool isDeltaSample = bsdfSample.isLobe(LobeType::Delta);
        if (!validSample || (!isDeltaSample && bsdfSample.pdf <= 0.0f))
            break;

        if (!RAB_IsDirectionUsableForOpaqueReflection(currentRab, bsdfSample.wo))
        {
            ctx.SetPathTermination(true);
            break;
        }

        RAB_SetBrdfRaySampleFromBSDF(ctx, bsdfSample);
        if (!ctx.ValidContinuationRayBrdfOverPdf())
        {
            ctx.SetPathTermination(true);
            break;
        }

        ctx.MultiplyPathThroughput(ctx.GetContinuationRayBrdfOverPdf());

        if (ctx.ShouldRunRussianRoulette() && Raytracing.RussianRoulette == 1 && ctx.GetBounceDepth() > 3)
        {
            float rrProb = saturate(1.0f - Color::RGBToLuminance(ctx.GetPathThroughput()));
            rrProb *= rrProb;
            ctx.RecordRussianRouletteProbability(1.0f - rrProb);
            if (RAB_RngNext(ptRandContext.initialRandomSamplerState) < rrProb)
            {
                ctx.SetPathTermination(true);
                break;
            }
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
        {
            ctx.SetPathTermination(true);
            break;
        }

        uint traceSeed = RAB_RngToUint(ptRandContext.replayRandomSamplerState);
        RAB_PTTraceResult traceResult = RAB_TraceRayStandardForPT(Scene, ray, traceSeed);
        Payload payload = traceResult.payload;
        ctx.SetTraceResult(RAB_MakeRayPayload(payload));

        if (!payload.Hit())
        {
            ctx.RecordPathRadianceMiss(ptRandContext.initialRandomSamplerState);
            float3 skyRadiance = SampleSky(SkyHemisphere, ray.Direction) * Raytracing.Sky;
            RTXDI_BrdfRaySample brs = ctx.GetBrdfRaySample();
            if (!brs.properties.IsDelta())
            {
                float envPdf = RAB_EvaluateSkySamplingPdf(ray.Direction);
                skyRadiance *= RAB_GetMISWeightForEnvironmentMap(skyRadiance, brs.OutPdf, envPdf);
            }
            ctx.RecordEnvironmentMapLightSample(skyRadiance, currentRab, ptRandContext.initialRandomSamplerState);
            break;
        }

        float3 hitPos = ray.Origin + ray.Direction * payload.hitDistance;
        rayCone = rayCone.propagateDistance(payload.hitDistance);
        if (!bsdfSample.isLobe(LobeType::Delta))
            rayCone = RayCone::make(rayCone.getWidth(), min(rayCone.getSpreadAngle() + ComputeRayConeSpreadAngleExpansionByScatterPDF(bsdfSample.pdf), 2.0 * K_PI));

        rab = RAB_MakeSurfaceFromPayload(hitPos, payload, ray.Direction, rayCone, false);
        ctx.RecordPathIntersection(rab);
        if (ctx.IsPathTerminated())
            break;

        Surface hitSurface = rab.surface;
        Material hitMaterial = GetMaterial(rab.materialIndex);
        Instance hitInstance = Instances[NonUniformResourceIndex(rab.instanceIndex)];
        BRDFContext hitBrdfContext = rab.brdfContext;
        StandardBSDF hitBsdf = StandardBSDF::make(hitSurface, rab.isEnter);

        rab.surface = hitSurface;
        rab.brdfContext = hitBrdfContext;
        rab.materialFeature = hitMaterial.Feature;
        rab.hasSpecularTransmission = hitSurface.SpecTrans > 0.0f;

        bool hitIsTwoSided = RAB_IsTwoSidedMaterial(rab);
        if (ctx.ShouldSampleEmissiveSurfaces() && (traceResult.frontFace || hitIsTwoSided) && any(hitSurface.Emissive > 0.0f))
            ctx.RecordEmissiveLightSample(hitSurface.Emissive, currentRab, ptRandContext.initialRandomSamplerState);

        if (ctx.ShouldSampleNee())
        {
            RAB_RecordDirectionalNee(ctx, rab, hitMaterial, hitBsdf, ptRandContext);
            RAB_RecordSkyNee(ctx, rab, hitMaterial, hitBsdf, ptRandContext);
            RAB_RecordPointNee(ctx, rab, hitMaterial, hitInstance, hitBsdf, ptRandContext);
        }

        ctx.IncreaseBounceDepth();
    }
}

#endif // RTXDI_PT_PATH_TRACE_BRIDGE_HLSLI
