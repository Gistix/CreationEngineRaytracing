#ifndef SAMPLE_GENERATORS_HLSLI
#define SAMPLE_GENERATORS_HLSLI

static const uint SAMPLE_GENERATOR_EFFECT_BASE = 0u;
static const uint SAMPLE_GENERATOR_EFFECT_SCATTER_BSDF = 1u;
static const uint SAMPLE_GENERATOR_EFFECT_NEXT_EVENT_ESTIMATION = 2u;
static const uint SAMPLE_GENERATOR_EFFECT_RUSSIAN_ROULETTE = 6u;

#ifndef ENABLE_LOW_DISCREPANCY_SAMPLER_FOR_BSDF
#   define ENABLE_LOW_DISCREPANCY_SAMPLER_FOR_BSDF 1
#endif

#ifndef DISABLE_LOW_DISCREPANCY_SAMPLING_AFTER_DIFFUSE_BOUNCE_COUNT
#   define DISABLE_LOW_DISCREPANCY_SAMPLING_AFTER_DIFFUSE_BOUNCE_COUNT 1
#endif

static const uint kRandomFrameSalt = 0x035F9F29u;
static const uint kRandomStepSalt = 0x9E3779B9u;
static const uint kSampleGeneratorLDDisabled = 0xFFFFFFFEu;
static const uint kSampleGeneratorLDRanOutOfDimensions = 0xFFFFFFFFu;
static const uint kSampleGeneratorMaxLDDimension = 5u;

uint Hash32(uint x)
{
    x ^= x >> 16;
    x *= 0x21F0AAADu;
    x ^= x >> 15;
    x *= 0xF35A2D97u;
    x ^= x >> 15;
    return x;
}

uint Hash32Combine(uint seed, uint value)
{
    return seed ^ (Hash32(value) + kRandomStepSalt + (seed << 6) + (seed >> 2));
}

float Hash32ToFloat(uint hash)
{
    return (hash >> 8) * (1.0f / 16777216.0f);
}

uint SampleGeneratorSobol(uint index, uint dimension)
{
    const uint directions[160] = {
        0x80000000u, 0x40000000u, 0x20000000u, 0x10000000u,
        0x08000000u, 0x04000000u, 0x02000000u, 0x01000000u,
        0x00800000u, 0x00400000u, 0x00200000u, 0x00100000u,
        0x00080000u, 0x00040000u, 0x00020000u, 0x00010000u,
        0x00008000u, 0x00004000u, 0x00002000u, 0x00001000u,
        0x00000800u, 0x00000400u, 0x00000200u, 0x00000100u,
        0x00000080u, 0x00000040u, 0x00000020u, 0x00000010u,
        0x00000008u, 0x00000004u, 0x00000002u, 0x00000001u,

        0x80000000u, 0xC0000000u, 0xA0000000u, 0xF0000000u,
        0x88000000u, 0xCC000000u, 0xAA000000u, 0xFF000000u,
        0x80800000u, 0xC0C00000u, 0xA0A00000u, 0xF0F00000u,
        0x88880000u, 0xCCCC0000u, 0xAAAA0000u, 0xFFFF0000u,
        0x80008000u, 0xC000C000u, 0xA000A000u, 0xF000F000u,
        0x88008800u, 0xCC00CC00u, 0xAA00AA00u, 0xFF00FF00u,
        0x80808080u, 0xC0C0C0C0u, 0xA0A0A0A0u, 0xF0F0F0F0u,
        0x88888888u, 0xCCCCCCCCu, 0xAAAAAAAAu, 0xFFFFFFFFu,

        0x80000000u, 0xC0000000u, 0x60000000u, 0x90000000u,
        0xE8000000u, 0x5C000000u, 0x8E000000u, 0xC5000000u,
        0x68800000u, 0x9CC00000u, 0xEE600000u, 0x55900000u,
        0x80680000u, 0xC09C0000u, 0x60EE0000u, 0x90550000u,
        0xE8808000u, 0x5CC0C000u, 0x8E606000u, 0xC5909000u,
        0x6868E800u, 0x9C9C5C00u, 0xEEEE8E00u, 0x5555C500u,
        0x8000E880u, 0xC0005CC0u, 0x60008E60u, 0x9000C590u,
        0xE8006868u, 0x5C009C9Cu, 0x8E00EEEEu, 0xC5005555u,

        0x80000000u, 0xC0000000u, 0x20000000u, 0x50000000u,
        0xF8000000u, 0x74000000u, 0xA2000000u, 0x93000000u,
        0xD8800000u, 0x25400000u, 0x59E00000u, 0xE6D00000u,
        0x78080000u, 0xB40C0000u, 0x82020000u, 0xC3050000u,
        0x208F8000u, 0x51474000u, 0xFBEA2000u, 0x75D93000u,
        0xA0858800u, 0x914E5400u, 0xDBE79E00u, 0x25DB6D00u,
        0x58800080u, 0xE54000C0u, 0x79E00020u, 0xB6D00050u,
        0x800800F8u, 0xC00C0074u, 0x200200A2u, 0x50050093u,

        0x80000000u, 0x40000000u, 0x20000000u, 0xB0000000u,
        0xF8000000u, 0xDC000000u, 0x7A000000u, 0x9D000000u,
        0x5A800000u, 0x2FC00000u, 0xA1600000u, 0xF0B00000u,
        0xDA880000u, 0x6FC40000u, 0x81620000u, 0x40BB0000u,
        0x22878000u, 0xB3C9C000u, 0xFB65A000u, 0xDDB2D000u,
        0x78022800u, 0x9C0B3C00u, 0x5A0FB600u, 0x2D0DDB00u,
        0xA2878080u, 0xF3C9C040u, 0xDB65A020u, 0x6DB2D0B0u,
        0x800228F8u, 0x400B3CDCu, 0x200FB67Au, 0xB00DDB9Du
    };

    uint x = 0u;
    dimension = min(dimension, 4u);
    [unroll]
    for (uint bit = 0u; bit < 32u; bit++)
    {
        x ^= ((index >> bit) & 1u) * directions[dimension * 32u + bit];
    }
    return x;
}

