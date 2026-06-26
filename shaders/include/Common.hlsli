#ifndef COMMON_HLSLI
#define COMMON_HLSLI

#include "Include/Common/Game.hlsli"

#define FP_VIEW_Z (16.5f)
#define SKY_Z (0.9999f)

#define M_TO_GAME_UNIT (1.0f / (GAME_UNIT_TO_M))

#define DIV_EPSILON (1e-4f)
#define LAND_MIN_WEIGHT (0.01f)

void CreateOrthonormalBasis(in float3 normal, out float3 tangent, out float3 bitangent)
{
    float3 up = abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);

    tangent = normalize(cross(up, normal));
    bitangent = cross(normal, tangent);
}

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

void NormalMap(float3 normalMap, float3 geomNormalWS, float3 geomTangentWS, float3 geomBitangentWS, out float3 normalWS, out float3 tangentWS, out float3 bitangentWS)
{
	normalMap = normalize(normalMap * 2.0f - 1.0f);
	
    normalWS = normalize(normalMap.x * geomTangentWS + normalMap.y * geomBitangentWS + normalMap.z * geomNormalWS);
    tangentWS = normalize(geomTangentWS - normalWS * dot(geomTangentWS, normalWS));
    bitangentWS = cross(normalWS, tangentWS);
    bitangentWS *= (dot(bitangentWS, geomBitangentWS) < 0.0f) ? -1.0f : 1.0f;
}

void ModelSpaceNormalMap(float3 normalMap, out float3 normalWS, out float3 tangentWS, out float3 bitangentWS)
{
    // Swizzle matches vanilla shaders
    normalWS = normalize(normalMap.xzy * 2.0f - 1.0f);
    CreateOrthonormalBasis(normalWS, tangentWS, bitangentWS);
}

float Remap(float x, float min, float max)
{
    return clamp(min + saturate(x) * (max - min), min, max);
}

float ScreenToViewDepth(const float screenDepth, float4 cameraData)
{
	return (cameraData.w / (-screenDepth * cameraData.z + cameraData.x));
}

float3 ScreenToViewPosition(const float2 screenPos, const float viewspaceDepth, const float4 ndcToView)
{
	float3 ret;
	ret.xy = (ndcToView.xy * screenPos.xy + ndcToView.zw) * viewspaceDepth;
	ret.z = viewspaceDepth;
	return ret;
}

float3 ViewToWorldPosition(const float3 pos, const float4x4 invView)
{
	float4 worldpos = mul(invView, float4(pos, 1));
	return worldpos.xyz / worldpos.w;
}

float3 ViewToWorldVector(const float3 vec, const float4x4 invView)
{
	return mul((float3x3)invView, vec);
}

half2 EncodeNormal(half3 n)
{
	n = -n;
	half2 p = n.xy / (abs(n.x) + abs(n.y) + abs(n.z));
	if (n.z < 0.0)
	{
		#if !defined(DX11)
		p = (1.0 - abs(p.yx)) * select(p >= 0.0, half2(1.0, 1.0), half2(-1.0, -1.0));
		#else
		p = (1.0 - abs(p.yx)) * (p >= 0.0 ? half2(1.0, 1.0) : half2(-1.0, -1.0));
		#endif
	}
	return p * 0.5 + 0.5;
}

half3 DecodeNormal(half2 f)
{
	f = f * 2.0 - 1.0;
	// https://twitter.com/Stubbesaurus/status/937994790553227264
	half3 n = half3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	half t = saturate(-n.z);
	#if !defined(DX11)
	n.xy += select(n.xy >= 0.0, -t, t);
	#else
	n.xy += n.xy >= 0.0 ? -t : t;
	#endif
	return -normalize(n);
}


// ============================================================================
// Motion Vector Computation
// ============================================================================

float3 computeMotionVector(float3 posW, float3 prevPosW)
{
    float4 currClip = mul(Camera.ViewProj, float4(posW - Camera.Position, 1.0));
    float4 prevClip = mul(Camera.PrevViewProj, float4(prevPosW - Camera.PositionPrev, 1.0));

    float3 currNDC = currClip.xyz / currClip.w;
    float3 prevNDC = prevClip.xyz / prevClip.w;

    float3 motion = prevNDC - currNDC;
    return motion * float3(0.5f, -0.5f, 1.0f);
}

float3 computeMotionVectorCameraRelative(float3 posCamera, float3 prevPosCamera)
{
    float4 currClip = mul(Camera.ViewProj, float4(posCamera, 1.0));
    float4 prevClip = mul(Camera.PrevViewProj, float4(prevPosCamera, 1.0));

    float3 currNDC = currClip.xyz / currClip.w;
    float3 prevNDC = prevClip.xyz / prevClip.w;

    float3 motion = prevNDC - currNDC;
    return motion * float3(0.5f, -0.5f, 1.0f);
}

// ============================================================================
// Clip-space Depth Computation
// ============================================================================

