#ifndef WETNESS_HLSLI
#define WETNESS_HLSLI

// Wetness effects for path tracing, aligned with skyrim-community-shaders Lighting.hlsl
// All bounces: porosity albedo darkening + puddle noise + wet coat lobe
// Primary surface only: raindrop ripples + splashes (complex calculation)

#include "include/Surface.hlsli"
#include "interop/SharedData.hlsli"

namespace Wetness
{
    // ---- Noise helpers (from community shaders Random.hlsli) ----

    uint Rotl(uint x, uint r)
    {
        return (x << r) | (x >> (32u - r));
    }

    uint Fmix(uint h)
    {
        h ^= h >> 16;
        h *= 2246822507u;
        h ^= h >> 13;
        h *= 3266489917u;
        h ^= h >> 16;
        return h;
    }

    uint Murmur3(uint3 x, uint seed = 0)
    {
        static const uint c1 = 3432918353u;
        static const uint c2 = 461845907u;

        uint h = seed;
        uint k = x.x;

        k *= c1; k = Rotl(k, 15u); k *= c2;
        h ^= k; h = Rotl(h, 13u); h = h * 5u + 3864292196u;

        k = x.y;
        k *= c1; k = Rotl(k, 15u); k *= c2;
        h ^= k; h = Rotl(h, 13u); h = h * 5u + 3864292196u;

        k = x.z;
        k *= c1; k = Rotl(k, 15u); k *= c2;
        h ^= k; h = Rotl(h, 13u); h = h * 5u + 3864292196u;

        h ^= 12u;
        return Fmix(h);
    }

    float3 PerlinGradient(uint hash)
    {
        switch (hash & 15u) {
        case 0:  return float3( 1,  1,  0);
        case 1:  return float3(-1,  1,  0);
        case 2:  return float3( 1, -1,  0);
        case 3:  return float3(-1, -1,  0);
        case 4:  return float3( 1,  0,  1);
        case 5:  return float3(-1,  0,  1);
        case 6:  return float3( 1,  0, -1);
        case 7:  return float3(-1,  0, -1);
        case 8:  return float3( 0,  1,  1);
        case 9:  return float3( 0, -1,  1);
        case 10: return float3( 0,  1, -1);
        case 11: return float3( 0, -1, -1);
        case 12: return float3( 1,  1,  0);
        case 13: return float3(-1,  1,  0);
        case 14: return float3( 0, -1,  1);
        case 15: return float3( 0, -1, -1);
        default: return float3( 0,  0,  0);
        }
    }

    // Returns [-1, 1]
    float PerlinNoise(float3 position, uint seed = 0x578437ADu)
    {
        float3 i_f = floor(position);
        float3 f = position - i_f;
        uint3 i = asuint(int3(i_f));
        float v1 = dot(PerlinGradient(Murmur3(i, seed)), f);
        float v2 = dot(PerlinGradient(Murmur3(i + uint3(1,0,0), seed)), f - float3(1,0,0));
        float v3 = dot(PerlinGradient(Murmur3(i + uint3(0,1,0), seed)), f - float3(0,1,0));
        float v4 = dot(PerlinGradient(Murmur3(i + uint3(1,1,0), seed)), f - float3(1,1,0));
        float v5 = dot(PerlinGradient(Murmur3(i + uint3(0,0,1), seed)), f - float3(0,0,1));
        float v6 = dot(PerlinGradient(Murmur3(i + uint3(1,0,1), seed)), f - float3(1,0,1));
        float v7 = dot(PerlinGradient(Murmur3(i + uint3(0,1,1), seed)), f - float3(0,1,1));
        float v8 = dot(PerlinGradient(Murmur3(i + uint3(1,1,1), seed)), f - float3(1,1,1));

        float3 u = f * f * (3.0 - 2.0 * f);
        return lerp(
            lerp(lerp(v1, v2, u.x), lerp(v3, v4, f.x), u.y),
            lerp(lerp(v5, v6, u.x), lerp(v7, v8, f.x), u.y),
            u.z);
    }

    // ---- Water data lookup (5x5 cell grid centered on camera) ----

    // ---- Additional random helpers for raindrop effects ----

