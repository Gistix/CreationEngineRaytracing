#ifndef LIGHTING_HLSL
#define LIGHTING_HLSL

#include "include/Common/Game.hlsli"
#include "include/Common/BRDF.hlsli"

#include "raytracing/Include/AdvancedSettings.hlsli"

#include "raytracing/include/Common.hlsli"
#include "include/ColorConversions.hlsli"
#include "raytracing/include/Rays.hlsli"
#include "raytracing/include/MonteCarlo.hlsli"
#include "include/Surface.hlsli"

#include "raytracing/include/Materials/BSDF.hlsli"

#include "interop/Light.hlsli"

static const float ISL_SCALE = 0.8f;
static const float ISL_METRES_TO_UNITS = 70.f;
static const float ISL_METRES_TO_UNITS_SQ = ISL_METRES_TO_UNITS * ISL_METRES_TO_UNITS;
static const float ISL_SCALED_UNITS_SQ = ISL_SCALE * ISL_METRES_TO_UNITS_SQ;
static const float RCP_ISL_SCALED_UNITS_SQ = rcp(ISL_SCALED_UNITS_SQ);

#define DIRECTIONAL_LIGHT Raytracing.DirectionalLight
#define SKY_HEMI SkyHemisphere

float ShadowTerminatorTerm(float3 L, float3 N, float3 Ns)
{
	// Disney terminator softening:
	// "Taming the Shadow Terminator"
	// Matt Jen-Yuan Chiang, Yining Karl Li, and Brent Burley
	// SIGGRAPH 2019 Talks
	// https://www.yiningkarlli.com/projects/shadowterminator.html
	const float NoL = saturate(dot(N, L));
	const float NgoL = saturate(dot(Ns, L));
	const float NgoN = saturate(dot(Ns, N));
	const float G = saturate(NgoL / (NoL * NgoN + 1e-6));
	return G + G * (G - G * G); // smooth
}

float2 EvalHemiUV(float3 dir)
{
    dir.z = max(dir.z, 0.0f);

    float r = sqrt(1.0f - dir.z);
    float2 disk = dir.xy * (r * rsqrt(max(dot(dir.xy, dir.xy), 1e-30f)));
    return disk * 0.5f + 0.5f;
}

// Samples the sky hemisphere texture based on the given direction
// Output is in true linear space
float3 SampleSky(Texture2D<float4> SkyHemisphere, float3 dir)
{
    float2 uv = EvalHemiUV(dir);

    float3 color = SkyHemisphere.SampleLevel(DefaultSampler, uv, 0.0f).rgb;

    return LLGammaToTrueLinear(color);
}

float EvalSkyOcclusion(Texture2D<float4> SkyHemisphere, float3 dir, float opacity)
{
    if (opacity == 0.0f)
        return 1.0f;

    float2 uv = EvalHemiUV(dir);

    return lerp(1.0f, 1.0f - SkyHemisphere.SampleLevel(DefaultSampler, uv, 0.0f).a, opacity);
}

float3 EvalDiffuse(in float3 l, in Surface surface, in BRDFContext brdfContext)
{
    float NdotL = saturate(dot(surface.Normal, l));

    if (NdotL <= 0.0f)
        return float3(0.0f, 0.0f, 0.0f);

    // Diffuse is meant to be very light (and used with DDGI), so I don't see much point in using a different diffuse or shading model here
    return surface.DiffuseAlbedo * NdotL * BRDF::Diffuse_Lambert();
}

float3 EvalLight(in float3 l, in uint16_t type, in uint16_t feature, in Surface surface, in BRDFContext brdfContext, in StandardBSDF bsdf)
{
#if LIGHTEVAL_MODE == LIGHTEVAL_MODE_DIFFUSE
    return EvalDiffuse(l, surface, brdfContext);
#else
    float shadowTerminator = dot(surface.FaceNormal, l) > 0.0f ? ShadowTerminatorTerm(l, surface.Normal, surface.GeomNormal) : 1.0f;
    float4 bsdfEval = bsdf.Eval(brdfContext, feature, surface, l) * shadowTerminator * (type == Type::TruePBR ? 1.0f : PBRLightingScaleCompensation);
    return bsdfEval.xyz;
#endif
}

