## ADDED Requirements

### Requirement: StablePlane data structure
系统 SHALL 定义 `StablePlane` 结构体，大小为 80 字节（20 dwords），包含以下字段：RayOrigin (float3), LastRayTCurrent (float), RayDir (float3), SceneLength (float), PackedThpAndMVs (uint3, fp16 packed throughput + motion vectors), VertexIndexAndRoughness (uint, upper 16=vertex index, lower 16=fp16 roughness), DenoiserPackedBSDFEstimate (uint3, fp16 packed diff+spec BSDF estimates), PackedNormal (uint, octahedral encoded), PackedNoisyRadianceAndSpecAvg (uint2, fp16 packed radiance + specular average), FlagsAndVertexIndex (uint), PackedCounters (uint)。该结构体 MUST 同时在 HLSL 和 C++ 中可用（通过 `__cplusplus` 编译守卫隔离 shader-only 方法）。

#### Scenario: C++ sizeof 与 shader stride 一致
- **WHEN** C++ 代码使用 `sizeof(StablePlane)` 创建 StructuredBuffer
- **THEN** 大小 SHALL 为 80 字节，与 shader 中的结构体 stride 一致

### Requirement: StablePlanesHeader texture
系统 SHALL 创建 `StablePlanesHeader` 资源，类型为 `RWTexture2DArray<uint>`，格式 R32_UINT，大小为 `renderWidth × renderHeight × 4 slices`。Slice 0-2 存储各 plane 的 Branch ID（`0xFFFFFFFF` 表示空/无效）。Slice 3 的高 30 位存储 first hit ray length（`asuint(length) & 0xFFFFFFFC`），低 2 位存储 dominant plane index。

#### Scenario: 初始化后所有 plane 为空
- **WHEN** BUILD pass 调用 `StartPixel` 初始化
- **THEN** slice 0-2 的值 SHALL 为 `cStablePlaneInvalidBranchID`（0xFFFFFFFF）

### Requirement: StablePlanesBuffer structured buffer
系统 SHALL 创建 `StablePlanesBuffer`，类型为 `RWStructuredBuffer<StablePlane>`，元素数为 `cStablePlaneCount × renderWidth × renderHeight`，stride 为 80 字节。

#### Scenario: Buffer 大小满足所有 plane
- **WHEN** 分辨率为 1920×1080，cStablePlaneCount=3
- **THEN** buffer 元素数 SHALL 为 3 × 1920 × 1080 = 6,220,800

### Requirement: StableRadiance texture
系统 SHALL 创建 `StableRadiance` 资源，类型为 `RWTexture2D<float4>`，格式 RGBA16_FLOAT，大小为 `renderWidth × renderHeight`。此 buffer 存储沿 delta 路径收集的无噪声 emissive radiance，所有 plane 共享。

#### Scenario: BUILD pass 重置 StableRadiance
- **WHEN** BUILD pass 开始新帧
- **THEN** StableRadiance SHALL 被清零为 (0,0,0)

### Requirement: Scanline 寻址函数
系统 SHALL 提供 `PixelToAddress(uint2 pixelPos, uint planeIndex)` 函数，返回 `pixelPos.y * renderWidth + pixelPos.x + planeIndex * renderWidth * renderHeight`，用于从像素坐标和 plane 索引计算 StablePlanesBuffer 的线性地址。

#### Scenario: 不同 plane 的地址不重叠
- **WHEN** 像素 (0,0) 访问 plane 0 和 plane 1
- **THEN** 返回的地址差值 SHALL 等于 renderWidth × renderHeight

### Requirement: 常量定义
系统 SHALL 定义以下常量：`cStablePlaneCount = 3`（最大 plane 数）、`cStablePlaneMaxVertexIndex = 15`（最大 delta 路径深度）、`cStablePlaneInvalidBranchID = 0xFFFFFFFF`（无效 plane 标记）、`cStablePlaneEnqueuedBranchID = 0xFFFFFFFE`（已入队等待探索标记）。

#### Scenario: 常量在 shader 和 C++ 中一致
- **WHEN** shader 和 C++ 分别 include 同一定义文件
- **THEN** 所有常量值 SHALL 完全一致