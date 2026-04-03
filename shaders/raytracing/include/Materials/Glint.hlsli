#ifndef __GLINT_HLSLI__
#define __GLINT_HLSLI__

// Evaluating and Sampling Glinty NDFs in Constant Time
// Pauli Kemppinen, LoÏs Paulin, Théo Thonat, Jean-Marc Thiery, Jaakko Lehtinen & Tamy Boubekeur
// https://perso.telecom-paristech.fr/boubek/papers/Glinty/
// https://www.shadertoy.com/view/tcdGDl

#include "Include/Utils/MathConstants.hlsli"

namespace Glint
{

// --- Utility functions ---

float2 Lambert(float3 v)
{
    return v.xy / sqrt(1.0 + v.z);
}

// Map half-vector from GGX NDF space to unit disk via Lambert projection.
// Returns float3(disk_uv, jacobian_determinant).
float3 NdfToDiskGGX(float3 v, float alpha)
{
    float3 hemi = float3(v.xy / alpha, v.z);
    float denom = dot(hemi, hemi);
    float2 vDisk = Lambert(normalize(hemi)) * 0.5 + 0.5;
    float jacobianDeterminant = 1.0 / (alpha * alpha * denom * denom);
    return float3(vDisk, jacobianDeterminant);
}

// Inverse quadratic form of a 2x2 matrix.
float2x2 InvQuadratic(float2x2 M)
{
    float D = determinant(M);
    float2 col0 = M[0] / D;
    float2 col1 = M[1] / D;
    float A = dot(col0, col0);
    float B = -dot(col0, col1);
    float C = dot(col1, col1);
    return float2x2(C, B, B, A);
}

// Compute LOD level from UV Jacobian and filter size.
float QueryLod(float2x2 uvJ, float filterSize)
{
    float s0 = length(uvJ[0]);
    float s1 = length(uvJ[1]);
    return log2(max(s0, s1) * filterSize) + pow(2.0, filterSize);
}

// --- Hashing / RNG ---

uint2 Shuffle(uint2 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;

    v = v ^ (v >> 16u);

    v.x += v.y * 1664525u;
    v.y += v.x * 1664525u;
    v = v ^ (v >> 16u);
    return v;
}

float2 Rand(uint2 v)
{
    return float2(Shuffle(v)) * pow(0.5, 32.0);
}

float2 Rand2D(float2 x, float2 y, float l, uint i)
{
    uint2 ux = asuint(x);
    uint2 uy = asuint(y);
    uint ul = asuint(l);
    return Rand((ux >> 16u | ux << 16u) ^ uy ^ ul ^ (i * 0x124u));
}

float Rand1D(float2 x, float2 y, float l, uint i)
{
    return Rand2D(x, y, l, i).x;
}

// --- Gaussian / CDF helpers ---

// Bürmann series approximation to the error function.
float Erf(float x)
{
    float e = exp(-x * x);
    return sign(x) * 2.0 * sqrt((1.0 - e) / K_PI) * (sqrt(K_PI) * 0.5 + 31.0 / 200.0 * e - 341.0 / 8000.0 * e * e);
}

float Cdf(float x, float mu, float sigma)
{
    return 0.5 + 0.5 * Erf((x - mu) / (sigma * sqrt(2.0)));
}

float IntegrateInterval(float x, float size, float mu, float stdev, float lowerLimit, float upperLimit)
{
    return Cdf(min(x + size, upperLimit), mu, stdev) - Cdf(max(x - size, lowerLimit), mu, stdev);
}

float IntegrateBox(float2 x, float2 size, float2 mu, float2x2 sigma, float2 lowerLimit, float2 upperLimit)
{
    return IntegrateInterval(x.x, size.x, mu.x, sqrt(sigma[0][0]), lowerLimit.x, upperLimit.x)
         * IntegrateInterval(x.y, size.y, mu.y, sqrt(sigma[1][1]), lowerLimit.y, upperLimit.y);
}

// Gaussian kernel evaluation (separable approximation using diagonal of covariance).
float Normal(float2x2 cov, float2 x)
{
    // Use full inverse for accuracy
    float det = determinant(cov);
    if (det < 1e-20) return 0.0;
    float2x2 invCov = float2x2(cov[1][1], -cov[0][1], -cov[1][0], cov[0][0]) / det;
    return exp(-0.5 * dot(x, mul(invCov, x))) / (sqrt(det) * 2.0 * K_PI);
}

// Compensation term for non-evaluated cells at a given LOD.
float Compensation(float2 xA, float2x2 sigmaA, float resA)
{
    float containing = IntegrateBox(float2(0.5, 0.5), float2(0.5, 0.5), xA, sigmaA, float2(0, 0), float2(1, 1));
    float explicitlyEvaluated = IntegrateBox(round(xA * resA) / resA, float2(1.0 / resA, 1.0 / resA), xA, sigmaA, float2(0, 0), float2(1, 1));
    return containing - explicitlyEvaluated;
}

// --- Main Glint NDF ---

/** Evaluate the discrete stochastic glint NDF.
    
    This replaces the standard GGX D term with a stochastic particle-based NDF
    that produces discrete glints from individual microfacets.

    \param[in] h         Half vector in local (tangent) space, normalized.
    \param[in] alpha     Base GGX roughness parameter (squared linear roughness).
    \param[in] glintAlpha Microfacet roughness controlling individual glint size.
    \param[in] uv        Surface UV coordinates for spatial hashing.
    \param[in] uvJ       2x2 UV Jacobian matrix (UV footprint at the shading point).
    \param[in] N         Number of microfacet particles (controls density).
    \param[in] filterSize Filter width parameter for LOD computation.
    \return    Glint NDF value (replaces D(h) in microfacet BRDF).
*/
float EvalGlintNDF(float3 h, float alpha, float glintAlpha, float2 uv, float2x2 uvJ, float N, float filterSize)
{
    float res = sqrt(N);
    float2 xS = uv;
    float3 xAandD = NdfToDiskGGX(h, alpha);
    float2 xA = xAandD.xy;
    float d = xAandD.z;

    float lambda = QueryLod(res * uvJ, filterSize);

    float D_filter = 0.0;

    [unroll]
    for (float m = 0.0; m < 2.0; m += 1.0)
    {
        float l = floor(lambda) + m;

        float wLambda = 1.0 - abs(lambda - l);
        float resS = res * pow(2.0, -l);
        float resA = pow(2.0, l);

        float2x2 uvJ2 = uvJ * filterSize;
        float2x2 sigmaS = mul(uvJ2, transpose(uvJ2));

        float2x2 sigmaA = d * (glintAlpha * glintAlpha) * float2x2(1, 0, 0, 1);

        float2 baseIA = clamp(round(xA * resA), 1.0, resA - 1.0);

        [unroll]
        for (int jA = 0; jA < 4; ++jA)
        {
            float2 iA = baseIA + float2(jA % 2, (jA / 2) % 2) - 0.5;

            float2 baseIS = round(xS * resS);

            [unroll]
            for (int jS = 0; jS < 4; ++jS)
            {
                float2 iS = baseIS + float2(jS % 2, (jS / 2) % 2) - 0.5;

                float2 gS = (iS + Rand2D(iS, iA, l, 1u) - 0.5) / resS;
                float2 gA = (iA + Rand2D(iS, iA, l, 2u) - 0.5) / resA;

                float r = Rand1D(iS, iA, l, 4u);
                float roulette = smoothstep(max(0.0, r - 0.1), min(1.0, r + 0.1), wLambda);

                D_filter += roulette * Normal(sigmaA, xA - gA) * Normal(sigmaS, xS - gS) / N;
            }
        }

        D_filter += wLambda * Compensation(xA, sigmaA, resA);
    }

    return D_filter * d / K_PI;
}

/** Compute an approximate isotropic UV Jacobian from mip level.
    
    In path tracing we don't have screen-space derivatives, so we estimate
    the UV-space footprint from the texture mip level which encodes the
    ray cone spread.

    \param[in] mipLevel  Texture mip level at the shading point.
    \return    2x2 UV Jacobian matrix (isotropic approximation).
*/
float2x2 EstimateUVJacobian(float mipLevel)
{
    float footprint = pow(2.0, mipLevel) / 1024.0; // Normalize assuming ~1024 texel base
    return float2x2(footprint, 0, 0, footprint);
}

} // namespace Glint

#endif // __GLINT_HLSLI__
