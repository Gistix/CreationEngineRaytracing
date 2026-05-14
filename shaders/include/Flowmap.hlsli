#ifndef FLOWMAP_HLSLI
#define FLOWMAP_HLSLI

/**
 * Structure containing complete flowmap information
 */
struct FlowmapData
{
	float4 color;       // Raw flowmap color (R=flow_x, G=flow_y, B=flow_strength, A=flow_mask)
	float2 flowVector;  // Flow vector (coordinate space depends on source function)
};

/**
 * Gets raw flowmap data before UV-space coordinate transformation
 *
 * @param input Pixel shader input containing texture coordinates
 * @param uvShift UV offset for sampling the flowmap texture
 * @return FlowmapData with raw components:
 *         - color: Raw flowmap texture sample (RG=rotation, B=strength, A=mask)
 *         - flowVector: Base flow vector before any coordinate transformation
 *                      Ready for direct application of rotation matrix for world positioning
 *
 * @details This function provides flowmap data in its original coordinate space, suitable
 *          for world-space positioning effects (like ripple movement). The flowVector has
 *          NOT been transformed for UV-space normal sampling - that transformation is only
 *          applied in GetFlowmapDataUV() which uses transpose for UV coordinate perturbation.
 *
 *          Use this function when you need to apply the rotation matrix directly for
 *          world-space effects without needing to reverse any existing transformations.
 *
 * @see GetFlowmapDataUV() for UV-space normal sampling (applies transpose transformation)
 */
FlowmapData GetFlowmapDataTextureSpace(Texture2D<float4> flowMapTex, SamplerState flowMapSampler, float4 flowCoord, float2 uvShift)
{
	FlowmapData data;
	data.color = flowMapTex.SampleLevel(flowMapSampler, flowCoord.xy + uvShift, 0);
	data.flowVector = (64 * flowCoord.zw) * sqrt(1.01 - data.color.z);
	// NOTE: flowVector is NOT transformed yet - this is the raw vector before rotation matrix
	return data;
}

/**
 * Samples flowmap texture and calculates UV-space flow data for texture sampling
 *
 * @param input Pixel shader input containing texture coordinates and world position data
 * @param uvShift UV offset for sampling the flowmap texture (used for animation/variation)
 * @return FlowmapData Complete flowmap information with UV-space flow vector
 *
 * @details This function:
 *          - Samples the flowmap texture at the specified UV coordinates
 *          - Decodes flow direction from RG channels (remapped from [0,1] to [-1,1])
 *          - Calculates flow strength using the blue channel with sqrt falloff
 *          - Applies transpose rotation matrix to transform flow direction to UV space
 *          - Scales flow vector by world position and strength factors
 *
 * @note Flowmap format:
 *       - Red channel: Flow direction X component (0.5 = no flow, 0/1 = negative/positive flow)
 *       - Green channel: Flow direction Y component (0.5 = no flow, 0/1 = negative/positive flow)
 *       - Blue channel: Flow strength (0 = no flow, 1 = maximum flow)
 *       - Alpha channel: Flow mask/intensity multiplier
 */
FlowmapData GetFlowmapDataUV(Texture2D<float4> flowMapTex, SamplerState flowMapSampler, float4 flowCoord, float2 uvShift)
{
	FlowmapData flowTexData = GetFlowmapDataTextureSpace(flowMapTex, flowMapSampler, flowCoord, uvShift);
	float2 flowSinCos = flowTexData.color.xy * 2 - 1;
	float2x2 flowRotationMatrix = float2x2(flowSinCos.x, flowSinCos.y, -flowSinCos.y, flowSinCos.x);
	flowTexData.flowVector = mul(transpose(flowRotationMatrix), flowTexData.flowVector);
	return flowTexData;
}

FlowmapData GetFlowmapDataWorldSpace(FlowmapData textureSpaceData)
{
	FlowmapData data = textureSpaceData;
	float2 flowDirection = -(data.color.xy * 2 - 1);    // Decode direction with 180° correction
	data.flowVector = data.flowVector * flowDirection;  // Transform to world space
	return data;
}

/**
 * Generates flowmap-based normal (no parallax - flowmap normals are not parallax-shifted)
 * No mip clamping
 */
float3 GetFlowmapNormal(Texture2D<float4> flowMapTex, SamplerState pointSampler,
	Texture2D<float4> flowMapNormalsTex, SamplerState defaultSampler,
	float4 flowCoord, float2 uvShift, float multiplier, float offset, float scroll, float mipLevel)
{
    FlowmapData flowData = GetFlowmapDataUV(flowMapTex, pointSampler, flowCoord, uvShift);
    float2 uv = offset + (flowData.flowVector - float2(multiplier * ((0.001f * scroll) * flowData.color.w), 0));

    return float3(flowMapNormalsTex.SampleLevel(defaultSampler, uv, mipLevel).xy, flowData.color.z);
}

#endif // FLOWMAP_HLSLI