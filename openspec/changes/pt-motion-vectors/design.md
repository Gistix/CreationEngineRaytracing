## Context

当前 CreationEngine Path Tracing 管线已实现 Stable Planes 系统（BUILD/FILL 两阶段），用于通过 delta (mirror/glass) 表面正确追踪和去噪。Stable Planes 的数据结构中已预留了 Motion Vector 字段（`PackedThpAndMVs` 中的 fp16 packed MV），`imageXform` 累积矩阵也已实现，但 MV 计算本身尚未完成——BUILD pass 中 `buildMVs` 硬编码为 `float3(0,0,0)`。

参考实现 RTXPT 中，MV 在 BUILD pass 的每个 stable plane 基表面/天空 miss 处计算：
1. 利用累积的 `imageXform` 将物体运动变换到虚拟空间
2. 通过 `computeMotionVector(virtualWorldPos, virtualWorldPos + virtualWorldMotion)` 投影到屏幕空间
3. Dominant plane 的 MV 写入全局 `MotionVectors` 纹理供 TAA/DLSS/RTXDI 使用

**关键约束**: 当前代码库无逐物体前帧变换（`Instance` 中无 `PrevTransform`，`Vertex` 中无 `PrevPosition`），因此初始实现仅支持**纯相机运动**的 MV。这仍然显著优于当前的零 MV，因为相机运动是最常见的时序不稳定来源。

## Goals / Non-Goals

**Goals:**
- 在 BUILD pass 中为每个 stable plane 计算正确的 2.5D 屏幕空间 Motion Vector（相机运动部分）
- 通过 `imageXform` 正确处理镜面/玻璃后的虚拟位置投影
- Dominant plane 的 MV 写入专用 UAV 输出，供下游消费
- 天空 miss 的 MV 正确反映无限远距离处的相机运动
- Per-plane MV 正确存储在 StablePlane 中，为后续 NRD per-plane 降噪做准备

**Non-Goals:**
- 不实现逐物体运动向量（需要 `Instance.PrevTransform` 基础设施，属于独立 change）
- 不实现 Motion Vector Blocking（`isPSDBlockMotionVectorsAtSurface`，参考 RTXPT 的高曲率表面 MV 冻结机制）
- 不实现 DLSS-RR Specular Motion Vectors（`ComputeSpecularMotionVector`，需要 DLSS-RR 集成完成后再做）
- 不实现 DenoiserMotionVectors 单独纹理输出（NRD per-plane MV 输出属于降噪集成 change）
- 不修改 GBuffer pass（已被注释掉，不影响）

## Decisions

### D1: MV 计算位置 — 在 `StablePlanesHandleHit`/`HandleMiss` 内部计算

**选择**: 在 `PathTracerStablePlanes.hlsli` 的 `StablePlanesHandleHit` 和 `StablePlanesHandleMiss` 函数内部，存储 plane 之前计算 MV。

**理由**: 这些函数已经拥有计算 MV 所需的所有上下文（`imageXform`、`rayOrigin`、`rayDir`、`sceneLength`、`throughput`）。在此处计算避免了将额外参数向上传递到 `RayGeneration.hlsl`。参考 RTXPT 的 `StablePlanesHandleHit` 也采用相同模式。

**替代方案**: 在 `RayGeneration.hlsl` 中每次 `StablePlanesHandleHit` 返回后计算 MV——但需要暴露内部状态，增加接口复杂度。

### D2: MV 输出 UAV 槽位 — 使用 u8

**选择**: 在 PT Registers 中新增 `RWTexture2D<float4> MotionVectors : register(u8)`。

**理由**: u0-u7 已被占用（Output, DiffuseAlbedo, SpecularAlbedo, NormalRoughness, SpecularHitDistance, StablePlanesHeader, StablePlanesBuffer, StableRadiance）。u8 是下一个可用槽位。使用 `float4` 以匹配 2.5D MV 格式（xy=屏幕像素位移, z=深度差, w=reserved）。

