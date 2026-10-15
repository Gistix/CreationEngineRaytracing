#ifndef SH_HLSLI
#define SH_HLSLI

// L2 spherical harmonics helpers shared by SHResolveTemporal (project +
// temporal accumulate at low res) and SHUpsample (decode at full res).
//
// Coefficient order: L0, L1m1, L10, L11, L2m2, L2m1, L20, L21, L22.
// Packed per lobe into 7 RGBA16F slices (27 floats + 1 spare):
//   s0: L0.rgb, L1m1.r | s1: L1m1.gb, L10.rg | s2: L10.b, L11.rgb
//   s3: L2m2.rgb, L2m1.r | s4: L2m1.gb, L20.rg | s5: L20.b, L21.rgb
//   s6: L22.rgb, extra (average hit distance)

void SHEvaluateBasis(float3 dir, out float shBasis[9])
{
    shBasis[0] = 0.282095f;
    shBasis[1] = 0.488603f * dir.y;
    shBasis[2] = 0.488603f * dir.z;
    shBasis[3] = 0.488603f * dir.x;
    shBasis[4] = 1.092548f * dir.x * dir.y;
    shBasis[5] = 1.092548f * dir.y * dir.z;
    shBasis[6] = 0.315392f * (3.0f * dir.z * dir.z - 1.0f);
    shBasis[7] = 1.092548f * dir.x * dir.z;
    shBasis[8] = 0.546274f * (dir.x * dir.x - dir.y * dir.y);
}

void SHAccumulate(float3 dir, float3 radiance, inout float3 sh[9])
{
    float shBasis[9];
    SHEvaluateBasis(dir, shBasis);

    [unroll]
    for (uint i = 0; i < 9; i++)
        sh[i] += radiance * shBasis[i];
}

float3 SHResolveCoeffs(float3 sh[9], float3 n)
{
    float shBasis[9];
    SHEvaluateBasis(n, shBasis);

    float3 result = 0.0f;

    [unroll]
    for (uint i = 0; i < 9; i++)
        result += sh[i] * shBasis[i];

    return result;
}

void SHPack(float3 sh[9], float extra, out float4 slices[7])
{
    slices[0] = float4(sh[0], sh[1].r);
    slices[1] = float4(sh[1].gb, sh[2].rg);
    slices[2] = float4(sh[2].b, sh[3]);
    slices[3] = float4(sh[4], sh[5].r);
    slices[4] = float4(sh[5].gb, sh[6].rg);
    slices[5] = float4(sh[6].b, sh[7]);
    slices[6] = float4(sh[8], extra);
}

void SHUnpack(float4 slices[7], out float3 sh[9], out float extra)
{
    sh[0] = slices[0].rgb;
    sh[1] = float3(slices[0].a, slices[1].rg);
    sh[2] = float3(slices[1].ba, slices[2].r);
    sh[3] = slices[2].gba;
    sh[4] = slices[3].rgb;
    sh[5] = float3(slices[3].a, slices[4].rg);
    sh[6] = float3(slices[4].ba, slices[5].r);
    sh[7] = slices[5].gba;
    sh[8] = slices[6].rgb;
    extra = slices[6].a;
}

#endif // SH_HLSLI
