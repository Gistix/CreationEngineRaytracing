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

// Light 0 is always valid (sun, or the brighter moon at night). Light 1 is the second moon at night.
bool HasSecondDirectionalLight()
{
    return Raytracing.NumDirectionalLights > 1;
}

Light GetDirectionalLight(uint index)
{
    if (index == 0)
        return Raytracing.DirectionalLight;
    return Raytracing.DirectionalLight2;
}

// Unshadowed irradiance of a directional light, excluding the sampled direction.
// Color/Fade are the light's own colour and intensity (see SceneTLAS).
float3 GetDirectionalLightBaseIrradiance(Light light)
{
    return DirLightToLinear(light.Color) * light.Fade * EvalSkyOcclusion(SKY_HEMI, light.Direction, Features.CloudShadows.Opacity);
}

// Picks one of up to three candidates proportionally to their weights. pdf is 0 when every weight is 0.
uint SampleLightCandidate(float3 weights, inout uint randomSeed, out float pdf)
{
    const float total = weights.x + weights.y + weights.z;
    if (total <= 0.0f)
    {
        pdf = 0.0f;
        return 0;
    }

    const float r = Random(randomSeed) * total;

    uint index = 2;
    if (r < weights.x || (weights.y + weights.z) <= 0.0f)
        index = 0;
    else if (r < weights.x + weights.y || weights.z <= 0.0f)
        index = 1;

    pdf = weights[index] / total;
    return index;
}

