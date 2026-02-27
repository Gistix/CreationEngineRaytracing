#ifndef COMMON_HLSLI
#define COMMON_HLSLI

#include "Include/Common/Game.hlsli"

#define M_TO_GAME_UNIT (1.0f / (GAME_UNIT_TO_M))

#define DIV_EPSILON (1e-4f)
#define LAND_MIN_WEIGHT (0.01f)

float3 SafeNormalize(float3 input)
{
    float lenSq = dot(input,input);
    return input * rsqrt(max( 1.175494351e-38, lenSq));
}

float3 FlipIfOpposite(float3 normal, float3 referenceNormal)
{
    return (dot(normal, referenceNormal)>=0)?(normal):(-normal);
}

float F0toIOR(float3 F0)
{
	float f0 = max(max(F0.r, F0.g), F0.b);
	return (1.0 + sqrt(f0)) / (1.0 - sqrt(f0));
}

void NormalMap(float3 normalMap, float handedness, float3 geomNormalWS, float3 geomTangentWS, float3 geomBitangentWS, out float3 normalWS, out float3 tangentWS, out float3 bitangentWS)
{
	normalMap = normalMap * 2.0f - 1.0f;
	
    normalWS = normalMap.x * geomTangentWS + normalMap.y * geomBitangentWS + normalMap.z * geomNormalWS;
	
	float normalLengthSq = dot(normalWS, normalWS);
    normalWS = (normalLengthSq > 1e-6f) ? (normalWS * rsqrt(normalLengthSq)) : geomNormalWS;

    tangentWS = normalize(geomTangentWS - normalWS * dot(geomTangentWS, normalWS));
    bitangentWS = cross(normalWS, tangentWS) * handedness;
}

float Remap(float x, float min, float max)
{
    return clamp(min + saturate(x) * (max - min), min, max);
}

#endif // COMMON_HLSLI