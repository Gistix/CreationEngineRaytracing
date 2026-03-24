## ADDED Requirements

### Requirement: 从 StablePlane 恢复路径状态
FILL pass SHALL 从 `StablePlanesBuffer` 中加载 plane 0 的数据，恢复路径状态包括：射线原点/方向、vertex index、throughput、scene length、Branch ID。使用存储的 `LastRayTCurrent` 设置窄范围的 re-trace 窗口（`tCurrent × [0.99, 1.01]`），高效重新找到表面。

#### Scenario: 恢复 plane 0 的路径状态
- **WHEN** FILL pass 开始
- **THEN** 路径 SHALL 从 plane 0 的存储位置和方向开始追踪，throughput 为 BUILD pass 累积的值

### Requirement: Branch ID 匹配与 plane 切换
FILL pass 在每次 BSDF 采样后 SHALL 检查当前 Branch ID 是否匹配任何已存储 plane 的 Branch ID。如果精确匹配（`StablePlaneIsOnPlane`），路径 SHALL 切换到该 plane，将已累积的 noisy radiance 提交到之前的 plane，并重置路径 radiance 累加器。如果是前缀匹配（`StablePlaneIsOnStablePath`），路径保持在当前分支上继续前进。

#### Scenario: FILL pass 中的 delta bounce 匹配 plane 1
- **WHEN** FILL 路径经过 delta bounce 后 Branch ID 恰好等于 plane 1 的 Branch ID
- **THEN** 路径 SHALL 切换到 plane 1，在 plane 1 开始新的 radiance 累积

#### Scenario: 无匹配则脱离稳定分支
- **WHEN** FILL 路径的 Branch ID 不匹配任何 plane 且不是任何 plane 的前缀
- **THEN** 路径 SHALL 标记为已脱离稳定分支，后续 radiance 存入最后匹配的 plane

### Requirement: Per-plane noisy radiance 累积
FILL pass 中的随机采样 radiance（NEE + emissive hit）SHALL 累积到当前活跃 plane 的 `PackedNoisyRadianceAndSpecAvg` 字段。Radiance 包含 RGB 通道和一个 specular average 分量（用于 diff/spec 分离启发式）。

#### Scenario: 漫反射 bounce 的 NEE radiance
- **WHEN** FILL pass 在 plane 0 的基表面上进行 NEE
- **THEN** NEE 返回的 radiance × throughput SHALL 被加到 plane 0 的 noisy radiance 中

### Requirement: 跳过 BUILD 已收集的 emissive
FILL pass 在稳定分支上（`stablePlaneOnBranch=true`）时 SHALL 跳过 emissive 累积，因为这些 emissive 已经在 BUILD pass 的 StableRadiance 中收集。只有脱离稳定分支后才累积 emissive。

#### Scenario: FILL 路径在 delta 分支上命中 emissive
- **WHEN** FILL 路径仍在稳定分支上，命中 emissive 表面
- **THEN** 该 emissive SHALL 不被累积到 noisy radiance（已在 StableRadiance 中）

### Requirement: CommitDenoiserRadiance
当路径切换到新 plane 或路径终止时，FILL pass SHALL 调用 `CommitDenoiserRadiance` 将路径累加器中的 radiance 写入当前 plane 的 StablePlane 数据。写入使用原子加法（fp16 packed add），支持多 sample 累积。

#### Scenario: 路径终止时提交 radiance
- **WHEN** FILL 路径因 Russian Roulette 终止
- **THEN** 已累积的 radiance SHALL 被提交到当前活跃 plane

### Requirement: NEE 和 Delta Lobe Lighting 正常工作
FILL pass 中的 NEE SHALL 正常工作：非 delta 表面使用 `EvaluateDirectRadiance`，delta 表面使用 `EvalDeltaLobeLighting`。所有 radiance 贡献按 throughput 缩放后存入当前 plane。

#### Scenario: FILL pass 在 plane 基表面的 NEE
- **WHEN** FILL pass re-trace 到 plane 0 的基表面（roughness > 0）
- **THEN** EvaluateDirectRadiance SHALL 被调用，返回的 radiance × throughput 存入 plane 0

### Requirement: Sky miss 处理
FILL pass 中当 plane 的 SceneLength 为无穷大时（表示 BUILD pass 中该路径 miss 到天空），FILL pass SHALL 直接处理 miss 而不追踪新射线。

#### Scenario: Plane 0 是天空 miss
- **WHEN** plane 0 的 SceneLength 为 +inf
- **THEN** FILL pass SHALL 直接以存储的方向采样天空，乘以 throughput，不追踪射线