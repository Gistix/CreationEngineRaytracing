/*
 * Copyright (c) 2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#define SHARC
#define SHARC_RESOLVE 1

#include "Interop/CameraData.hlsli"
#include "Interop/SHaRCData.hlsli"

#include "Raytracing/Include/SHaRC/SHaRC.hlsli"

#ifndef LINEAR_BLOCK_SIZE
#   define LINEAR_BLOCK_SIZE                   256
#endif

ConstantBuffer<CameraData>                  Camera                    : register(b0);
ConstantBuffer<SHaRCData>                   SHaRC                     : register(b1);

RWStructuredBuffer<uint64_t>                SharcHashEntriesBuffer    : register(u0);
RWStructuredBuffer<SharcAccumulationData>   SharcAccumulationBuffer   : register(u1);
RWStructuredBuffer<SharcPackedData>         SharcResolvedBuffer       : register(u2);

#include "Raytracing/Include/SHaRC/SHaRCHelper.hlsli"

[numthreads(LINEAR_BLOCK_SIZE, 1, 1)]
void Main(in uint2 did : SV_DispatchThreadID)
{
    SharcParameters sharcParameters = GetSharcParameters();
    SharcResolveParameters resolveParameters = GetSharcResolveParameters();

    SharcResolveEntry(did.x, sharcParameters, resolveParameters);
}