## ADDED Requirements

### Requirement: Branch ID encoding
BUILD pass SHALL 使用 32 位无符号整数编码 delta 路径树的分支标识。Camera 初始 Branch ID 为 `1`（哨兵位）。每经过一次 delta bounce，Branch ID 通过 `(prevID << 2) | deltaLobeIndex` 更新，其中 `deltaLobeIndex` 为 0（transmission）或 1（reflection）。最高有效位的位置除以 2 加 1 等于当前 vertex index。

#### Scenario: 两次反射后的 Branch ID
- **WHEN** 路径经过两次 delta 反射（lobeIndex=1）
- **THEN** Branch ID SHALL 为 `((1 << 2) | 1) << 2) | 1 = 21`，vertex index SHALL 为 3

### Requirement: Delta 路径遍历
BUILD pass SHALL 使用 `EvalDeltaLobes` 获取当前表面的所有 delta lobe（最多 3 个：transmission、reflection、coat reflection）。忽略 throughput 平均值 < 0.001 的 lobe。如果表面有非 delta 成分（`nonDeltaPart > 1e-5`），则该表面被设为 plane 基表面，停止继续 delta 遍历。

#### Scenario: 纯金属镜面表面
- **WHEN** 命中 roughness=0, metallic=1 的表面
- **THEN** EvalDeltaLobes SHALL 返回 1 个 delta reflection lobe，路径 SHALL 继续沿反射方向遍历

#### Scenario: 漫反射表面终止探索
- **WHEN** 命中 roughness=0.5 的漫反射表面
- **THEN** 该表面 SHALL 被设为当前 plane 的基表面，BUILD 路径在此终止

### Requirement: 平面分配策略
当 delta 表面有多个 delta lobe（如玻璃同时有反射和折射）时，BUILD pass SHALL 将一个 lobe 分配给当前路径继续（reuse），其余 lobe 分配给可用的空 plane（通过检查 Header 中 Branch ID 为 `cStablePlaneInvalidBranchID` 的 plane）。如果空 plane 不足，超出的 lobe SHALL 被丢弃。

#### Scenario: 玻璃表面有 2 个 delta lobe，1 个空 plane 可用
- **WHEN** 命中玻璃表面，有 reflection 和 transmission 两个 lobe，plane 1 空闲
- **THEN** 一个 lobe SHALL 分叉到 plane 1（通过 StoreExplorationStart 入队），另一个 lobe SHALL 继续在当前路径上

### Requirement: Delta 路径分叉
`SplitDeltaPath` 函数 SHALL 创建新的路径状态，包含：更新后的 Branch ID、新的射线方向（delta lobe 方向）、新的射线原点（带 offset）、更新后的 throughput（`oldThp × lobe.thp`）、更新后的 `imageXform`（反射时使用镜面矩阵，折射时使用旋转矩阵）。

#### Scenario: 反射分叉更新 imageXform
- **WHEN** delta 反射分叉发生在法线为 (0,1,0) 的表面
- **THEN** imageXform SHALL 乘以绕该法线的反射矩阵

### Requirement: StableRadiance 累积
BUILD pass 中命中的 emissive 表面和天空的辐射度 SHALL 累积到 `StableRadiance` texture（per-pixel shared across all planes），而非 per-plane noisy radiance。

#### Scenario: Delta 路径命中天空
- **WHEN** delta bounce 后的射线 miss 所有几何体
- **THEN** sky radiance × throughput SHALL 被加到 StableRadiance[pixel]

### Requirement: 平面基表面存储
当 delta 路径在表面终止（表面有非 delta 成分或达到深度限制）时，BUILD pass SHALL 调用 `StoreStablePlane` 写入该 plane 的 StablePlane 数据，包括：射线原点/方向、场景长度、throughput、运动向量（通过 imageXform 变换后的虚拟世界位置计算）、法线（通过 imageXform 变换）、粗糙度、diff/spec BSDF estimate。

#### Scenario: 镜面反射后命中漫反射墙
- **WHEN** delta 反射后命中 roughness=0.3 的墙面
- **THEN** StablePlane SHALL 存储该墙面的法线（经 imageXform 变换）和 roughness=0.3

### Requirement: Dominant plane 选择
BUILD pass SHALL 维护 dominant plane 标记。Plane 0 默认为 dominant。当 delta 路径分叉时，dominant 标记跟随主 lobe（reflection 优先于 transmission）。最终的 dominant plane index 存储在 StablePlanesHeader slice 3 的低 2 位。

#### Scenario: 玻璃表面分叉
- **WHEN** 玻璃表面分叉为 reflection（plane 0 继续）和 transmission（plane 1）
- **THEN** dominant plane SHALL 保持为 0（跟随 reflection lobe）

### Requirement: BUILD pass 禁用 NEE 和随机采样
BUILD pass SHALL 禁用所有 NEE（EvaluateDirectRadiance、EvalDeltaLobeLighting）、Russian Roulette、和 SHaRC。BUILD pass 仅执行确定性 delta 路径追踪和 emissive 收集。

#### Scenario: BUILD pass 中的 delta-only 表面
- **WHEN** BUILD pass 命中一个 delta-only 表面
- **THEN** 不 SHALL 调用任何 NEE 函数，仅调用 EvalDeltaLobes 获取 delta 方向

### Requirement: 深度限制
BUILD pass 的 delta 路径遍历深度 SHALL 受 `maxStablePlaneVertexDepth`（默认 9，上限 `cStablePlaneMaxVertexIndex=15`）限制。超过深度限制的表面直接设为 plane 基表面。

#### Scenario: 达到深度限制
- **WHEN** delta 路径已经经过 9 次 bounce（vertexIndex >= maxStablePlaneVertexDepth）
- **THEN** 当前表面 SHALL 被设为 plane 基表面，不再继续 delta 遍历

### Requirement: 入队探索机制
被分叉到其他 plane 的 delta 路径 SHALL 通过 `StoreExplorationStart` 将路径状态序列化存入该 plane 的 StablePlanesBuffer 位置，并将 Header 中的 Branch ID 设为 `cStablePlaneEnqueuedBranchID`。当前路径终止后，系统通过 `FindNextToExplore` 扫描入队的 plane，反序列化路径状态并继续探索。

#### Scenario: 主路径终止后探索分叉路径
- **WHEN** plane 0 的主路径终止，plane 1 有入队的探索请求
- **THEN** 系统 SHALL 恢复 plane 1 的路径状态并继续 delta 遍历