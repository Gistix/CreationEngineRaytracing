## Why

当前 Path Tracing 模式下 Motion Vector 完全缺失——BUILD pass 中 `buildMVs` 硬编码为 `float3(0,0,0)`（带有 TODO 注释），GBuffer MV pass 已从活跃渲染管线中注释掉且仅支持纯相机运动。参考 RTXPT 实现，PT 的 Motion Vector 应由 Stable Planes 系统在 BUILD pass 中计算，利用 `imageXform` 累积矩阵正确处理镜面反射/折射后的虚拟运动向量，这是去噪器（NRD/DLSS-RR）和时序复用（TAA/RTXDI）正确工作的基础。

## What Changes

- 在 BUILD pass 中实现基于 Stable Planes 的 Motion Vector 计算：利用 `imageXform` 将物体运动投影到虚拟世界空间，通过当前帧/上一帧 ViewProj 矩阵转换为 2.5D 屏幕空间运动向量
- 新增 `computeMotionVector` 辅助函数，将虚拟世界位置对投影为像素空间运动 + 深度差
- 在 PathTracing pass 的 shader register 中新增 Motion Vector 输出 UAV
- 在 C++ 端为 PathTracing pass 绑定 Motion Vector 输出纹理（复用现有 `GBufferOutput::motionVectors` 或新建专用纹理，格式改为 RGBA16_FLOAT 以支持有符号值和深度分量）
- BUILD pass 中 dominant plane 的 MV 直接写入全局 MotionVectors UAV
- 天空 miss 时基于 `kEnvironmentMapSceneDistance` 计算纯相机运动的 MV

## Capabilities

### New Capabilities
- `pt-motion-vectors`: PT 模式下由 Stable Planes BUILD pass 计算并输出 2.5D 屏幕空间 Motion Vector 的完整流程，包括 shader 辅助函数、UAV 绑定、纹理创建和格式定义

### Modified Capabilities
- `stable-plane-build`: BUILD pass 新增 MV 计算逻辑——在每个 plane 基表面和天空 miss 处计算 `virtualWorldPos` + `virtualWorldMotion`，通过 `imageXform` 变换后调用 `computeMotionVector` 得到 2.5D MV，dominant plane 同时写入全局输出

## Impact

- **Shader 文件**:
  - `shaders/raytracing/include/PathTracerStablePlanes.hlsli` — `StablePlanesHandleHit`/`StablePlanesHandleMiss` 中新增 MV 计算逻辑
  - `shaders/raytracing/Pathtracing/Registers.hlsli` — 新增 MotionVectors UAV 绑定
  - `shaders/raytracing/Pathtracing/RayGeneration.hlsl` — BUILD 初始化计算 primary hit 的 MV，传递给 stable planes 函数；dominant plane 写入 MV UAV
- **C++ 文件**:
  - `src/Pass/Raytracing/PathTracing.cpp` — binding layout 新增 MV UAV 槽位，绑定 MV 纹理
  - `src/Renderer.cpp` — MV 纹理格式改为 RGBA16_FLOAT（支持有符号值）
- **依赖**: 依赖 `Camera.ViewProj`、`Camera.PrevViewProj`、`Camera.RenderSize` 等已有 CameraData 成员，无新外部依赖