### D3: MV 纹理格式 — RGBA16_FLOAT

**选择**: 新建专用的 MV 纹理，格式 `RGBA16_FLOAT`，而非复用 GBuffer 的 `R11G11B10_FLOAT`。

**理由**: `R11G11B10_FLOAT` 无符号位，无法表示负方向运动（MV 必须可正可负）。`RGBA16_FLOAT` 提供足够精度和符号支持，且与 RTXPT 参考实现一致。GBuffer 的 MV 纹理仍保留但不使用。

### D4: 全局 MotionVectors UAV 直接传入 HandleHit/HandleMiss

**选择**: 通过全局 UAV 变量（Registers.hlsli 中声明）直接在 HandleHit/HandleMiss 中写入，当 `isDominant=true` 时写入 `MotionVectors[pixelPos]`。

**理由**: 避免通过函数返回值传递 MV 后在 RayGeneration.hlsl 中写入，减少代码改动量。全局 UAV 在 shader 中可见，函数内可直接访问。

**替代方案**: HandleHit 返回 MV，由 RayGeneration 写入——更纯粹但增加接口变更。

### D5: 纯相机运动 MV 的计算方式

**选择**: 对于 primary hit 和所有 delta 路径终点：
```hlsl
float3 virtualWorldPos = cameraPos + cameraDir * sceneLengthForMVs;
float3 virtualWorldMotion = float3(0,0,0); // 无逐物体运动
float3 motionVectors = computeMotionVector(virtualWorldPos, virtualWorldPos + virtualWorldMotion);
```
即虚拟世界位置不变（因无物体运动），MV 完全来自 ViewProj / PrevViewProj 矩阵差异（即相机移动/旋转）。

**理由**: 当 `virtualWorldMotion = 0` 时，`computeMotionVector` 本质上计算的是 "同一世界点在两帧中的屏幕位置差"，这恰好等于纯相机运动导致的像素偏移。

### D6: `computeMotionVector` 函数签名与实现

**选择**: 参考 RTXPT 的 `Bridge::computeMotionVector`，实现为：
```hlsl
float3 computeMotionVector(float3 posW, float3 prevPosW)
{
    float4 clipPos = mul(float4(posW, 1), Camera.ViewProj);
    clipPos.xyz /= clipPos.w;
    float4 prevClipPos = mul(float4(prevPosW, 1), Camera.PrevViewProj);
    prevClipPos.xyz /= prevClipPos.w;
    float3 motion;
    motion.xy = (prevClipPos.xy - clipPos.xy) * float2(0.5, -0.5) * Camera.RenderSize;
    motion.z = prevClipPos.w - clipPos.w;
    return motion;
}
```

**理由**: 直接使用 `Camera.ViewProj`（非 `ViewProj * NoOffset`），因为当前 CameraData 中 `ViewProj` 即为 unjittered 矩阵（C++ 端赋值自 `viewProjMatrixUnjittered`）。输出 xy 为像素空间位移，z 为线性深度差，与 RTXPT 一致。

## Risks / Trade-offs

- **[Risk] 无逐物体运动** → 动态物体的 MV 会不正确（仅反映相机运动）。缓解：这是已知限制，标记为后续 change `per-object-motion-vectors` 的前置工作。
- **[Risk] imageXform 精度对 MV 的影响** → fp16 压缩的 imageXform 可能在多次 delta bounce 后累积误差。缓解：参考 RTXPT 同样使用 fp16 压缩，实践中可接受；多数场景 delta bounce 深度 ≤ 3。
- **[Risk] R11G11B10 格式的 GBuffer MV 纹理仍在创建** → 浪费显存。缓解：GBuffer 目前不在活跃管线中，后续可清理。当前不修改以避免副作用。
- **[Trade-off] 全局 UAV 写入 vs 返回值传递** → 全局写入减少接口改动但降低函数纯度。可接受，因 shader 中 UAV 全局访问是常见模式。
