#ifndef OWEN_SCRAMBLING_HLSLI
#define OWEN_SCRAMBLING_HLSLI

// ============================================================
// Hash-based Owen Scrambling
// Based on: Brent Burley, "Practical Hash-based Owen Scrambling"
// JCGT, Vol. 9, No. 4, 2020
// http://jcgt.org/published/0009/04/01/
// ============================================================

// Laine-Karras permutation (Listing 1)
// Achieves a nested uniform permutation by exploiting the property
// that multiplying by an even constant makes each bit only affect
// bits to the left (a nested hash).
uint LKPermutation(uint x, uint seed)
{
    x += seed;
    x ^= x * 0x6c50b47cu;
    x ^= x * 0xb82f1e52u;
    x ^= x * 0xc7afe638u;
    x ^= x * 0x8d22f6e6u;
    return x;
}

// Nested uniform scramble / Owen scrambling (Listing 2)
// Applying the Laine-Karras permutation in bit-reversed order
// produces a nested uniform scramble (Owen scramble).
uint OwenScramble(uint x, uint seed)
{
    x = reversebits(x);
    x = LKPermutation(x, seed);
    x = reversebits(x);
    return x;
}

// Hash combine for seed mixing (boost::hash_combine style)
uint HashCombine(uint seed, uint value)
{
    return seed ^ (value + 0x9e3779b9u + (seed << 6) + (seed >> 2));
}

// ============================================================
// Sobol Sequence - Direction Numbers
// ============================================================
// Dimension 0: Van der Corput (radical inverse base 2) = reversebits(index)
// Dimensions 1-3: Standard Sobol direction numbers from Joe-Kuo tables

// Dimension 1: primitive polynomial x+1, degree 1
static const uint SOBOL_DIR_1[32] = {
    0x80000000u, 0xc0000000u, 0xa0000000u, 0xf0000000u,
    0x88000000u, 0xcc000000u, 0xaa000000u, 0xff000000u,
    0x80800000u, 0xc0c00000u, 0xa0a00000u, 0xf0f00000u,
    0x88880000u, 0xcccc0000u, 0xaaaa0000u, 0xffff0000u,
    0x80008000u, 0xc000c000u, 0xa000a000u, 0xf000f000u,
    0x88008800u, 0xcc00cc00u, 0xaa00aa00u, 0xff00ff00u,
    0x80808080u, 0xc0c0c0c0u, 0xa0a0a0a0u, 0xf0f0f0f0u,
    0x88888888u, 0xccccccccu, 0xaaaaaaaau, 0xffffffffu
};

// Dimension 2: primitive polynomial x^2+x+1, degree 2
static const uint SOBOL_DIR_2[32] = {
    0x80000000u, 0x40000000u, 0xe0000000u, 0xb0000000u,
    0x68000000u, 0xf4000000u, 0x86000000u, 0x4f000000u,
    0xe8800000u, 0xb4400000u, 0x66e00000u, 0xffb00000u,
    0x80e80000u, 0x40b40000u, 0xe0660000u, 0xb0ff0000u,
    0x68808000u, 0xf4404000u, 0x86e0e000u, 0x4fb0b000u,
    0xe8e86800u, 0xb4b4f400u, 0x66668600u, 0xffff4f00u,
    0x80006880u, 0x4000f440u, 0xe00086e0u, 0xb0004fb0u,
    0x6800e8e8u, 0xf400b4b4u, 0x86006666u, 0x4f00ffffu
};

// Dimension 3: primitive polynomial x^3+x+1, degree 3
static const uint SOBOL_DIR_3[32] = {
    0x80000000u, 0x40000000u, 0x20000000u, 0xd0000000u,
    0x68000000u, 0xf4000000u, 0xa2000000u, 0x91000000u,
    0x48800000u, 0x27400000u, 0xcba00000u, 0x66d00000u,
    0xe8080000u, 0xb4040000u, 0x82020000u, 0x410d0000u,
    0x20868000u, 0xd34f4000u, 0x69aa2000u, 0xf7d91000u,
    0xa08c8800u, 0x93467400u, 0x49aeba00u, 0x27db6d00u,
    0xc8800080u, 0x67400040u, 0xeba00020u, 0xb6d000d0u,
    0x80080068u, 0x400400f4u, 0x200200a2u, 0xd00d0091u
};

// Compute raw Sobol sample value (as uint) for given index and dimension (0-3)
uint SobolSample(uint index, uint dim)
{
    uint result = 0;

    if (dim == 0)
    {
        result = reversebits(index);
    }
    else
    {
        for (uint bit = 0; bit < 32; ++bit, index >>= 1)
        {
            if (index & 1)
            {
                if (dim == 1)
                    result ^= SOBOL_DIR_1[bit];
                else if (dim == 2)
                    result ^= SOBOL_DIR_2[bit];
                else // dim == 3
                    result ^= SOBOL_DIR_3[bit];
            }
        }
    }

    return result;
}

