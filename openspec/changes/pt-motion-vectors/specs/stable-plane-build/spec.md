## MODIFIED Requirements

### Requirement: 平面基表面存储
当 delta 路径在表面终止（表面有非 delta 成分或达到深度限制）时，BUILD pass SHALL 调用 `StoreStablePlane` 写入该 plane 的 StablePlane 数据，包括：射线原点/方向、场景长度、throughput、运动向量（通过 `computeMotionVector(virtualWorldPos, virtualWorldPos)` 计算，其中 `virtualWorldPos = cameraRayOrigin + cameraRayDir * sceneLength`，当前仅支持相机运动）、法线（通过 imageXform 变换）、粗糙度、diff/spec BSDF estimate。当 `isDominant=true` 时，计算得到的 MV SHALL 同时写入全局 `MotionVectors[pixelPos]` UAV。

#### Scenario: 镜面反射后命中漫反射墙
- **WHEN** delta 反射后命中 roughness=0.3 的墙面
- **THEN** StablePlane SHALL 存储该墙面的法线（经 imageXform 变换）和 roughness=0.3，且运动向量 SHALL 基于 virtualWorldPos（使用累积 sceneLength）通过 `computeMotionVector` 计算

#### Scenario: 直接命中 dominant 表面写入全局 MV
- **WHEN** primary hit 命中漫反射表面（plane 0, dominant），相机正在移动
- **THEN** StablePlane 中的 MV 和 MotionVectors UAV 中的 MV SHALL 相同，且反映相机运动导致的像素偏移

### Requirement: Delta 路径分叉
`SplitDeltaPath` 函数 SHALL 创建新的路径状态，包含：更新后的 Branch ID、新的射线方向（delta lobe 方向）、新的射线原点（带 offset）、更新后的 throughput（`oldThp × lobe.thp`）、继承的 motion vectors（从 `prevMotionVectors` 传递，不在分叉时重新计算）、更新后的 `imageXform`（反射时使用镜面矩阵，折射时使用旋转矩阵）。

#### Scenario: 反射分叉更新 imageXform
- **WHEN** delta 反射分叉发生在法线为 (0,1,0) 的表面
- **THEN** imageXform SHALL 乘以绕该法线的反射矩阵

#### Scenario: 分叉路径继承父路径的 MV
- **WHEN** delta 路径分叉产生新的 exploration payload
- **THEN** 新 payload 的 motionVectors SHALL 等于父路径的 motionVectors（在存储 plane 时才重新计算）
