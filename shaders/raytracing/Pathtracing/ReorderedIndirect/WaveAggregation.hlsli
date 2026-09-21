#ifndef WAVE_AGGREGATION_HLSLI
#define WAVE_AGGREGATION_HLSLI

// Result of wave-level key matching
struct WaveKeyMatch
{
    bool isLeader;      // True for the first lane in the wave that shares this key
    uint matchCount;    // Total number of lanes in this wave that share this key
    uint prefixIndex;   // Contiguous 0-based offset of this lane among all lanes with this key
    uint leaderLane;    // Wave lane index of the leader lane for this key
};

// Groups all active lanes in the wave by identical key value using SM 6.5+ WaveMatch.
// Supports both 32-lane (NVIDIA) and 64-lane (AMD) waves.
inline WaveKeyMatch WaveMatchKey(uint key)
{
    WaveKeyMatch res;
    uint4 match = WaveMatch(key);
    uint laneId = WaveGetLaneIndex();

    // Low 32 lanes match against match.x, high 32 lanes against match.y
    uint matchMask = (laneId < 32u) ? match.x : match.y;
    uint bitInMask = 1u << (laneId & 31u);
    uint prefixMask = bitInMask - 1u;

    if (laneId < 32u)
    {
        res.isLeader = (firstbitlow(match.x) == laneId);
        res.matchCount = countbits(match.x) + countbits(match.y);
        res.prefixIndex = countbits(match.x & prefixMask);
        res.leaderLane = firstbitlow(match.x);
    }
    else
    {
        res.isLeader = (match.x == 0u) && (firstbitlow(match.y) == (laneId - 32u));
        res.matchCount = countbits(match.x) + countbits(match.y);
        res.prefixIndex = countbits(match.x) + countbits(match.y & prefixMask);
        res.leaderLane = (match.x != 0u) ? firstbitlow(match.x) : (32u + firstbitlow(match.y));
    }

    return res;
}

#endif // WAVE_AGGREGATION_HLSLI