void EvalLight(in float3 l, in uint16_t type, in uint16_t feature, in Surface surface, in BRDFContext brdfContext, in StandardBSDF bsdf, out float3 outDiffuse, out float3 outSpecular)
{
#if LIGHTEVAL_MODE == LIGHTEVAL_MODE_DIFFUSE
    outDiffuse = EvalDiffuse(l, surface, brdfContext);
    outSpecular = 0.0f;
#else
    float shadowTerminator = dot(surface.FaceNormal, l) > 0.0f ? ShadowTerminatorTerm(l, surface.Normal, surface.GeomNormal) : 1.0f;
    float scale = shadowTerminator * (type == Type::TruePBR ? 1.0f : PBRLightingScaleCompensation);
    bsdf.EvalDiffuseAndSpecular(brdfContext, feature, surface, l, outDiffuse, outSpecular);
    outDiffuse *= scale;
    outSpecular *= scale;
#endif
}

void GetDirectionalLightIrradiance(out float3 irradiance, out float3 lr, inout uint randomSeed)
{
    irradiance = DirLightToLinear(DIRECTIONAL_LIGHT.Color) * EvalSkyOcclusion(SKY_HEMI, DIRECTIONAL_LIGHT.Direction, Features.CloudShadows.Opacity);

    // Sun angular radius is ~0.00465 radians (~0.266 degrees)
    float cosSunDisk = cos(0.00465f);
    lr = TangentToWorld(DIRECTIONAL_LIGHT.Direction, SampleConeUniform(randomSeed, cosSunDisk));

    // Correct MC weight for uniform cone sampling of a finite-size disk light.
    // Factor = 2/(1+cosα) → 1 for small angles (sun), matters for large celestial bodies.
    irradiance *= 2.0f / (1.0f + cosSunDisk);
}

float GetPointAttenuation(Light light, float dist, inout float lightSourceAngle)
{
    if ((light.Flags & LightFlags::ISL) != 0)
    {
        float size = sqrt((light.SizeBias * 2.0f) * RCP_ISL_SCALED_UNITS_SQ);
        lightSourceAngle = atan2(size, dist);
        
        float invSq = ISL_SCALED_UNITS_SQ * rcp(dist * dist + light.SizeBias);
        float t = saturate((light.Radius - dist) * light.FadeZone);
        float fastSmoothstep = t * t * (3.0f - 2.0f * t);
        return invSq * fastSmoothstep;
    }
    else
    {
        float intensityFactor = saturate(dist * light.InvRadius);
        float attenuation = 1.0f - intensityFactor * intensityFactor;
#if defined(SKYRIM)
        if (LLON)
            attenuation = pow(attenuation, TransferFunctions::GAME_GAMMA);
#endif
        return attenuation;
    }
}

float GetSpotAttenuation(Light light, float3 surfaceToLight)
{
    float cosOuter = light.CosOuterAngle;
    float cosInner = light.CosInnerAngle;

    // light.Direction points from light toward where it aims
    // surfaceToLight points from surface toward light, so negate it
    float cosTheta = dot(normalize(-surfaceToLight), light.Direction);
    return smoothstep(cosOuter, cosInner, cosTheta);
}

float GetAttenuation(Light light, float3 lr, float dist, inout float lightSourceAngle)
{
    if (light.Type == LightType::Directional)
        return 1.0f;
    
    float atten = GetPointAttenuation(light, dist, lightSourceAngle);
    
    if (light.Type == LightType::Spot)
        atten *= GetSpotAttenuation(light, lr);
        
    return atten;
}

float GetLightAngle(Light light, float dist)
{
    if ((light.Flags & LightFlags::ISL) != 0)
    {
        float size = sqrt((light.SizeBias * 2.0f) * RCP_ISL_SCALED_UNITS_SQ);
        return atan2(size, dist);
    }
    return 0.005f;
}

