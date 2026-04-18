#include "interop/CameraData.hlsli"

ConstantBuffer<CameraData> Camera       : register(b0);

Texture2D<float4>    CurrentFrame       : register(t0);

RWTexture2D<float4>  AccumulationBuffer : register(u0);

cbuffer AccumulationConstants           : register(b1)
{
    uint  AccumulatedFrames;   // Number of frames already accumulated (0 = first frame)
    uint3 _Pad;
};

[numthreads(8, 8, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
{
    const uint2 size = Camera.RenderSize;
    
    if (any(idx >= size))
        return;
    
    float4 currentSample = CurrentFrame[idx];
    
    if (AccumulatedFrames == 0)
    {
        // First frame: initialize accumulation buffer directly
        AccumulationBuffer[idx] = currentSample;
    }
    else
    {
        // Subsequent frames: blend with running average
        // Formula: result = lerp(history, new, 1 / (N + 1))
        // This is equivalent to: result = (history * N + new) / (N + 1)
        float blend = 1.0f / (float(AccumulatedFrames) + 1.0f);
        float4 history = AccumulationBuffer[idx];
        float4 accumulated = lerp(history, currentSample, blend);
        
        // Guard against NaN/Inf from degenerate samples
        if (any(isnan(accumulated)) || any(isinf(accumulated)))
            accumulated = history;
        
        AccumulationBuffer[idx] = accumulated;
    }
}
