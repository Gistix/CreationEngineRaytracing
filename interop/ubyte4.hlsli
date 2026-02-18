#ifndef UBYTE4_HLSL
#define UBYTE4_HLSL

#define UBYTE_MAX (255.0f)
#define BYTE_NORM_RCP (1.0f / UBYTE_MAX)

struct u16bytef {
    uint16_t x : 8;
 
#ifdef __cplusplus
    void pack(half value)
    {
        x = static_cast<uint16_t>(value * UBYTE_MAX);
    }  
#endif
    
    half unpack()
    {
        return (half) x * BYTE_NORM_RCP;
    }  
};

struct ubyte4f {
    uint x : 8;
    uint y : 8;
    uint z : 8;
    uint w : 8;
    
#ifdef __cplusplus
    void pack(half4 value)
    {
        x = static_cast<uint>(value.x * UBYTE_MAX);
        y = static_cast<uint>(value.y * UBYTE_MAX);
        z = static_cast<uint>(value.z * UBYTE_MAX);
        w = static_cast<uint>(value.w * UBYTE_MAX);
    }     
#endif
    
    half4 unpack()
    {
        return half4(
            x * BYTE_NORM_RCP,
            y * BYTE_NORM_RCP,
            z * BYTE_NORM_RCP,
            w * BYTE_NORM_RCP
        );
    } 
};

#endif