    uint3 Pcg3d(uint3 v)
    {
        v = v * 1664525u + 1013904223u;
        v.x += v.y * v.z;
        v.y += v.z * v.x;
        v.z += v.x * v.y;
        v ^= v >> 16u;
        v.x += v.y * v.z;
        v.y += v.z * v.x;
        v.z += v.x * v.y;
        return v;
    }

    uint Iqint3(uint2 x)
    {
        uint2 q = 1103515245U * ((x >> 1U) ^ (x.yx));
        uint n = 1103515245U * ((q.x) ^ (q.y >> 3U));
        return n;
    }

    // ---- Raindrop helpers ----

    float SmoothstepDeriv(float x)
    {
        return 6.0 * x * (1.0 - x);
    }

    float RainFade(float normalised_t)
    {
        const float rain_stay = 0.5;
        if (normalised_t < rain_stay)
            return 1.0;
        float val = lerp(1.0, 0.0, (normalised_t - rain_stay) / (1.0 - rain_stay));
        return val * val;
    }

    // Reorient normal (s = (0,0,1) variant)
    float3 ReorientNormal(float3 n1, float3 n2)
    {
        n1 += float3(0, 0, 1);
        n2 *= float3(-1, -1, 1);
        return n1 * dot(n1, n2) / n1.z - n2;
    }

    // xyz - ripple normal, w - splash wetness
    float4 GetRainDrops(float3 worldPos, float t, float3 normal, float rippleStrengthModifier, WetnessEffectsSettings settings)
    {
        static const float uintToFloat = 1.0 / 4294967295.0;

        float rippleBreadthRcp = rcp(max(settings.RippleBreadth, 1e-6));
        float intervalRcp = settings.RaindropIntervalRcp;
        float lifetimeRcp = settings.RippleLifetimeRcp;

        float2 gridUV = worldPos.xy * settings.RaindropGridSizeRcp + normal.xy;
        int2 grid = int2(floor(gridUV));
        gridUV -= float2(grid);

        float3 rippleNormal = float3(0, 0, 1);
        float wetness = 0.0;

        bool hasEffects = settings.EnableSplashes || settings.EnableRipples;
        if (!hasEffects)
            return float4(rippleNormal, 0.0);

        [unroll]
        for (int i = -1; i <= 1; i++)
        {
            [unroll]
            for (int j = -1; j <= 1; j++)
            {
                int2 gridCurr = grid + int2(i, j);
                float tOffset = float(Iqint3(asuint(gridCurr))) * uintToFloat;

                // Splashes
                if (settings.EnableSplashes)
                {
                    float residual = t * intervalRcp / max(settings.SplashesLifetime, 1e-6) + tOffset + worldPos.z * 0.001;
                    uint timestep = uint(residual);
                    residual -= timestep;

                    uint3 hash = Pcg3d(uint3(asuint(gridCurr), timestep));
                    float3 floatHash = float3(hash) * uintToFloat;

                    if (floatHash.z < settings.RaindropChance)
                    {
                        float2 vec2Centre = int2(i, j) + floatHash.xy - gridUV;
                        float distSqr = dot(vec2Centre, vec2Centre);
                        float dropRadius = lerp(settings.SplashesMinRadius, settings.SplashesMaxRadius,
                            float(Iqint3(hash.yz)) * uintToFloat);
                        if (distSqr < dropRadius * dropRadius)
                            wetness = max(wetness, RainFade(residual));
                    }
                }

                // Ripples
                if (settings.EnableRipples)
                {
                    float residual = t * intervalRcp + tOffset + worldPos.z * 0.001;
                    uint timestep = uint(residual);
                    residual -= timestep;

                    uint3 hash = Pcg3d(uint3(asuint(gridCurr), timestep));
                    float3 floatHash = float3(hash) * uintToFloat;

                    if (floatHash.z < settings.RaindropChance)
                    {
                        float2 vec2Centre = int2(i, j) + floatHash.xy - gridUV;
                        float distSqr = dot(vec2Centre, vec2Centre);
                        float rippleT = residual * lifetimeRcp;

                        if (rippleT < 1.0)
                        {
                            uint sizeHash = Iqint3(hash.xy);
                            float sizeVariation = lerp(0.7, 1.3, float(sizeHash) * uintToFloat);

                            float rippleRadius = settings.RippleRadius * sizeVariation;
                            float rippleR = lerp(0.0, rippleRadius, rippleT);
                            float rippleInnerRadius = rippleR - settings.RippleBreadth;

                            float bandLerp = (sqrt(distSqr) - rippleInnerRadius) * rippleBreadthRcp;
                            if (bandLerp > 0.0 && bandLerp < 1.0)
                            {
                                float rippleStr = settings.RippleStrength * rippleStrengthModifier;
                                float deriv = (bandLerp < 0.5 ? SmoothstepDeriv(bandLerp * 2.0) : -SmoothstepDeriv(2.0 - bandLerp * 2.0)) *
                                              lerp(rippleStr, 0.0, rippleT * rippleT);

                                float3 grad = float3(normalize(vec2Centre), -deriv);
                                float3 bitangent = float3(-grad.y, grad.x, 0.0);
                                float3 n = normalize(cross(grad, bitangent));

                                rippleNormal = ReorientNormal(n, rippleNormal);
                            }
                        }
                    }
                }
            }
        }

        return float4(rippleNormal, wetness * settings.SplashesStrength);
    }

