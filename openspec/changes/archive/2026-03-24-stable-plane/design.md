## Context

当前引擎的路径追踪器（Pathtracing 和 GlobalIllumination 两个 shader）在单一 pass 中完成所有 bounce 的采样，将最终辐射度写入一个 output buffer，然后交由 NRD/DLSS-RR 降噪。Delta Lobe 已实现（`USE_DELTALOBES=1`），完美镜面/玻璃可以正确采样，但降噪器看到的是混合了多个光路的噪声信号——同一个像素可能同时包含直视表面的漫反射和镜面反射中看到的另一个表面的漫反射，它们有完全不同的 motion vector 和法线。

参考实现 RTXPT（`G:\Git\RTXPT`）提供了完整的 Stable Plane 系统，包含 BUILD/FILL 两遍渲染、Branch ID 时域匹配、per-plane 降噪等。本设计将该系统适配到当前引擎架构中。

当前引擎与 RTXPT 的关键差异：
- RTXPT 使用 Donut 框架 + NVRHI 抽象层；当前引擎基于 Creation Engine 的自有渲染框架
- RTXPT 的光源系统使用 polymorphic light + light sampler；当前引擎使用解析光源（directional + point lights）
- RTXPT 有 SER（Shader Execution Reordering）支持；当前引擎可能不使用
- RTXPT 使用 V-Buffer；当前引擎使用标准 hit shader
- 当前引擎有 SHaRC irradiance caching 集成；RTXPT 没有

## Goals / Non-Goals

**Goals:**
- 实现最多 3 个 Stable Plane 的分解系统，每个 plane 有独立 GBuffer 数据
- 实现 BUILD pass：确定性 delta 路径追踪 + Branch ID 编码 + 平面分配
- 实现 FILL pass：从 plane 基表面开始随机采样 + per-plane radiance 累积
- 将 StableRadiance（沿 delta 路径的无噪声 emissive）与 per-plane 降噪输出正确合成
- 保持 SHaRC、SSS、水体体积吸收等现有功能完整可用
- 为后续 ReSTIR 实现提供 per-plane 数据基础

**Non-Goals:**
- 本阶段不实现 ReSTIR（DI 或 GI）
- 不修改 DLSS-RR 管线的 per-plane 支持（DLSS-RR 将使用合并后的单一信号）
- 不实现 PSR（Primary Surface Replacement）— 先用 plane 0 代表主表面
- 不实现 motion vector blocking（`isPSDBlockMotionVectorsAtSurface`）
- 不修改 GI shader（`GlobalIllumination/RayGeneration.hlsl`）— GI 从 rasterized GBuffer 开始，没有 primary ray 的 delta 路径探索

## Decisions

### D1: Buffer 布局和寻址 — 采用 Scanline 模式

**决策**: 使用简单的 scanline 寻址（`address = y * width + x + planeIndex * width * height`），而非 RTXPT 的 Morton/Tiled 模式。

**理由**: Morton 寻址对 cache locality 有一定帮助，但增加了实现复杂度。当前引擎的 thread group 分发已经使用 ThreadGroupTilingX 来优化局部性。Scanline 寻址足够且更易调试。后续如有性能瓶颈再考虑 Morton。

### D2: PATH_TRACER_MODE 编译时分支

**决策**: 通过 `#define PATH_TRACER_MODE` 编译时宏控制 BUILD/FILL 行为，与 RTXPT 相同。

- `PATH_TRACER_MODE_BUILD_STABLE_PLANES = 1`：BUILD pass
- `PATH_TRACER_MODE_FILL_STABLE_PLANES = 2`：FILL pass  
- `PATH_TRACER_MODE_REFERENCE = 0`（或未定义）：原始单 pass 模式（保留作为 fallback/debug）

**理由**: 编译时分支避免运行时条件判断的 GPU 分支开销，BUILD 和 FILL 的代码路径差异很大，共用会导致大量无用代码被执行。同时保留原始模式（MODE_REFERENCE）作为对比和调试手段。

### D3: StablePlane 数据结构 — 80 字节与 RTXPT 对齐

**决策**: 采用与 RTXPT 相同的 80 字节 StablePlane 结构体（20 个 dword），使用相同的 fp16 packing 方式。

**理由**: 
1. 证明可行的设计，减少适配风险
2. 字段完整覆盖降噪器所需信息（法线、粗糙度、motion vector、diff/spec BSDF estimate）
3. 80 字节可接受——对于 1920×1080、3 planes，总共约 500MB 显存

