## ADDED Requirements

### Requirement: computeMotionVector 辅助函数
系统 SHALL 在 `PathTracerStablePlanes.hlsli` 中提供 `computeMotionVector(float3 posW, float3 prevPosW)` 函数，将一对世界空间位置（当前帧 / 上一帧）投影为 2.5D 屏幕空间运动向量。函数 SHALL 使用 `Camera.ViewProj` 和 `Camera.PrevViewProj` 分别投影两个位置到 NDC，然后输出 `float3(pixelDisplacementXY, viewDepthDelta)`。pixelDisplacement 的计算公式为 `(prevNDC.xy - currNDC.xy) * float2(0.5, -0.5) * Camera.RenderSize`。viewDepthDelta 为 `prevClip.w - currClip.w`。

#### Scenario: 静止场景、相机不动
- **WHEN** posW 和 prevPosW 相同，且 ViewProj == PrevViewProj
- **THEN** 返回的 motion vector SHALL 为 float3(0, 0, 0)

#### Scenario: 相机平移导致的屏幕运动
- **WHEN** 世界位置不变（posW == prevPosW），但 ViewProj 和 PrevViewProj 因相机平移而不同
- **THEN** 返回的 xy 分量 SHALL 反映该世界点在两帧中的像素位移

### Requirement: BUILD pass 计算 per-plane Motion Vector
BUILD pass 在每个 stable plane 基表面存储时（`StoreStablePlane` 调用前），SHALL 计算该 plane 的 motion vector。计算方式为：`virtualWorldPos = rayOrigin + rayDir * sceneLength`（rayOrigin 为相机位置或 delta 路径起点），当前无逐物体运动时 `virtualWorldMotion = float3(0,0,0)`，最终调用 `computeMotionVector(virtualWorldPos, virtualWorldPos + virtualWorldMotion)` 得到 MV。计算得到的 MV SHALL 传入 `StoreStablePlane` 并存储在 `StablePlane.PackedThpAndMVs` 中。

#### Scenario: Primary hit 直接表面（无 delta bounce）
- **WHEN** 第一个命中表面为漫反射（非 delta），sceneLength 等于 hitDistance
- **THEN** MV SHALL 基于 `Camera.Position + viewDir * hitDistance` 的虚拟位置计算

#### Scenario: 经过镜面反射后的表面
- **WHEN** 路径经过一次 delta 反射后到达基表面
- **THEN** MV 的 virtualWorldPos SHALL 使用累积的 sceneLength（包含反射前后的总距离），MV 值 SHALL 正确反映虚拟深度处的相机运动

### Requirement: BUILD pass 天空 miss 的 Motion Vector
BUILD pass 在天空 miss 时 SHALL 使用 `kEnvironmentMapSceneDistance` 作为场景长度计算 virtualWorldPos，然后调用 `computeMotionVector(virtualWorldPos, virtualWorldPos)` 计算纯相机运动的 MV（天空无物体运动）。

#### Scenario: 直接看天空
- **WHEN** 相机直接看天空（无 delta bounce），相机正在旋转
- **THEN** MV SHALL 反映极远距离处的相机旋转引起的像素偏移

#### Scenario: 镜面反射后看天空
- **WHEN** 路径经过 delta 反射后 miss 到天空
- **THEN** virtualWorldPos SHALL 基于 `rayOrigin + rayDir * kEnvironmentMapSceneDistance` 计算

### Requirement: Dominant plane MV 写入全局输出
BUILD pass 中当 `isDominant=true` 时，计算得到的 MV SHALL 同时写入全局 `MotionVectors` UAV (`MotionVectors[pixelPos] = float4(motionVectors, 0)`)。每个像素仅有一个 dominant plane，因此每像素仅写入一次。

#### Scenario: 简单场景 plane 0 为 dominant
- **WHEN** 场景无 delta 分叉，plane 0 为 dominant
- **THEN** plane 0 的 MV SHALL 被写入 MotionVectors UAV

#### Scenario: 玻璃分叉后 dominant plane
- **WHEN** 玻璃表面分叉为 reflection（plane 0）和 transmission（plane 1），dominant 为 plane 0
- **THEN** 仅 plane 0 的 MV SHALL 写入 MotionVectors UAV

### Requirement: MotionVectors UAV 绑定
PathTracing pass 的 shader register 中 SHALL 声明 `RWTexture2D<float4> MotionVectors : register(u8)`。此 UAV 仅在 `PATH_TRACER_MODE_BUILD_STABLE_PLANES` 模式下被写入。

#### Scenario: BUILD pass 可写入 MotionVectors
- **WHEN** PathTracing shader 以 BUILD 模式编译
- **THEN** MotionVectors UAV SHALL 在 register u8 可访问且可写入

### Requirement: Motion Vector 纹理创建
C++ 端 SHALL 创建专用的 Motion Vector 纹理，格式为 `RGBA16_FLOAT`，大小为 `renderWidth × renderHeight`，命名为 `"PT MotionVectors"`。该纹理 SHALL 作为 UAV 绑定到 PathTracing pass 的 binding set 中对应 u8 槽位。

#### Scenario: 纹理格式支持有符号值
- **WHEN** 相机向左移动导致负 x 方向的 MV
- **THEN** RGBA16_FLOAT 格式 SHALL 能正确存储负值的 MV 分量

### Requirement: 非 BUILD pass 的 MV 清零
当 PathTracing pass 不在 BUILD 模式（即 REFERENCE 或 FILL 模式）时，不 SHALL 写入 MotionVectors UAV。BUILD pass 在主 miss（源射线未命中）时 SHALL 写入零 MV。

#### Scenario: 源射线 miss 天空
- **WHEN** 源射线在 BUILD pass 中直接 miss 到天空（`!sourcePayload.Hit()`）
- **THEN** 天空 miss 处理中计算的 MV SHALL 通过 dominant plane 写入 MotionVectors UAV