    float4 GetWaterData(float3 worldPosition, float3 cameraPosition, float4 waterDataGrid[25])
    {
        float2 cellF = ((worldPosition.xy + cameraPosition.xy) / 4096.0) + 64.0;
        int2 cellInt;
        float2 cellFrac;
        cellFrac = frac(cellF);
        cellInt = int2(floor(cellF));

        cellF = worldPosition.xy / float2(4096.0, 4096.0);
        cellF += 2.5;
        cellF -= cellFrac;
        cellInt = int2(round(cellF));

        uint waterTile = (uint)clamp(cellInt.x + cellInt.y * 5, 0, 24);

        float4 waterData = float4(1.0, 1.0, 1.0, -2147483648);
        [flatten] if (cellInt.x < 5 && cellInt.x >= 0 && cellInt.y < 5 && cellInt.y >= 0)
            waterData = waterDataGrid[waterTile];
        return waterData;
    }

    // ---- Wetness parameters ----

    struct WetnessParams
    {
        float wetness;                // Overall wetness amount [0,1]
        float wetnessGlossinessAlbedo; // For albedo darkening
        float waterRoughness;         // Roughness for the wet coat layer
        float3 wetnessNormal;         // Normal for the wet layer
        bool isSkinned;               // Whether this is a skinned mesh
    };