uint SampleGeneratorOwenHash(uint x, uint seed)
{
    x ^= x * 0x3D20ADEAu;
    x += seed;
    x *= (seed >> 16) | 1u;
    x ^= x * 0x05526C56u;
    x ^= x * 0x53A22864u;
    return x;
}

uint SampleGeneratorOwenScramble(uint x, uint seed)
{
    x = reversebits(x);
    x = SampleGeneratorOwenHash(x, seed);
    return reversebits(x);
}

struct SampleGeneratorVertexBase
{
    uint baseHash;
    uint sampleIndex;

    static SampleGeneratorVertexBase make(uint2 pixelCoord, uint vertexIndex, uint sampleIndex)
    {
        SampleGeneratorVertexBase ret;
        ret.sampleIndex = sampleIndex;
        ret.baseHash = Hash32Combine(Hash32(vertexIndex + kRandomFrameSalt), (pixelCoord.x << 16) | (pixelCoord.y & 0xFFFFu));
        return ret;
    }

    static SampleGeneratorVertexBase makePacked(uint packedPixel, uint vertexIndex, uint sampleIndex)
    {
        SampleGeneratorVertexBase ret;
        ret.sampleIndex = sampleIndex;
        ret.baseHash = Hash32Combine(Hash32(vertexIndex + kRandomFrameSalt), packedPixel);
        return ret;
    }
};

struct UniformSampleSequenceGenerator
{
    uint currentHash;

    static UniformSampleSequenceGenerator make(SampleGeneratorVertexBase base, uint effectSeed, uint subSampleCount)
    {
        UniformSampleSequenceGenerator ret;
        ret.currentHash = Hash32Combine(base.baseHash, effectSeed);
        ret.currentHash = Hash32Combine(ret.currentHash, base.sampleIndex * subSampleCount);
        return ret;
    }

    uint Next()
    {
        currentHash = Hash32(currentHash);
        return currentHash;
    }
};

struct SampleSequenceGenerator
{
    uint startingHash;
    uint currentHash;
    uint activeIndex;
    uint dimension;

    static SampleSequenceGenerator make(SampleGeneratorVertexBase base, uint effectSeed, bool lowDiscrepancy, uint subSampleCount)
    {
        SampleSequenceGenerator ret;
        ret.activeIndex = base.sampleIndex * subSampleCount;
        ret.currentHash = Hash32Combine(base.baseHash, effectSeed);
        ret.startingHash = ret.currentHash;

        if (lowDiscrepancy)
        {
            ret.dimension = 0u;
        }
        else
        {
            ret.currentHash = Hash32Combine(ret.currentHash, ret.activeIndex);
            ret.dimension = kSampleGeneratorLDDisabled;
        }

        return ret;
    }

