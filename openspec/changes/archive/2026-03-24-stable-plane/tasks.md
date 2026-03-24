## 1. 数据结构和常量定义

- [x] 1.1 创建 `shaders/raytracing/include/StablePlanes.hlsli`：定义 `cStablePlaneCount`、`cStablePlaneMaxVertexIndex`、`cStablePlaneInvalidBranchID`、`cStablePlaneEnqueuedBranchID` 常量，以及 `PATH_TRACER_MODE_BUILD_STABLE_PLANES=1`、`PATH_TRACER_MODE_FILL_STABLE_PLANES=2`、`PATH_TRACER_MODE_REFERENCE=0` 模式常量
- [x] 1.2 在 `StablePlanes.hlsli` 中定义 `StablePlane` 结构体（80 字节，20 dwords），包含 fp16 packing 方法（`PackTwoFp32ToFp16`、`Fp32ToFp16`）和 octahedral normal encoding（`NDirToOctUnorm32`、`OctToNDirUnorm32`）
- [x] 1.3 在 `StablePlanes.hlsli` 中定义 `StablePlanesContext` 结构体，包含 buffer 引用和所有操作方法：`StartPixel`、`PixelToAddress`、`LoadStablePlane`、`StoreStablePlane`、`LoadStableRadiance`、`StoreStableRadiance`、`AccumulateStableRadiance`、`GetBranchID`、`SetBranchID`、`LoadFirstHitRayLength`、`StoreFirstHitRayLengthAndClearDominantToZero`、`LoadDominantIndex`、`StoreDominantIndex`
- [x] 1.4 实现 Branch ID 工具函数：`StablePlanesAdvanceBranchID`、`StablePlanesVertexIndexFromBranchID`、`StablePlaneIsOnPlane`、`StablePlaneIsOnStablePath`
- [x] 1.5 实现 `CommitDenoiserRadiance`、`StoreExplorationStart`、`FindNextToExplore`、`ExplorationStart` 等路径序列化/反序列化方法

## 2. GPU Buffer 创建和绑定（C++ 侧）

- [x] 2.1 在渲染目标管理代码中创建 `StablePlanesHeader`（R32_UINT, 2DArray, 4 slices）、`StablePlanesBuffer`（StructuredBuffer, stride=80, count=3×W×H）、`StableRadiance`（RGBA16_FLOAT, 2D）
- [x] 2.2 在 shader binding 设置中将三个资源绑定到 UAV 寄存器（u5, u6, u7），并在 Registers.hlsli 中声明
- [x] 2.3 创建 BUILD pass 的 shader 编译变体（`PATH_TRACER_MODE=1`）和 FILL pass 变体（`PATH_TRACER_MODE=2`）
- [x] 2.4 在渲染管线中添加 BUILD pass dispatch，确保在 FILL pass 之前执行

## 3. BUILD Pass 实现

- [x] 3.1 创建 `shaders/raytracing/include/PathTracerStablePlanes.hlsli`：实现 `SplitDeltaPath` 函数，包括 Branch ID 更新、throughput 传播、imageXform 累积（反射矩阵/折射旋转矩阵）、射线原点偏移
- [x] 3.2 实现 `StablePlanesHandleHit` 函数：评估 delta lobes、过滤微弱 lobe、检查非 delta 成分、分配空闲 plane、入队分叉路径、设置基表面
- [x] 3.3 实现 `StablePlanesHandleMiss` 函数：处理天空 miss，存储 plane 数据（SceneLength=inf），计算虚拟运动向量
- [x] 3.4 修改 `Pathtracing/RayGeneration.hlsl` 添加 BUILD 模式入口：初始化 `StablePlanesContext`，调用 `StartPixel`，在命中时调用 `StablePlanesHandleHit`，在 miss 时调用 `StablePlanesHandleMiss`，禁用 NEE/RR/SHaRC，实现 `postProcessHit` 入队探索循环
- [x] 3.5 实现 BUILD pass 中 emissive/sky 到 StableRadiance 的累积逻辑
- [x] 3.6 实现 dominant plane 追踪和存储

## 4. FILL Pass 实现

- [x] 4.1 实现 `FirstHitFromVBuffer` 函数：从 StablePlane 加载路径状态，设置 re-trace 窗口，处理天空 miss inline
- [x] 4.2 实现 `StablePlanesOnScatter` 函数：在 BSDF 采样后推进 Branch ID，检查所有 plane 匹配，处理 plane 切换和 radiance 提交
- [x] 4.3 修改 `Pathtracing/RayGeneration.hlsl` 添加 FILL 模式入口：调用 `FirstHitFromVBuffer`，在 bounce 循环中调用 `StablePlanesOnScatter`，路径终止时调用 `CommitDenoiserRadiance`
- [x] 4.4 实现 FILL pass 中的 emissive 跳过逻辑（`stablePlaneOnBranch` 时跳过）
- [x] 4.5 确保 SHaRC 在 FILL pass 中正常工作（SHaRC 在 FILL 的 bounce 循环中自然工作，roughness 从 plane base surface 累积）

## 5. 合成管线（DLSS-RR + 无降噪路径）

- [x] 5.1 DLSS-RR 合成：FILL 模式将 `StableRadiance + Σ noisyRadiance_i` 写入 Output，GBuffer (DiffuseAlbedo/SpecularAlbedo/NormalRoughness) 使用 dominant plane 的 base surface 数据，DLSS-RR 外部消费这些纹理无需修改
- [x] 5.2 实现无降噪直接合成路径：FILL 模式最终输出 `GetAllRadiance(idx, true)` = `StableRadiance + Σ noisyRadiance_i`
- [x] 5.3 保留 per-plane 降噪接口方法（`GetNoisyDiffRadiance`、`GetNoisySpecRadiance`、BSDF estimate 解调制/再调制），已在 StablePlane 结构体中实现

## 6. 保持现有功能和集成测试

- [x] 6.1 确保 `PATH_TRACER_MODE_REFERENCE`（mode 0 或未定义）保持原始单 pass 行为不变（已验证：所有 BUILD/FILL 代码被 #if 编译排除，bounce loop、输出完全保持原样）
- [x] 6.2 验证 GI shader（`GlobalIllumination/RayGeneration.hlsl`）不受 Stable Plane 修改影响（已验证：GI 有独立的 Registers.hlsli，不包含任何 StablePlane 相关头文件）
- [x] 6.3 验证 SSS（次表面散射）在 BUILD 和 FILL pass 中正确工作（BUILD 跳过 NEE/SSS，FILL 从 plane base surface 开始完整 bounce loop 包含 SSS）
- [x] 6.4 验证水体体积吸收在 FILL pass 中正确工作（FILL bounce loop 保留完整的 Beer-Lambert 体积追踪逻辑）
- [x] 6.5 验证 SHaRC 在 FILL pass 中正常工作（FILL bounce loop 中 SHaRC Update/Query 代码路径完整保留）
- [x] 6.6 验证 Delta Lobe Lighting（解析光源的 delta 方向检测）在 FILL pass 中正确工作（FILL bounce loop 保留 bounceHasDeltaLobes + EvalDeltaLobeLighting 调用）
- [ ] 6.7 端到端测试：场景中有镜面、玻璃、漫反射表面混合，验证降噪输出正确