float GetLightSampleWeight(Surface surface, Light light)
{
    float3 l = (light.Position - surface.Position);
    float dist = length(l) * GAME_UNIT_TO_M;
    float3 lr = l / dist;
    float lightSourceAngle = 0.0f;
    float atten = GetAttenuation(light, lr, dist, lightSourceAngle);
    float intensity = max(light.Color.r, max(light.Color.g, light.Color.b)) * light.Fade;
    return atten * intensity;
}

// Get irradiance for point light without BRDF evaluation
int GetPointLightIrradiance(in InstanceLightData lightData, in Surface surface, out float3 irradiance, out float3 lr, out float dist, inout uint randomSeed)
{   
#if defined(GLOBAL_LIGHTS)
    const uint lightCount = Raytracing.NumLights;
#else
    const uint lightCount = lightData.LightCount;
#endif
    
    if (lightCount == 0)
    {
        irradiance = float3(0, 0, 0);
        lr = float3(0, 0, 0);
        dist = 0.0f;
        return -1;
    }

    float lightWeight = float(lightCount);

#if defined(RIS)
#   if defined(GLOBAL_LIGHTS)
    const uint candidateCount = lightCount;
#   else
    const uint candidateCount = min(RIS_MAX_CANDIDATES, lightCount);
#   endif    

    uint selectedLightID = 0;
    float totalWeight = 0.0f;
    float selectedWeight = 0.0f;

    for (uint i = 0; i < candidateCount; i++)
    {
        const uint lightIdx = min(uint(Random(randomSeed) * lightCount), lightCount - 1);
    
#   if defined(GLOBAL_LIGHTS)    
        const uint lightID = lightIdx;
#   else
        const uint lightID = InstanceLightList[lightData.LightOffset + lightIdx];
#   endif
    
        Light testLight = Lights[lightID];
    
        const bool isTestLinear = (testLight.Flags & LightFlags::LinearLight) != 0;
        testLight.Color = (half3)PointLightToLinear(testLight.Color, isTestLinear);
        float weight = GetLightSampleWeight(surface, testLight);
        totalWeight += weight;

        if (Random(randomSeed) * totalWeight < weight)
        {
            selectedLightID = lightID;
            selectedWeight = weight;
        }
    }
    
    if (totalWeight == 0.0f)
    {
        irradiance = float3(0, 0, 0);
        lr = float3(0, 0, 0);
        dist = 0.0f;    
        return -1;
    }

    float risWeight = (totalWeight / max(selectedWeight, 1e-7f)) / float(candidateCount);

    lightWeight *= risWeight;

    Light light = Lights[selectedLightID];
#else

    const uint lightIdx = min(uint(Random(randomSeed) * lightCount), lightCount - 1);
    
#   if defined(GLOBAL_LIGHTS)    
    const uint lightID = lightIdx;
#   else
    const uint lightID = InstanceLightList[lightData.LightOffset + lightIdx];
#   endif
    
    Light light = Lights[lightID];
#endif

    const bool isLinear = (light.Flags & LightFlags::LinearLight) != 0;
    light.Color = (half3)PointLightToLinear(light.Color, isLinear);

    lr = (light.Position - surface.Position);
    dist = length(lr);
    lr /= dist;

    float lightSourceAngle = 0.005f;

    float atten = GetAttenuation(light, lr, dist, lightSourceAngle);
    irradiance = light.Color * light.Fade * atten * lightWeight;
    lr = TangentToWorld(lr, SampleCosineHemisphereScaled(randomSeed, lightSourceAngle));
    
#if defined(RIS)
    return selectedLightID;
#else
    return lightID;
#endif
}

