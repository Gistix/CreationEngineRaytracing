#pragma once

#include "PCH.h"

namespace RE
{
    struct BGSObjectLODAttachState {
        void* sourceBlock;
        NiNode* objectNode;
        char pad10;
        bool isAttached;
    };
}