    uint Next()
    {
        if (dimension >= kSampleGeneratorLDDisabled)
        {
            currentHash = Hash32(currentHash);
            return currentHash;
        }

        uint shuffleSeed = Hash32Combine(currentHash, 0u);
        uint dimensionSeed = Hash32Combine(currentHash, 1u + dimension);
        uint shuffledIndex = SampleGeneratorOwenScramble(activeIndex, shuffleSeed);

        uint sampleBits = dimension == 0u ? reversebits(shuffledIndex) : SampleGeneratorSobol(shuffledIndex, dimension);
        sampleBits = SampleGeneratorOwenScramble(sampleBits, dimensionSeed);

        dimension++;
        if (dimension >= kSampleGeneratorMaxLDDimension)
        {
            currentHash = Hash32Combine(currentHash, activeIndex);
            dimension = kSampleGeneratorLDRanOutOfDimensions;
        }

        return sampleBits;
    }
};

float UniformSampleNext1D(inout UniformSampleSequenceGenerator sampleGenerator)
{
    return Hash32ToFloat(sampleGenerator.Next());
}

float2 UniformSampleNext2D(inout UniformSampleSequenceGenerator sampleGenerator)
{
    float2 sample;
    sample.x = UniformSampleNext1D(sampleGenerator);
    sample.y = UniformSampleNext1D(sampleGenerator);
    return sample;
}

float4 UniformSampleNext4D(inout UniformSampleSequenceGenerator sampleGenerator)
{
    float4 sample;
    sample.x = UniformSampleNext1D(sampleGenerator);
    sample.y = UniformSampleNext1D(sampleGenerator);
    sample.z = UniformSampleNext1D(sampleGenerator);
    sample.w = UniformSampleNext1D(sampleGenerator);
    return sample;
}

float SampleSequenceNext1D(inout SampleSequenceGenerator sampleGenerator)
{
    return Hash32ToFloat(sampleGenerator.Next());
}

float2 SampleSequenceNext2D(inout SampleSequenceGenerator sampleGenerator)
{
    float2 sample;
    sample.x = SampleSequenceNext1D(sampleGenerator);
    sample.y = SampleSequenceNext1D(sampleGenerator);
    return sample;
}

float4 SampleSequenceNext4D(inout SampleSequenceGenerator sampleGenerator)
{
    float4 sample;
    sample.x = SampleSequenceNext1D(sampleGenerator);
    sample.y = SampleSequenceNext1D(sampleGenerator);
    sample.z = SampleSequenceNext1D(sampleGenerator);
    sample.w = SampleSequenceNext1D(sampleGenerator);
    return sample;
}

void GenerateScatterBSDFSamples(
    uint2 pixelCoord,
    uint sampleIndex,
    uint vertexIndex,
    uint diffuseBounceCount,
    out float4 preGeneratedSamples,
    out float2 extraSamples)
{
    SampleGeneratorVertexBase base = SampleGeneratorVertexBase::make(pixelCoord, vertexIndex, sampleIndex);

#if ENABLE_LOW_DISCREPANCY_SAMPLER_FOR_BSDF
    if (diffuseBounceCount < DISABLE_LOW_DISCREPANCY_SAMPLING_AFTER_DIFFUSE_BOUNCE_COUNT)
    {
        SampleSequenceGenerator sampleGenerator = SampleSequenceGenerator::make(base, SAMPLE_GENERATOR_EFFECT_SCATTER_BSDF, true, 1u);
        preGeneratedSamples = SampleSequenceNext4D(sampleGenerator);
        extraSamples = SampleSequenceNext2D(sampleGenerator);
        return;
    }
#endif

    UniformSampleSequenceGenerator sampleGenerator = UniformSampleSequenceGenerator::make(base, SAMPLE_GENERATOR_EFFECT_SCATTER_BSDF, 1u);
    preGeneratedSamples = UniformSampleNext4D(sampleGenerator);
    extraSamples = UniformSampleNext2D(sampleGenerator);
}

#endif // SAMPLE_GENERATORS_HLSLI
