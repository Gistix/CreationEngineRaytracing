## Why

当前的路径追踪器将所有 bounce 的辐射度混合到单一的 noisy buffer 中，denoiser 无法区分来自不同光路的噪声特征（如：直接可见表面 vs 镜面反射 vs 玻璃折射）。这导致：
1. **镜面/玻璃表面降噪质量差**：不同光路有不同的运动向量、法线和粗糙度，混合后时域降噪无法正确追踪
2. **无法为 ReSTIR 提供干净的 per-plane 数据**：ReSTIR 需要知道每个"可见平面"的独立 GBuffer 信息
3. **Delta bounce 后的 emissive 是无噪声的确定性数据**，不应该和随机采样的间接光混合降噪

Stable Plane 通过将像素分解为多个独立的"稳定平面"（最多3个），每个平面有自己的 GBuffer（法线、粗糙度、运动向量）和独立的降噪通道，从根本上解决这些问题。

## What Changes

- **新增 Stable Plane GPU 缓冲区系统**：包括 `StablePlanesHeader`（per-pixel 2D array texture, R32_UINT, 4 slices）、`StablePlanesBuffer`（StructuredBuffer, 80 bytes/element）、`StableRadiance`（RGBA16_FLOAT 2D texture）
- **新增 BUILD 渲染 pass**：在现有路径追踪前执行 Whitted-style 确定性光线追踪，沿 delta 路径发现并分配最多 3 个稳定平面
- **修改现有路径追踪 pass 为 FILL pass**：从 BUILD pass 发现的平面基表面开始随机采样，将 noisy radiance 存入对应平面
- **新增 Branch ID 系统**：用 32 位编码 delta 路径树结构，实现跨帧时域稳定匹配
- **修改 denoiser 管线**：从单次降噪改为每个平面独立降噪，每个平面有独立的时域历史
- **新增后处理合成 pass**：将降噪后的多个平面辐射度与 StableRadiance 合成为最终图像
- **修改 GBuffer 导出逻辑**：仅导出 dominant plane 的表面数据

## Capabilities

### New Capabilities
- `stable-plane-buffers`: GPU 缓冲区基础设施（StablePlanesHeader, StablePlanesBuffer, StableRadiance）的创建、绑定和寻址
- `stable-plane-build`: BUILD pass 逻辑，包括 delta 路径遍历、Branch ID 计算、平面分配、delta 路径分叉、StableRadiance 累加
- `stable-plane-fill`: FILL pass 逻辑，包括从 StablePlane 恢复路径状态、Branch ID 匹配、平面间切换、per-plane noisy radiance 累积
- `stable-plane-denoise-compose`: 每平面独立降噪和最终合成管线

### Modified Capabilities
<!-- 无现有 specs 需要修改 -->

## Impact

- **Shader 文件**：新增 `StablePlanes.hlsli`（核心数据结构和操作）、`PathTracerStablePlanes.hlsli`（BUILD/FILL 逻辑）；修改 `Pathtracing/RayGeneration.hlsl` 和 `GlobalIllumination/RayGeneration.hlsl` 以支持两遍渲染
- **C++ 渲染管线**：需要修改 render target 创建代码以分配新缓冲区，修改 binding 设置，增加 BUILD pass dispatch，修改 denoiser 循环为 per-plane
- **Denoiser 集成**：NRD 需要从单实例改为多实例（每 plane 一个），每个实例有独立时域历史
- **性能影响**：BUILD pass 是额外开销（但只追踪 delta 路径，通常很快），FILL pass 的起始点变化但工作量类似，denoiser 运行次数增加至 3x
- **依赖**：依赖已完成的 Delta Lobe 实现（`USE_DELTALOBES=1`，`EvalDeltaLobes` 函数）