float computeClipDepth(float3 posW)
{
    float4 clipPos = mul(Camera.ViewProj, float4(posW - Camera.Position, 1.0));
    return clipPos.z / clipPos.w;
}

float computeClipDepthCameraRelative(float3 posCamera)
{
    float4 clipPos = mul(Camera.ViewProj, float4(posCamera, 1.0));
    return clipPos.z / clipPos.w;
}

float3 GetWorldPosition(const int2 id, const float depth)
{
    const float depthVS = ScreenToViewDepth(depth, Camera.CameraData);
    
    const float2 uv = clamp(float2(id + 0.5f) / Camera.RenderSize, float2(0.0f, 0.0f), float2(1.0f, 1.0f));
    
    const float3 positionVS = ScreenToViewPosition(uv, depthVS, Camera.NDCToView);
    const float3 positionCS = ViewToWorldPosition(positionVS, Camera.ViewInverse);
    return positionCS + Camera.Position.xyz;
}

float GetDepthClamped(const Texture2D<float> depth, in int2 id)
{
    return depth[clamp(id, int2(0, 0), (int2)Camera.RenderSize)];
}

// https://atyuwen.github.io/posts/normal-reconstruction/
float3 ComputeNormalImproved(const Texture2D<float> depth, in float2 id, in int2 depthID)
{
    float c0 = GetDepthClamped(depth, depthID);
    float l2 = GetDepthClamped(depth, depthID - int2(2, 0));
    float l1 = GetDepthClamped(depth, depthID - int2(1, 0));
    float r1 = GetDepthClamped(depth, depthID + int2(1, 0));
    float r2 = GetDepthClamped(depth, depthID + int2(2, 0));
    float b2 = GetDepthClamped(depth, depthID - int2(0, 2));
    float b1 = GetDepthClamped(depth, depthID - int2(0, 1));
    float t1 = GetDepthClamped(depth, depthID + int2(0, 1));
    float t2 = GetDepthClamped(depth, depthID + int2(0, 2));
    
    float dl = abs(l1 * l2 / (2.0 * l2 - l1) - c0);
    float dr = abs(r1 * r2 / (2.0 * r2 - r1) - c0);
    float db = abs(b1 * b2 / (2.0 * b2 - b1) - c0);
    float dt = abs(t1 * t2 / (2.0 * t2 - t1) - c0);
    
    float3 ce = GetWorldPosition(id, c0);

    float3 dpdx = (dl < dr) ? ce - GetWorldPosition(id - int2(1, 0), l1) :
                          -ce + GetWorldPosition(id + int2(1, 0), r1);
    float3 dpdy = (db < dt) ? ce - GetWorldPosition(id - int2(0, 1), b1) :
                          -ce + GetWorldPosition(id + int2(0, 1), t1);

    // Invert normals so they match GBuffer
    return normalize(cross(dpdy, dpdx));
}

float3 TransformPointCameraRelative(float3x4 transform, float3 position, float3 cameraPosition)
{
    float3 translation = float3(transform._m03, transform._m13, transform._m23);
    return mul((float3x3) transform, position) + (translation - cameraPosition);
}

float3 TransformMeshInstancePointCameraRelative(float3 objectSpacePosition, float3x4 meshTransform, float3x4 instanceTransform, float3 cameraPosition)
{
    float3 rootSpacePosition = mul(meshTransform, float4(objectSpacePosition, 1.0));
    return TransformPointCameraRelative(instanceTransform, rootSpacePosition, cameraPosition);
}

float3 RotateByQuaternion(float3 v, float4 q)
{
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

float4 MatrixToQuaternionLocal(float3x3 m)
{
    float4 q;
    float trace = m[0][0] + m[1][1] + m[2][2];

    if (trace > 0.0)
    {
        float s = sqrt(trace + 1.0) * 2.0;
        q.w = 0.25 * s;
        q.x = (m[2][1] - m[1][2]) / s;
        q.y = (m[0][2] - m[2][0]) / s;
        q.z = (m[1][0] - m[0][1]) / s;
    }
    else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
    {
        float s = sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0;
        q.w = (m[2][1] - m[1][2]) / s;
        q.x = 0.25 * s;
        q.y = (m[0][1] + m[1][0]) / s;
        q.z = (m[0][2] + m[2][0]) / s;
    }
    else if (m[1][1] > m[2][2])
    {
        float s = sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0;
        q.w = (m[0][2] - m[2][0]) / s;
        q.x = (m[0][1] + m[1][0]) / s;
        q.y = 0.25 * s;
        q.z = (m[1][2] + m[2][1]) / s;
    }
    else
    {
        float s = sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0;
        q.w = (m[1][0] - m[0][1]) / s;
        q.x = (m[0][2] + m[2][0]) / s;
        q.y = (m[1][2] + m[2][1]) / s;
        q.z = 0.25 * s;
    }

    return normalize(q);
}

float4 QuaternionMultiplyLocal(float4 a, float4 b)
{
    return float4(
        a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz),
        a.w * b.w - dot(a.xyz, b.xyz));
}
#endif // COMMON_HLSLI
