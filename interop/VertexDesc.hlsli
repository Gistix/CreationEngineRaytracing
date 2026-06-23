#ifndef VERTEX_DESC_HLSL
#define VERTEX_DESC_HLSL

#include "Interop.h"

namespace VertexAttribute
{
    static const uint16_t Position     = 0x0;
    static const uint16_t Texcoord0    = 0x1;
    static const uint16_t Texcoord1    = 0x2;
    static const uint16_t Normal       = 0x3;
    static const uint16_t Binormal     = 0x4;
    static const uint16_t Color        = 0x5;
    static const uint16_t Skinning     = 0x6;
    static const uint16_t LandData     = 0x7;
    static const uint16_t EyeData      = 0x8;
    static const uint16_t InstanceData = 0x9;

    static const uint16_t Count        = 10;
}

namespace VertexFlags
{
    static const uint16_t Vertex       = (1u << Attribute::Position);
    static const uint16_t UV           = (1u << Attribute::Texcoord0);
    static const uint16_t UV_2         = (1u << Attribute::Texcoord1);
    static const uint16_t Normal       = (1u << Attribute::Normal);
    static const uint16_t Tangent      = (1u << Attribute::Binormal);
    static const uint16_t Colors       = (1u << Attribute::Color);
    static const uint16_t Skinned      = (1u << Attribute::Skinning);
    static const uint16_t LandData     = (1u << Attribute::LandData);
    static const uint16_t EyeData      = (1u << Attribute::EyeData);
    static const uint16_t InstanceData = (1u << Attribute::InstanceData);
    static const uint16_t FullPrec     = 0x400;
}

struct VertexDesc
{
    uint32_t Lower;
    uint32_t Upper;

    // Low 32 bits of the 64-bit desc logically right-shifted by 'shift'.
    uint GetDescBits(uint shift)
    {
        if (shift == 0)
            return Lower;
        
        if (shift < 32)
            return (Lower >> shift) | (Upper << (32 - shift));
        
        return Upper >> (shift - 32);
    }

    uint16_t GetFlags()
    {
        return (uint16_t)(Upper >> 12);
    }

    bool HasFlag(uint16_t flag)
    {
        return (GetFlags() & flag) != 0;
    }

    uint32_t GetAttributeOffset(uint16_t attribute)
    {
        return GetDescBits(4 * attribute + 2) & 0x3C;
    }

    // Stored vertex size, see "src\Utils\Geometry.cpp" - GetStoredVertexSize
    uint16_t GetVertexSize()
    {
        return (uint16_t)((Lower & 0xF) << 2);
    }
};
VALIDATE_SIZE(VertexDesc, 8);

#endif  // VERTEX_DESC_HLSL