// Evaluate analytical lights for delta lobes specifically.
// For delta (perfect mirror/glass) surfaces, the standard bsdf.Eval() returns 0 for any light direction.
// Instead, we use EvalDeltaLobes to get the exact reflection/refraction directions and check whether
// each delta direction falls within a light source's solid angle. If so, we contribute the delta lobe's
// throughput multiplied by the light irradiance. This correctly handles sun glints, point light
// reflections in mirrors, etc.
float3 EvalDeltaLobeLighting(in Surface surface, in Instance instance, in DeltaLobe deltaLobes[cMaxDeltaLobes],
                              inout uint randomSeed, bool isPrimary)
{
    float3 totalRadiance = 0.0f;

    const float3 sunDir = DIRECTIONAL_LIGHT.Direction;
    const float cosSunDisk = cos(0.00465f);

    bool anyLobeActive = false;
    [unroll]
    for (uint lobe = 0; lobe < cMaxDeltaLobes; lobe++)
        anyLobeActive = anyLobeActive || (deltaLobes[lobe].probability > 0.0f);

    bool hasPointLight = false;
    int lightIndex = -1;
    float3 lightIrradiance = 0.0f;
    float3 dirToLight = 0.0f;
    float lightDist = 0.0f;
    float cosLightAngle = 1.0f;

    if (anyLobeActive && instance.LightData.LightCount > 0)
    {
        float3 lr;
        lightIndex = GetPointLightIrradiance(instance.LightData, surface, lightIrradiance, lr, lightDist, randomSeed);

        if (lightIndex >= 0)
        {
            Light light = Lights[lightIndex];
            cosLightAngle = cos(GetLightAngle(light, lightDist * GAME_UNIT_TO_M));
            dirToLight = normalize(light.Position - surface.Position);
            hasPointLight = true;
        }
    }

    [unroll]
    for (uint i = 0; i < cMaxDeltaLobes; i++)
    {
        if (deltaLobes[i].probability <= 0.0f)
            continue;

        float3 deltaDir = deltaLobes[i].dir;
        float3 deltaThroughput = deltaLobes[i].thp;

        // --- Directional Light (Sun) ---
        {
            float cosDelta = dot(deltaDir, sunDir);

            if (cosDelta >= cosSunDisk)
            {
                float3 irradiance = DirLightToLinear(DIRECTIONAL_LIGHT.Color) * EvalSkyOcclusion(SKY_HEMI, sunDir, Features.CloudShadows.Opacity);

                // irradiance = E_sun (total flux/area from the sun).
                // A delta surface "picks out" the sun's RADIANCE L_sun, not irradiance E_sun.
                // Convert: L_sun = E_sun / Ω_proj, where Ω_proj = π·sin²α = π(1-cos²α)
                // is the projected solid angle of the disk.
                float sunSolidAngle = K_PI * (1.0f - cosSunDisk * cosSunDisk);
                float3 contribution = deltaThroughput * irradiance / sunSolidAngle * (isPrimary ? 1.0f : Raytracing.Directional);
                
                contribution *= TraceRayShadow(Scene, surface, deltaDir, randomSeed);
                
                totalRadiance += contribution;
            }
        }

        // --- Point Lights ---
        if (hasPointLight && dot(deltaDir, dirToLight) >= cosLightAngle)
        {
            float3 contribution = deltaThroughput * lightIrradiance * (isPrimary ? 1.0f : Raytracing.Point);

            // Shadow ray toward the light
#if USE_LIGHT_TLAS
#   define DELTA_LIGHT_TLAS LightTLAS[NonUniformResourceIndex(lightIndex)]
#else
#   define DELTA_LIGHT_TLAS Scene
#endif
            contribution *= TraceRayShadowFinite(DELTA_LIGHT_TLAS, surface, deltaDir, lightDist, randomSeed);

            totalRadiance += contribution;
        }
    }

    return totalRadiance;
}

float3 EvalDeltaLobeLighting(in Surface surface, in BRDFContext brdfContext, in Instance instance,
                              in StandardBSDF bsdf, inout uint randomSeed, bool isPrimary)
{
    DeltaLobe deltaLobes[cMaxDeltaLobes];
    int deltaLobeCount;
    float nonDeltaPart;
    bsdf.EvalDeltaLobes(brdfContext, surface, deltaLobes, deltaLobeCount, nonDeltaPart);

    return EvalDeltaLobeLighting(surface, instance, deltaLobes, randomSeed, isPrimary);
}