    // Compute wetness parameters matching community shaders approach
    WetnessParams ComputeWetness(
        float3 worldPosition,
        float3 vertexNormal,   // Geometric/vertex normal (upward-facing check)
        float3 surfaceNormal,  // After normal mapping
        float3 cameraPosition,
        float4 waterDataGrid[25],
        WetnessEffectsSettings settings,
        bool isSkinned,
        bool isPrimary)
    {
        WetnessParams params;
        params.wetness = 0;
        params.wetnessGlossinessAlbedo = 0;
        params.waterRoughness = 1;
        params.wetnessNormal = vertexNormal;
        params.isSkinned = isSkinned;

        if (!settings.EnableWetnessEffects)
            return params;

        float4 waterData = GetWaterData(worldPosition, cameraPosition, waterDataGrid);
        float waterHeight = waterData.w;

        // Shore wetness
        float wetnessDistToWater = abs(worldPosition.z - waterHeight);
        float shoreFactor = saturate(1.0 - (wetnessDistToWater / max((float)settings.ShoreRange, 1.0)));
        float shoreFactorAlbedo = (worldPosition.z < waterHeight) ? 1.0 : shoreFactor;

        // Rain angle and wetness
        float minWetnessValue = settings.MinRainWetness;
        float minWetnessAngle = saturate(max(minWetnessValue, vertexNormal.z));
        float flatnessAmount = smoothstep(settings.PuddleMaxAngle, 1.0, minWetnessAngle);

        // Raindrop effects (complex calculation, primary surface only)
        float4 raindropInfo = float4(0, 0, 1, 0);
        if (isPrimary && (vertexNormal.z > 0.0) && (settings.Raining > 0.0) && settings.EnableRaindropFx)
        {
            float3 ripplePosition = worldPosition.xyz + cameraPosition.xyz;
            raindropInfo = GetRainDrops(ripplePosition, settings.Time, vertexNormal, flatnessAmount, settings);
        }

        float rainWetness = settings.Wetness * minWetnessAngle * settings.MaxRainWetness;
        rainWetness = max(rainWetness, raindropInfo.w);

        if (isSkinned)
            rainWetness = settings.SkinWetness * settings.Wetness;

        float shoreWetness = shoreFactor * settings.MaxShoreWetness;
        float wetness = max(shoreWetness, rainWetness);

        // Puddle effects (basic calculation, applies on all bounces)
        float puddleWetness = settings.PuddleWetness * minWetnessAngle;
        float puddle = wetness;

        if (!isSkinned)
        {
            if (wetness > 0.0 || puddleWetness > 0.0)
            {
                float3 puddleCoords = ((worldPosition.xyz + cameraPosition.xyz) * 0.5 + 0.5) * 0.01 / max(settings.PuddleRadius, 0.001);
                puddle = PerlinNoise(puddleCoords) * 0.5 + 0.5;
                puddle = puddle * ((minWetnessAngle / max(settings.PuddleMaxAngle, 0.001)) * settings.MaxPuddleWetness * 0.25) + 0.5;
                puddle *= lerp(wetness, puddleWetness, saturate(puddle - 0.25));
            }
        }

        // Wetness normal: lerp from mapped normal to vertex normal based on puddle
        params.wetnessNormal = lerp(surfaceNormal, vertexNormal, saturate(puddle));

        // Glossiness factors
        float wetnessGlossinessAlbedo = max(puddle, shoreFactorAlbedo * settings.MaxShoreWetness);
        wetnessGlossinessAlbedo *= wetnessGlossinessAlbedo;

        float wetnessGlossinessSpecular = puddle;
        if (worldPosition.z < waterHeight)
            wetnessGlossinessSpecular *= shoreFactor;

        // Apply raindrop ripple normal (primary surface only)
        if (isPrimary)
        {
            flatnessAmount *= smoothstep(settings.PuddleMinWetness, 1.0, wetnessGlossinessSpecular);
            float3 rippleNormal = normalize(lerp(float3(0, 0, 1), raindropInfo.xyz, lerp(flatnessAmount, 1.0, 0.5)));
            params.wetnessNormal = ReorientNormal(rippleNormal, params.wetnessNormal);
        }

        float waterRoughnessSpecular = saturate(1.0 - wetnessGlossinessSpecular);

        params.wetness = wetness;
        params.wetnessGlossinessAlbedo = wetnessGlossinessAlbedo;
        params.waterRoughness = waterRoughnessSpecular;

        return params;
    }

    // Apply wetness to surface: darken albedo + set up coat lobe
    void ApplyWetness(inout Surface surface, WetnessParams params)
    {
        if (params.wetness <= 0 && params.waterRoughness >= 1.0)
            return;

        float wetnessStrength = saturate(1.0 - params.waterRoughness);

        // Albedo darkening (porosity-based, basic calculation for all bounces)
        {
            float porosity = 1.0;
            porosity = lerp(porosity, 0.0, saturate(sqrt(surface.Metallic)));
            float wetnessDarkeningAmount = porosity * params.wetnessGlossinessAlbedo;
            surface.Albedo = lerp(surface.Albedo, pow(abs(surface.Albedo), 1.0 + wetnessDarkeningAmount), 0.5);
        }

        // Apply wet coat: override coat roughness and normal even if coat already exists
        if (wetnessStrength > 0)
        {
            if (surface.CoatStrength <= 0)
            {
                // No existing coat — set up new wet coat
                surface.CoatStrength = wetnessStrength;
                surface.CoatF0 = float3(0.02, 0.02, 0.02);
                surface.CoatColor = float3(1.0, 1.0, 1.0);
            }

            // Always override coat roughness and normal with wetness values
            surface.CoatRoughness = min(surface.CoatRoughness, params.waterRoughness);
            surface.CoatNormal = params.wetnessNormal;

            // Recompute coat tangent frame from wetness normal
            float3 up = abs(params.wetnessNormal.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
            surface.CoatTangent = normalize(cross(up, params.wetnessNormal));
            surface.CoatBitangent = cross(params.wetnessNormal, surface.CoatTangent);
        }
    }
}

#endif // WETNESS_HLSLI
