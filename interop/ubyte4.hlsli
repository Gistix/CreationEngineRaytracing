#ifndef UBYTE4_HLSL
#define UBYTE4_HLSL

#define BYTE_NORM_RCP (1.0h / 255.0h)

struct u16bytef {
    uint16_t x : 8;
    
#ifndef __cplusplus   
    half unpack()
    {
        return (half)x * BYTE_NORM_RCP;
    }	
#endif    
};

struct ubyte4f {
    uint x : 8;
    uint y : 8;
    uint z : 8;
    uint w : 8;
    
#ifndef __cplusplus   
    half4 unpack()
    {
        return half4(
            (half)x * BYTE_NORM_RCP,
            (half)y * BYTE_NORM_RCP,
            (half)z * BYTE_NORM_RCP,
            (half)w * BYTE_NORM_RCP
        );
    }	
#endif    
};

#endif