#if USE_LIGHT_TLAS
#   define LIGHT_TLAS LightTLAS[NonUniformResourceIndex(lightIndex)]
#else
#   define LIGHT_TLAS Scene
#endif

float3 EvaluateDirectRadiance(in uint16_t type, in uint16_t feature, in Surface surface, in BRDFContext brdfContext, in Instance instance, in StandardBSDF bsdf, inout uint randomSeed, bool isPrimary)
{
    float3 sunIrradiance;
    float3 sunDir;
    GetDirectionalLightIrradiance(sunIrradiance, sunDir, randomSeed);
    float3 sunDirect = EvalLight(sunDir, type, feature, surface, brdfContext, bsdf) * sunIrradiance;

    float3 pointIrradiance;
    float3 pointDir;
    float pointDist;
    int lightIndex = GetPointLightIrradiance(instance.LightData, surface, pointIrradiance, pointDir, pointDist, randomSeed);

    float3 pointDirect = 0.0f;
    if (lightIndex >= 0)
        pointDirect = EvalLight(pointDir, type, feature, surface, brdfContext, bsdf) * pointIrradiance;

    float3 radiance = 0.0f;

    [branch]
    if (any(sunDirect > MIN_DIFFUSE_SHADOW))
        radiance += sunDirect * TraceRayShadow(Scene, surface, sunDir, randomSeed) * (isPrimary ? 1.0f : Raytracing.Directional);

    [branch]
    if (any(pointDirect > MIN_DIFFUSE_SHADOW))
        radiance += pointDirect * TraceRayShadowFinite(LIGHT_TLAS, surface, pointDir, pointDist, randomSeed) * (isPrimary ? 1.0f : Raytracing.Point);

    return radiance;
}

void EvaluateDirectRadiance(in uint16_t type, in uint16_t feature, in Surface surface, in BRDFContext brdfContext, in Instance instance, in StandardBSDF bsdf, inout uint randomSeed, bool isPrimary, out float3 outDiffuse, out float3 outSpecular)
{
    float3 sunIrradiance;
    float3 sunDir;
    GetDirectionalLightIrradiance(sunIrradiance, sunDir, randomSeed);
    float3 sunDiffuse, sunSpecular;
    EvalLight(sunDir, type, feature, surface, brdfContext, bsdf, sunDiffuse, sunSpecular);
    sunDiffuse *= sunIrradiance;
    sunSpecular *= sunIrradiance;

    float3 pointIrradiance;
    float3 pointDir;
    float pointDist;
    int lightIndex = GetPointLightIrradiance(instance.LightData, surface, pointIrradiance, pointDir, pointDist, randomSeed);

    float3 pointDiffuse = 0.0f;
    float3 pointSpecular = 0.0f;
    if (lightIndex >= 0)
    {
        EvalLight(pointDir, type, feature, surface, brdfContext, bsdf, pointDiffuse, pointSpecular);
        pointDiffuse *= pointIrradiance;
        pointSpecular *= pointIrradiance;
    }

    outDiffuse = 0.0f;
    outSpecular = 0.0f;

    [branch]
    if (any((sunDiffuse + sunSpecular) > MIN_DIFFUSE_SHADOW))
    {
        float3 shadow = TraceRayShadow(Scene, surface, sunDir, randomSeed) * (isPrimary ? 1.0f : Raytracing.Directional);
        outDiffuse += sunDiffuse * shadow;
        outSpecular += sunSpecular * shadow;
    }

    [branch]
    if (any((pointDiffuse + pointSpecular) > MIN_DIFFUSE_SHADOW))
    {
        float3 shadow = TraceRayShadowFinite(LIGHT_TLAS, surface, pointDir, pointDist, randomSeed) * (isPrimary ? 1.0f : Raytracing.Point);
        outDiffuse += pointDiffuse * shadow;
        outSpecular += pointSpecular * shadow;
    }
}

