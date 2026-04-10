StructuredBuffer<float>   InDisplacements  : register(t0);
RWByteAddressBuffer       OutPackedBuffer  : register(u0); // raw bytes

ConstantBuffer<DisplacementCB> cb : register(b0);

[numthreads(64, 1, 1)]
void CSPackUnorm11(uint3 id : SV_DispatchThreadID)
{
    uint triIdx   = id.x;
    const uint steps      = 1u << cb.SubdivLevel;
    const uint microVerts = (steps + 1) * (steps + 2) / 2;

    // Each triangle's packed block:
    // 11 bits * microVerts, rounded up to 4-byte alignment
    uint bitsNeeded  = microVerts * 11;
    uint bytesNeeded = (bitsNeeded + 31) / 32 * 4; // 4-byte aligned
    uint dstBase     = triIdx * bytesNeeded;

    uint bitPos = 0;
    for (uint vi = 0; vi < microVerts; ++vi)
    {
        float f    = InDisplacements[triIdx * microVerts + vi];
        uint  q    = uint(saturate(f) * 2047.0 + 0.5) & 0x7FF; // UNORM11

        // Write 11 bits at bitPos into OutPackedBuffer
        uint byteIdx = dstBase + bitPos / 8;
        uint bitOff  = bitPos % 8;

        // 11 bits spans at most 3 bytes
        uint dword0 = OutPackedBuffer.Load(byteIdx & ~3u);
        // ... bit manipulation to insert q at bitOff ...
        // For clarity: write via 32-bit masked store
        uint shift = bitOff + (byteIdx % 4) * 8;
        // Use InterlockedOr for thread safety if threads share dwords
        // (here they don't since each triangle owns its range)
        OutPackedBuffer.Store(byteIdx & ~3u,
            (OutPackedBuffer.Load(byteIdx & ~3u)) | (q << shift));

        bitPos += 11;
    }
}