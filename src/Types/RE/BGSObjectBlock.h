#pragma once

#include "PCH.h"

namespace RE
{
    class BGSTerrainNode;

    class BGSObjectBlock {
    public:
        // members
        BGSTerrainNode* node;    // 00
        BSMultiBoundNode* chunk; // 08
        bool loaded;             //
        bool attached;           //
    };
}