void GetLightIrradianceMIS(in Instance instance, in Surface surface, out float3 irradiance, out float3 lr, out float distance, inout uint randomSeed)
{
    float3 directionalIrradiance;
    float3 dirLr;
    GetDirectionalLightIrradiance(directionalIrradiance, dirLr, randomSeed);

    float3 pointIrradiance;
    float3 pointLr;
    float pointDist;
    GetPointLightIrradiance(instance.LightData, surface, pointIrradiance, pointLr, pointDist, randomSeed);

    float3 dirVisibility = TraceRayShadow(Scene, surface, dirLr, randomSeed);

    float pDirLight = Luminance(directionalIrradiance * dirVisibility);
    float pPointLight = Luminance(pointIrradiance);

    float total = pDirLight + pPointLight;
    if (total < 1e-6f)
    {
        irradiance = float3(0, 0, 0);
        lr = float3(0, 0, 0);
        distance = 0.0f;
        return;
    }

    float r = Random(randomSeed);
    pDirLight /= total;
    pPointLight /= total;

    if (r < pDirLight)
    {
        irradiance = directionalIrradiance / pDirLight;
        lr = dirLr;
        distance = SHADOW_RAY_TMAX;
    }
    else
    {
        irradiance = pointIrradiance / pPointLight;
        lr = pointLr;
        distance = pointDist;
    }
}

float3 EvaluateDirectRadianceMIS(in uint16_t type, in uint16_t feature, in Surface surface, in BRDFContext brdfContext, in Instance instance, in StandardBSDF bsdf, inout uint randomSeed)
{
    float3 lightIrradiance;
    float3 lr;
    float distance;
    GetLightIrradianceMIS(instance, surface, lightIrradiance, lr, distance, randomSeed);

    float3 direct = EvalLight(lr, type, feature, surface, brdfContext, bsdf) * lightIrradiance;

    return direct;
}

bool ComputeTangentSpace(inout Surface surface, const bool ignoreTangent)
{
    // Check that tangent space exists and can be safely orthonormalized.
    // Otherwise invent a tanget frame based on the normal.
    // We check that:
    //  - Tangent exists, this is indicated by a nonzero sign (w).
    //  - It has nonzero length. Zeros can occur due to interpolation or bad assets.
    //  - It is not parallel to the normal. This can occur due to normal mapping or bad assets.
    //  - It does not have NaNs. These will propagate and trigger the fallback.

    float NdotT = dot(surface.GeomTangent, surface.Normal);
    bool nonParallel = abs(NdotT) < 0.9999f;
    bool nonZero = dot(surface.GeomTangent, surface.GeomTangent) > 0.f;

    bool valid = nonZero && nonParallel;
    if (!ignoreTangent && valid)
    {
        surface.Tangent = normalize(surface.GeomTangent - surface.Normal * NdotT);
        surface.Bitangent = cross(surface.Normal, surface.Tangent);
    }
    else
    {
        surface.Tangent = perp_stark(surface.Normal);
        surface.Bitangent = cross(surface.Normal, surface.Tangent);
    }

    return valid;
}

void AdjustShadingNormal(inout Surface surface, BRDFContext brdfContext, uniform bool recomputeTangentSpace, const bool ignoreTangent)
{
    float3 Ng = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.f ? surface.FaceNormal : -surface.FaceNormal;
    float signN = dot(surface.Normal, Ng) >= 0.f ? 1.f : -1.f;
    float3 Ns = signN * surface.Normal;

    // Blend the shading normal towards the geometric normal at grazing angles.
    // This is to avoid the view vector from becoming back-facing.
    const float kCosThetaThreshold = 0.1f;
    float cosTheta = dot(brdfContext.ViewDirection, Ns);
    if (cosTheta <= kCosThetaThreshold)
    {
        float t = saturate(cosTheta * (1.f / kCosThetaThreshold));
        surface.Normal = signN * normalize(lerp(Ng, Ns, t));
    }
    if (cosTheta <= kCosThetaThreshold || recomputeTangentSpace)
        ComputeTangentSpace(surface, ignoreTangent);
}

#endif // LIGHTING_HLSL