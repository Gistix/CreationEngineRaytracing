#pragma once

struct MicroVertex
{
    float3 position;    // displaced world position
    float  displacement; // signed scalar along normal, [0,1] normalized
};

struct MicromapTriangle     // one per base triangle
{
    uint   byteOffset;      // into packed displacement buffer
    uint8_t subdivLevel;
    uint8_t edgeFlags;
    uint16_t pad;
};

/*struct MicroTriangle {
    uint16_t subdivLevel; // 0-5
    uint16_t edgeFlags; // decimation: bit 0=edge AB, 1=BC, 2=CA
    eastl::vector<float> microDisplacements; // (2^N+1)*(2^N+2)/2 values
};*/