void GetDirectionalLightIrradiance(uint index, out float3 irradiance, out float3 lr, inout uint randomSeed)
{
    const Light light = GetDirectionalLight(index);

    irradiance = GetDirectionalLightBaseIrradiance(light);

    // Sun angular radius is ~0.00465 radians (~0.266 degrees)
    float cosSunDisk = cos(0.00465f);
    lr = TangentToWorld(light.Direction, SampleConeUniform(randomSeed, cosSunDisk));

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

    const float cosSunDisk = cos(0.00465f);
    const uint numDirectionalLights = HasSecondDirectionalLight() ? 2 : 1;

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

        // --- Directional Lights (Sun / Moons) ---
        for (uint d = 0; d < numDirectionalLights; d++)
        {
            const Light dirLight = GetDirectionalLight(d);
            float cosDelta = dot(deltaDir, dirLight.Direction);

            if (cosDelta >= cosSunDisk)
            {
                float3 irradiance = GetDirectionalLightBaseIrradiance(dirLight);

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

// Direct lighting from the directional light(s) and one sampled point light.
// Primary hits: one shadow ray towards a directional light (when both moons are active, one is picked proportionally
// to its unshadowed contribution) plus one towards the point light.
// Secondary hits: a single shadow ray, towards one candidate (moon/sun, second moon or point light) picked
// proportionally to its unshadowed contribution.
float3 EvaluateDirectRadiance(in uint16_t type, in uint16_t feature, in Surface surface, in BRDFContext brdfContext, in Instance instance, in StandardBSDF bsdf, inout uint randomSeed, bool isPrimary)
{
    const float directionalMult = isPrimary ? 1.0f : Raytracing.Directional;
    const float pointMult = isPrimary ? 1.0f : Raytracing.Point;

    float3 dir0Irradiance;
    float3 dir0L;
    GetDirectionalLightIrradiance(0, dir0Irradiance, dir0L, randomSeed);
    const float3 dir0Direct = EvalLight(dir0L, type, feature, surface, brdfContext, bsdf) * dir0Irradiance * directionalMult;

    float3 dir1Direct = 0.0f;
    float3 dir1L = 0.0f;
    [branch]
    if (HasSecondDirectionalLight())
    {
        float3 dir1Irradiance;
        GetDirectionalLightIrradiance(1, dir1Irradiance, dir1L, randomSeed);
        dir1Direct = EvalLight(dir1L, type, feature, surface, brdfContext, bsdf) * dir1Irradiance * directionalMult;
    }

    float3 pointIrradiance;
    float3 pointDir;
    float pointDist;
    int lightIndex = GetPointLightIrradiance(instance.LightData, surface, pointIrradiance, pointDir, pointDist, randomSeed);

    float3 pointDirect = 0.0f;
    if (lightIndex >= 0)
        pointDirect = EvalLight(pointDir, type, feature, surface, brdfContext, bsdf) * pointIrradiance * pointMult;

    const float pointWeight = isPrimary ? 0.0f : Luminance(pointDirect);

    float pdf;
    const uint candidate = SampleLightCandidate(float3(Luminance(dir0Direct), Luminance(dir1Direct), pointWeight), randomSeed, pdf);

    float3 radiance = 0.0f;

    [branch]
    if (pdf > 0.0f)
    {
        [branch]
        if (candidate < 2)
        {
            const float3 dirDirect = candidate == 0 ? dir0Direct : dir1Direct;
            const float3 dirL = candidate == 0 ? dir0L : dir1L;

            [branch]
            if (any(dirDirect > MIN_DIFFUSE_SHADOW))
                radiance += dirDirect * (TraceRayShadow(Scene, surface, dirL, randomSeed) / pdf);
        }
        else if (any(pointDirect > MIN_DIFFUSE_SHADOW))
        {
            radiance += pointDirect * (TraceRayShadowFinite(LIGHT_TLAS, surface, pointDir, pointDist, randomSeed) / pdf);
        }
    }

    // Primary hits always trace the point light in addition to the directional light
    [branch]
    if (isPrimary && any(pointDirect > MIN_DIFFUSE_SHADOW))
        radiance += pointDirect * TraceRayShadowFinite(LIGHT_TLAS, surface, pointDir, pointDist, randomSeed);

    return radiance;
}

void EvaluateDirectRadiance(in uint16_t type, in uint16_t feature, in Surface surface, in BRDFContext brdfContext, in Instance instance, in StandardBSDF bsdf, inout uint randomSeed, bool isPrimary, out float3 outDiffuse, out float3 outSpecular)
{
    const float directionalMult = isPrimary ? 1.0f : Raytracing.Directional;
    const float pointMult = isPrimary ? 1.0f : Raytracing.Point;

    float3 dir0Irradiance;
    float3 dir0L;
    GetDirectionalLightIrradiance(0, dir0Irradiance, dir0L, randomSeed);
    float3 dir0Diffuse, dir0Specular;
    EvalLight(dir0L, type, feature, surface, brdfContext, bsdf, dir0Diffuse, dir0Specular);
    dir0Irradiance *= directionalMult;
    dir0Diffuse *= dir0Irradiance;
    dir0Specular *= dir0Irradiance;

    float3 dir1Diffuse = 0.0f;
    float3 dir1Specular = 0.0f;
    float3 dir1L = 0.0f;
    [branch]
    if (HasSecondDirectionalLight())
    {
        float3 dir1Irradiance;
        GetDirectionalLightIrradiance(1, dir1Irradiance, dir1L, randomSeed);
        EvalLight(dir1L, type, feature, surface, brdfContext, bsdf, dir1Diffuse, dir1Specular);
        dir1Irradiance *= directionalMult;
        dir1Diffuse *= dir1Irradiance;
        dir1Specular *= dir1Irradiance;
    }

    float3 pointIrradiance;
    float3 pointDir;
    float pointDist;
    int lightIndex = GetPointLightIrradiance(instance.LightData, surface, pointIrradiance, pointDir, pointDist, randomSeed);

    float3 pointDiffuse = 0.0f;
    float3 pointSpecular = 0.0f;
    if (lightIndex >= 0)
    {
        EvalLight(pointDir, type, feature, surface, brdfContext, bsdf, pointDiffuse, pointSpecular);
        pointIrradiance *= pointMult;
        pointDiffuse *= pointIrradiance;
        pointSpecular *= pointIrradiance;
    }

    const float pointWeight = isPrimary ? 0.0f : Luminance(pointDiffuse + pointSpecular);

    float pdf;
    const uint candidate = SampleLightCandidate(float3(Luminance(dir0Diffuse + dir0Specular), Luminance(dir1Diffuse + dir1Specular), pointWeight), randomSeed, pdf);

    outDiffuse = 0.0f;
    outSpecular = 0.0f;

    [branch]
    if (pdf > 0.0f)
    {
        [branch]
        if (candidate < 2)
        {
            const float3 dirDiffuse = candidate == 0 ? dir0Diffuse : dir1Diffuse;
            const float3 dirSpecular = candidate == 0 ? dir0Specular : dir1Specular;
            const float3 dirL = candidate == 0 ? dir0L : dir1L;

            [branch]
            if (any((dirDiffuse + dirSpecular) > MIN_DIFFUSE_SHADOW))
            {
                float3 shadow = TraceRayShadow(Scene, surface, dirL, randomSeed) / pdf;
                outDiffuse += dirDiffuse * shadow;
                outSpecular += dirSpecular * shadow;
            }
        }
        else if (any((pointDiffuse + pointSpecular) > MIN_DIFFUSE_SHADOW))
        {
            float3 shadow = TraceRayShadowFinite(LIGHT_TLAS, surface, pointDir, pointDist, randomSeed) / pdf;
            outDiffuse += pointDiffuse * shadow;
            outSpecular += pointSpecular * shadow;
        }
    }

    // Primary hits always trace the point light in addition to the directional light
    [branch]
    if (isPrimary && any((pointDiffuse + pointSpecular) > MIN_DIFFUSE_SHADOW))
    {
        float3 shadow = TraceRayShadowFinite(LIGHT_TLAS, surface, pointDir, pointDist, randomSeed);
        outDiffuse += pointDiffuse * shadow;
        outSpecular += pointSpecular * shadow;
    }
}

void GetLightIrradianceMIS(in Instance instance, in Surface surface, out float3 irradiance, out float3 lr, out float distance, inout uint randomSeed)
{
    float3 directionalIrradiance;
    float3 dirLr;
    GetDirectionalLightIrradiance(0, directionalIrradiance, dirLr, randomSeed);

    // Two moons: pick one proportionally to its irradiance
    [branch]
    if (HasSecondDirectionalLight())
    {
        float3 dir1Irradiance;
        float3 dir1Lr;
        GetDirectionalLightIrradiance(1, dir1Irradiance, dir1Lr, randomSeed);

        float dirPdf;
        const uint dirIndex = SampleLightCandidate(float3(Luminance(directionalIrradiance), Luminance(dir1Irradiance), 0.0f), randomSeed, dirPdf);
        if (dirIndex == 1)
        {
            directionalIrradiance = dir1Irradiance;
            dirLr = dir1Lr;
        }
        directionalIrradiance = dirPdf > 0.0f ? directionalIrradiance / dirPdf : 0.0f;
    }

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