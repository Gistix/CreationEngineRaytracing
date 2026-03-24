## ADDED Requirements

### Requirement: Per-plane 降噪接口预留
系统 SHALL 在 `StablePlanesContext` 和 `StablePlane` 中保留 per-plane 降噪所需的数据访问方法（`GetNoisyDiffRadiance`、`GetNoisySpecRadiance`、BSDF estimate 读写接口），以便后续集成 NRD 时使用。当前阶段不实现 NRD per-plane 降噪。

#### Scenario: 接口方法可用但不被 NRD 调用
- **WHEN** 当前阶段的渲染管线运行
- **THEN** per-plane 降噪接口方法 SHALL 存在于 StablePlane 结构体中，但不被 NRD 降噪管线调用

### Requirement: DLSS-RR 合成路径
当使用 DLSS-RR 降噪时，系统 SHALL 将 StableRadiance 加上所有 plane 的 noisy radiance 合并为单一信号，作为 DLSS-RR 的输入。Guide buffers（diffuse albedo, specular albedo, normals, roughness）SHALL 使用 dominant plane 的数据。

#### Scenario: DLSS-RR 接收合并后的 radiance
- **WHEN** DLSS-RR 降噪启用
- **THEN** DLSS-RR 输入 SHALL 等于 `StableRadiance + Σ(plane i) noisyRadiance_i`

#### Scenario: Guide buffers 使用 dominant plane
- **WHEN** dominant plane 为 plane 0
- **THEN** DLSS-RR 的 normal/roughness guide SHALL 来自 plane 0 的 StablePlane 数据

### Requirement: 无降噪直接合成路径
当降噪器禁用时，系统 SHALL 提供 NoDenoiserFinalMerge 逻辑，直接将 StableRadiance 加上所有 plane 的 noisy radiance 作为最终输出。

#### Scenario: 降噪器关闭
- **WHEN** 用户禁用降噪
- **THEN** OutputColor SHALL 等于 `StableRadiance + Σ(plane i) noisyRadiance_i`

### Requirement: 空 plane 跳过合成
如果某个 plane 的 Header Branch ID 为 `cStablePlaneInvalidBranchID`（无数据），该 plane SHALL 在合成时被跳过，不贡献 radiance。

#### Scenario: 大部分像素只有 plane 0
- **WHEN** 场景中只有少量镜面/玻璃表面
- **THEN** 大部分像素的 plane 1/2 SHALL 被跳过，不贡献任何 radiance

### Requirement: 渲染管线顺序
完整渲染管线 SHALL 按以下顺序执行：① BUILD pass → ② FILL pass → ③ 合成（DLSS-RR 路径或无降噪直接合成）→ ④ 后续后处理（tone mapping 等）。

#### Scenario: BUILD 在 FILL 之前
- **WHEN** 新帧开始路径追踪
- **THEN** BUILD pass SHALL 在 FILL pass 之前执行完毕，确保所有 plane 数据就绪

### Requirement: Diff/Spec 分离启发式
StablePlane 的 `PackedNoisyRadianceAndSpecAvg` 字段中的第四分量（specular average）SHALL 被正确累积。系统 SHALL 提供 `GetNoisyDiffRadiance` 和 `GetNoisySpecRadiance` 方法，通过 `specAvg / totalAvg` 比例启发式分离 diffuse 和 specular radiance。这些方法可供 DLSS-RR guide buffer 生成和后续 NRD 降噪使用。

#### Scenario: 金属表面的 radiance 分类
- **WHEN** plane 的 specular average 接近 total average
- **THEN** `GetNoisySpecRadiance` SHALL 返回接近总 radiance 的值