### D4: BUILD pass 不使用 NEE 和 Russian Roulette

**决策**: BUILD pass 中禁用 NEE（包括 delta lobe lighting）和 Russian Roulette，仅累积 emissive（包括天空）到 StableRadiance。

**理由**:
1. BUILD pass 只追踪确定性 delta 路径，不做随机采样——保证时域稳定性
2. NEE 在 BUILD pass 中没有意义——如果表面是 delta-only 的，标准 NEE 返回 0；delta lobe lighting 可以在 FILL pass 处理
3. Russian Roulette 引入随机性，破坏 stable plane 的确定性
4. StableRadiance 只收集 emissive hit（发光体、天空），这些是无噪声数据

### D5: FILL pass 从 plane 0 开始，通过 Branch ID 切换到其他 plane

**决策**: FILL pass 的入口固定为 plane 0。当路径的 Branch ID 与其他 plane 匹配时自动切换。

**理由**: 与 RTXPT 一致。plane 0 总是主表面（或通过 PSR 替换后的表面），是最常见的起始点。其他 plane（1、2）通过 delta 路径分叉产生，FILL 时从 plane 0 的 delta 路径出发，通过 `StablePlanesOnScatter` 中的 Branch ID 匹配自动路由到正确的 plane。

### D6: imageXform 用于运动向量计算

**决策**: 在 delta bounce 时累积 3×3 旋转矩阵（`imageXform`），用于将反射/折射后表面的世界坐标变换到"虚拟世界位置"，从而计算出正确的屏幕空间运动向量。

**理由**: 降噪器需要运动向量来做时域重投影。镜面反射中看到的表面实际位于镜面后方的虚拟位置，其运动向量不能直接用镜面表面的运动向量。通过 imageXform 将反射坐标映射到虚拟坐标可以计算出正确的 MV。

### D7: 仅修改 Pathtracing shader，不修改 GI shader

**决策**: Stable Plane 只在 `Pathtracing/RayGeneration.hlsl` 中实现。`GlobalIllumination/RayGeneration.hlsl` 保持不变。

**理由**: GI shader 从光栅化 GBuffer 重建表面，没有 primary ray，因此没有 delta 路径探索的需求。GI 的 bounce 循环中 delta 表面处理已经通过 Delta Lobe 实现完成。

### D8: SHaRC 在 BUILD pass 中禁用，在 FILL pass 中正常工作

**决策**: SHaRC Update 和 Query 仅在 FILL pass 中启用。BUILD pass 不使用 SHaRC。

**理由**: SHaRC 是间接照明缓存，BUILD pass 只关心确定性 delta 路径，不需要缓存。FILL pass 的随机 bounce 可以正常使用 SHaRC。

## Risks / Trade-offs

- **[显存增加]** → 3 个 plane × 80 bytes/pixel ≈ 500MB @ 1080p。可通过 `_activeStablePlaneCount` 运行时调整为 1-2 个 plane 来降低
- **[Denoiser 运行时间 3x]** → 每个 plane 独立降噪。减轻方案：plane 1/2 通常覆盖面积很小（只有镜面/玻璃区域），NRD 在大面积空白区域速度很快
- **[SHaRC 兼容性]** → FILL pass 从 plane 基表面开始，SHaRC 的 roughness 累积需要考虑 BUILD 阶段已经经过的 delta bounce。风险较低因为 delta bounce 累积 roughness ≈ 0
- **[SSS 兼容性]** → BUILD pass 中 SSS 表面被视为非 delta 表面（有 diffuse 成分），会被设为 plane 基表面。FILL pass 中 SSS NEE 正常工作。无需特殊处理
- **[与当前 denoiser 管线集成]** → 当前引擎的 NRD 集成可能不支持多实例。需要验证和可能的修改。如果难以实现多实例 NRD，可先用单 plane（dominant plane only）降噪作为中间方案
- **Denoiser 集成**：当前工程内无 NRD 实现。DLSS-RR 路径将合并所有 plane 数据为单一信号输入；无降噪路径直接合成 StableRadiance + noisy radiance。留有 per-plane NRD 接口方法以便后续集成
- **性能影响**：BUILD pass 是额外开销（但只追踪 delta 路径，通常很快），FILL pass 的起始点变化但工作量类似