// ============================================================
// Shuffled Scrambled Sobol Sampling (Listing 3 from paper)
// ============================================================

// Generate a single Owen-scrambled Sobol sample for a given dimension.
// sampleIndex: which sample to generate (e.g. frame count for progressive rendering)
// dimension:   which dimension (0-3 for primary Sobol, higher for padding)
// seed:        per-pixel or per-path seed for decorrelation
float OwenScrambledSobol(uint sampleIndex, uint dimension, uint seed)
{
    // For dimensions 0-3, use the Sobol sequence with Owen scrambling
    // For higher dimensions, use padding with shuffled sequences
    uint sobolDim = dimension % 4;
    uint groupSeed = HashCombine(seed, dimension / 4);

    // Shuffle the sample index (nested uniform shuffle)
    uint shuffledIndex = OwenScramble(sampleIndex, groupSeed);

    // Compute raw Sobol sample
    uint sobolValue = SobolSample(shuffledIndex, sobolDim);

    // Apply Owen scrambling to the sample value
    uint scrambleSeed = HashCombine(groupSeed, sobolDim);
    uint scrambled = OwenScramble(sobolValue, scrambleSeed);

    return float(scrambled) / 4294967296.0;
}

// Generate 4D Owen-scrambled Sobol sample
// sampleIndex: which sample to generate
// seed:        per-pixel seed for decorrelation
float4 OwenScrambledSobol4D(uint sampleIndex, uint seed)
{
    // Shuffle the sample index
    uint shuffledIndex = OwenScramble(sampleIndex, seed);

    // Compute 4D Sobol sample and scramble each dimension
    float4 result;
    [unroll]
    for (uint dim = 0; dim < 4; ++dim)
    {
        uint sobolValue = SobolSample(shuffledIndex, dim);
        result[dim] = float(OwenScramble(sobolValue, HashCombine(seed, dim))) / 4294967296.0;
    }
    return result;
}

// Generate 2D Owen-scrambled Sobol sample (most common use case)
float2 OwenScrambledSobol2D(uint sampleIndex, uint seed)
{
    uint shuffledIndex = OwenScramble(sampleIndex, seed);

    uint s0 = OwenScramble(SobolSample(shuffledIndex, 0), HashCombine(seed, 0));
    uint s1 = OwenScramble(SobolSample(shuffledIndex, 1), HashCombine(seed, 1));

    return float2(float(s0), float(s1)) / 4294967296.0;
}

// ============================================================
// SobolSampler: convenience struct for sequential sampling
// ============================================================
// Use this as a drop-in replacement for the Random() pattern.
// It draws from Owen-scrambled Sobol for the first few dimensions,
// and falls back to hashed random for higher dimensions (padding).

struct SobolSampler
{
    uint sampleIndex;  // Sample index (e.g., frame count)
    uint seed;         // Per-pixel/per-path seed
    uint dimension;    // Current dimension counter

    // Get next random float in [0, 1), consuming one dimension
    float Next()
    {
        float value = OwenScrambledSobol(sampleIndex, dimension, seed);
        dimension++;
        return value;
    }

    // Get next 2D sample, consuming two dimensions
    float2 Next2D()
    {
        float2 value = float2(
            OwenScrambledSobol(sampleIndex, dimension, seed),
            OwenScrambledSobol(sampleIndex, dimension + 1, seed)
        );
        dimension += 2;
        return value;
    }

    // Advance seed for next bounce/event (for padding to higher dimensions)
    void AdvanceSeed()
    {
        seed = PCGHash(seed);
        dimension = 0;
    }
};

// ============================================================
// Global Sobol state for transparent Random() integration
// ============================================================
// In RayGen shaders, InitSobolSampler() activates the global sampler.
// Random() then returns Owen-scrambled Sobol values.
// In AnyHit/ClosestHit shaders (not initialized), Random() uses PCGHash.

static SobolSampler g_SobolSampler = (SobolSampler)0;
static bool g_SobolActive = false;

// Call this at the start of each RayGen shader, AFTER InitRandomSeed.
void InitSobolSampler(uint2 coord, uint2 size, uint frameCount)
{
    g_SobolSampler.sampleIndex = frameCount;
    g_SobolSampler.seed = PCGHash(coord.x + coord.y * size.x);
    g_SobolSampler.dimension = 0;
    g_SobolActive = true;
}

// Call at bounce boundaries to decorrelate dimensions
void SobolNextBounce()
{
    if (g_SobolActive)
        g_SobolSampler.AdvanceSeed();
}

#endif // OWEN_SCRAMBLING_HLSLI
