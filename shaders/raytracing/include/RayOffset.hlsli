#ifndef RAY_OFFSET_HLSL
#define RAY_OFFSET_HLSL

#include "raytracing/include/AdvancedSettings.hlsli"

// https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/shaders/rtx/concept/ray/ray_utilities.h

/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// This ray offsetting method is inspired by the "A Fast and Robust Method for Avoiding Self-Intersection"
// article from the Ray Tracing Gems book. The original implementation from that article was found to be
// flawed in multiple ways, but the logic behind it is solid.
//
// When we hit a triangle and compute a position on that triangle from the vertices and the barycentrics,
// the resulting position is inexact, it has some error. You can think of that as a "cloud" of points
// around the triangle plane, and the position can be anywhere inside that cloud, on either side of
// the triangle. In order to avoid self-intersection, we need to apply an offset along the triangle's
// geometric normal that is larger in magnitude than the thickness of this error cloud.
// The magnitude of this error depends primarily on the magnitude of the vertex positions, IOW,
// the further away our triangle is from the world origin, the larger the error will be.
//
// So, we take the maximum magnitude of the position and multiply it by some constant.
// This is different from the code in the RTG article which was dealing with position components
// independently, but that just distorts the normal direction for most triangles, and fails
// on triangles that are coplanar to one of the major planes like Y=0 in particular.
//
// The reason why dealing with per-component errors fails on triangles coplanar to a major plane
// is apparently in the ray intersection math that is happening inside the GPU. At least the approach
// documented in the Vulkan ray tracing spec (*) is based on transforming the primitive positions to
// ray space, which involves multiplying the positions by a matrix. That matrix propagates
// the error from reconstructing the X and Z positions (in case of Y=0 plane) into the ray T.
// * https://www.khronos.org/registry/vulkan/specs/1.1-khr-extensions/html/chap33.html#ray-intersection-candidate-determination


// Calculates the *scaled* approximate error of a float32 position.
// The error is scaled by the 1/kFloatULP constant defined in the calculateRayOffset(...) function,
// which is moved there for efficiency: we don't want to do extra multiplications when combining
// errors from different positions.
// This function should be used on every position value in the chain of transforms, like so:
//
//   float error = calculatePositionError(objectSpacePosition);
//   float3 worldSpacePosition = mul(objectToWorld, objectSpacePosition);
//   error = max(error, calculatePositionError(worldSpacePosition));       <-- update the error
//   ...
//   float3 offset = calculateRayOffset(error, triangleNormal);
//   float3 rayOrigin = worldSpacePosition + offset;
//
float CalculatePositionError(float3 p)
{
    const float maxAbsComponent = max(abs(p.x), max(abs(p.y), abs(p.z)));
    return maxAbsComponent;
}

// This function calculates a ray offset in the direction of the normal, given the error 
// previously computed with the calculatePositionError(p) function above.
// If the offset point is desired on the "inside" of a surface (for example when dealing 
// with translucency or double sided geometry), invert the normal passed in beforehand.
float3 CalculateRayOffset(float positionError, float3 triangleNormal)
{
    // A single ULP (Unit in the Last Place, or Unit of Least Precision) of 32-bit floats, calculated as
    //   ((asfloat(asuint(x) + 1) - x) / x)
    // The actual value is smaller for numbers that are not powers of 2, so we use the largest ULP.
    // It can also be substantially larger for denormals, but we don't really care about them.
    const float kFloatULP = 0.00000011920928955078125; // pow(2.0, -23.0);

    // The original RTG article found that there is some "baseline" error coming from non-position sources,
    // and we account for that by adding a max(kOrigin, ...) term where kOrigin is the point on the
    // error plot in the article where the error switches from plateau to linear dependency on position.
    const float kOrigin = 1.0f / 1024.0f;

    // The kOffsetScale value was determined experimentally as the smallest value that doesn't result in
    // self-intersections in practice. The article claimed that the relative error is in the order of 10^-7,
    // but that is hard to believe because that's just 1 ULP of float32. At the same time, the article
    // was effectively multiplying the normal by 1 / 32768.0 with some cryptic integer math, and that
    // is often too large.
    const float kOffsetScale = 4.0;

    return (max(kOrigin, positionError) * (kFloatULP * kOffsetScale)) * triangleNormal;
}

// The method above seems to cause some self-intersection issues in certain scenarios

// Computes new ray origin based on hit position to avoid self-intersections. 
// The function assumes that the hit position has been computed by barycentric interpolation, and not from the ray t which is less accurate.
// Described in Ray Tracing Gems, Chapter 6, "A Fast and Robust Method for Avoiding Self-Intersection" by Carsten Wächter and Nikolaus Binder.
float3 OffsetRayAlt(float3 worldPosition, float3 faceNormal, bool hasTransmission = false)  // expects triangle faceNormal pointing towards the intended ray direction
{
    if (hasTransmission)
        faceNormal = -faceNormal;
    const float origin = 1.f / 16.f;
    const float fScale = 3.f / 65536.f;
    const float iScale = 3 * 256.f;

    // Per-component integer offset to bit representation of fp32 position.
    int3 iOff = int3(faceNormal * iScale);
    float3 iPos = asfloat(asint(worldPosition) + select(worldPosition < 0.f, -iOff, iOff));

    // Select per-component between small fixed offset or above variable offset depending on distance to origin.
    float3 fOff = faceNormal * fScale;
    return select(abs(worldPosition) < origin, worldPosition + fOff, iPos);
}

float3 OffsetRay(float3 position, float3 normal, float positionError, bool hasTransmission)
{
    float3 offset = CalculateRayOffset(positionError, normal);
    if (hasTransmission)
        offset = -offset;
    return position + offset;
}

float3 OffsetRay(float3 position, float3 normal, bool hasTransmission = false)
{
    return OffsetRay(position, normal, CalculatePositionError(position), hasTransmission);
}

// ============================================================================
// NVIDIA Self-Intersection Avoidance (SIA) offset method
// ============================================================================

// Offset the world-space position along the world-space normal by the SIA-computed
// safe offset. The offset value should come from SIA_SafeSpawnPoint() or
// SIA_SafeSpawnPointSimple().
//
// This replaces the standard OffsetRay for SIA-computed surfaces. The offset is
// already a tight error bound, so no additional scaling or position-error tracking
// is needed.
float3 OffsetRaySIA(float3 wldPosition, float3 wldNormal, float wldOffset, bool hasTransmission = false)
{
    float sign = hasTransmission ? -1.0f : 1.0f;
    precise float3 p = mad(sign * wldOffset, wldNormal, wldPosition);
    return p;
}

// Unified offset function that dispatches to the appropriate method based on
// USE_SIA_INTERPOLATION. Call sites can use this instead of branching manually.
//
// When USE_SIA_INTERPOLATION=1: uses SIA offset (siaOffset parameter, ignores positionError)
// When USE_SIA_INTERPOLATION=0: uses standard offset (positionError parameter, ignores siaOffset)
float3 OffsetRayAuto(float3 position, float3 normal, float positionError, float siaOffset, bool hasTransmission)
{
#if USE_SIA_INTERPOLATION
    return OffsetRaySIA(position, normal, siaOffset, hasTransmission);
#else
    return OffsetRay(position, normal, positionError, hasTransmission);
#endif
}

#endif // RAY_OFFSET_HLSL