## 1. C++ 端纹理与绑定

- [x] 1.1 在 `src/Renderer.h` 中为 PT MotionVectors 纹理添加成员声明（`nvrhi::TextureHandle ptMotionVectors`）
- [x] 1.2 在 `src/Renderer.cpp` 中创建 PT MotionVectors 纹理（RGBA16_FLOAT 格式，renderWidth × renderHeight，debugName = "PT MotionVectors"）
- [x] 1.3 在 `src/Pass/Raytracing/PathTracing.cpp` 的 binding layout 中新增 UAV 槽位（slot 8）
- [x] 1.4 在 `src/Pass/Raytracing/PathTracing.cpp` 的 binding set 中绑定 ptMotionVectors 纹理到 u8

## 2. Shader Register 声明

- [x] 2.1 在 `shaders/raytracing/Pathtracing/Registers.hlsli` 中新增 `RWTexture2D<float4> MotionVectors : register(u8)`（仅在非 SHARC_UPDATE 条件下）

## 3. computeMotionVector 辅助函数

- [x] 3.1 在 `shaders/raytracing/include/PathTracerStablePlanes.hlsli` 中实现 `computeMotionVector(float3 posW, float3 prevPosW)` 函数，使用 Camera.ViewProj / Camera.PrevViewProj 投影，输出 float3(pixelDisplacementXY, viewDepthDelta)

## 4. BUILD pass MV 计算集成

- [x] 4.1 修改 `StablePlanesHandleHit` 函数：在每个 `StoreStablePlane` 调用前，计算 `virtualWorldPos = rayOrigin + rayDir * totalSceneLength`，调用 `computeMotionVector` 得到 MV，替换传入的 motionVectors 参数；当 isDominant 时写入 `MotionVectors[pixelPos]`
- [x] 4.2 修改 `StablePlanesHandleMiss` 函数：使用 `rayOrigin + rayDir * kEnvironmentMapSceneDistance` 作为 virtualWorldPos，计算 MV 并存储；当 isDominant 时写入 `MotionVectors[pixelPos]`
- [x] 4.3 修改 `RayGeneration.hlsl` BUILD 模式初始 miss 处理（`!sourcePayload.Hit()` 分支）：将 `float3(0,0,0)` MV 替换为通过 `computeMotionVector` 计算的天空 MV
- [x] 4.4 修改 `RayGeneration.hlsl` BUILD 模式 `buildMVs` 初始化：移除 `float3(0,0,0)` 硬编码 TODO 注释，改为 `computeMotionVector` 计算 primary hit 的 MV

## 5. 验证

- [x] 5.1 编译验证：确保所有 PathTracing shader 变体（BUILD/FILL/REFERENCE, 含/不含 SHARC）编译无错误
- [ ] 5.2 运行时验证：确认 MotionVectors 纹理有非零值输出（静止相机时接近零，移动相机时有明显值）
