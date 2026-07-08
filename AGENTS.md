# creation-engine-raytracing

## Overview

Ray-traced rendering extensions for Creation Engine (Skyrim, Fallout 4). Adds path tracing and global illumination via DXR ray tracing shaders. Windows platform, HLSL shaders.

## C++ Core Architecture

### BaseMesh Hierarchy (`src/Core/`)

The per-triangle-mesh geometry abstraction. Each `BaseMesh` owns GPU buffers (index/vertex) and a material reference.

```
BaseMesh                              (BaseMesh.h/.cpp)
  |
  +-- DirectMesh                      (DirectMesh.h/.cpp)
  |     |
  |     +-- LandLODMesh               (LandLODMesh.h/.cpp)
  |
  +-- SkinnedMesh                     (SkinnedMesh.h/.cpp)
  |     |
  |     +-- DynamicMesh               (DynamicMesh.h/.cpp)

MeshType: { Base=0, Default=1, Skinned=2, Dynamic=3 }
DirtyFlags: Visibility, Transform, Vertex, Skin, Mesh
```

| Class | Purpose |
|---|---|
| `BaseMesh` | Root: owns `m_Material` (shared_ptr<MaterialBase>), `m_Cluster` back-pointer, `m_LocalTransform`/`m_PrevLocalTransform` (relative to owning BLASCluster). Static factory `Create()` dispatches by rendererData/skin/type. |
| `DirectMesh` | Static (non-skinned) geometry. One index/vertex buffer, one geometry desc. Native engine vertex buffer. |
| `LandLODMesh` | Terrain LOD4 meshes (inherits DirectMesh). Creates live GPU-owned vertex buffer, repoints RT vertex descriptor. `UpdateOcclusion()` computes LandLODUpdate for occluder pass. |
| `SkinnedMesh` | GPU-skinned geometry. Per-mesh vertex buffer, per-partition index buffers + geometry descs. `m_LiveVertexBuffer` (BLAS source), `m_PrevPositionBuffer`, `m_BoneWorlds`/`m_SkinToBones`. Delegates to `Pass::Skinning`. |
| `DynamicMesh` | Skinned + per-frame morphs (`BSDynamicTriShape`). Lives in `DynamicPositions` bindless slot (float4 stride, RGB32_FLOAT). |

### BLASCluster (`src/Core/BLASCluster.h/.cpp`)

Replaces the old 1:1 `Instance`→BLAS mapping. Aggregates all meshes belonging to the same `TESObjectREFR*` owner into a single BLAS / single TLAS instance. Meshes without an owner (null-owner) get degenerate per-mesh clusters ("orphan clusters").

```cpp
class BLASCluster {
    RE::TESObjectREFR* m_Owner;                    // grouping key only
    eastl::vector<BaseMesh*> m_Members;            // weak references
    eastl::vector<nvrhi::rt::GeometryDesc> m_Geom;// aggregated from members
    nvrhi::rt::AccelStructHandle m_BLAS;           // single BLAS per cluster
    float3x4 m_Transform, m_PrevTransform;          // owner-world transform
    float3 m_ClusterPosition;                       // cached for light culling
    float m_ClusterRadius;                          // world-space bounding sphere
    uint32_t m_InstanceIndex;                       // TLAS instance slot
    m_NumUpdatesSinceRebuild;                       // BLAS maintenance counter
};
```

**Lifecycle:**
- `SceneGraph::GetOrCreateCluster(owner, bsTriShape)` — creates or looks up cluster per owner.
- `AddMember(mesh)` / `RemoveMember(mesh)` — sets mesh's back-pointer.
- `Update(sceneGraph)` — computes cluster/world transforms, aggregates geometry descs, writes `MeshData`/`InstanceData` to GPU buffers.
- `BuildUpdate(commandList, sceneGraph)` — decides rebuild vs refit vs maintenance rebuild.
- `MakeInstanceDesc()` → `nvrhi::rt::InstanceDesc` (with `InstanceMask::Default`).

**BLAS Build Decision:**
```
1. Already built this frame? → skip
2. firstBuild || Visibility/Mesh dirty? → REBUILD, reset counter
3. Vertex/Skin/Transform dirty?
   ├── counter >= MAX_BLAS_UPDATES (256)?
   │   ├── TryMaintenanceRebuild() → REBUILD, reset counter
   │   └── false → fallback to UPDATE, counter++
   └── counter < threshold → UPDATE, counter++
4. No dirty flags → skip
5. rebuild: AllowUpdate | update: PerformUpdate
6. BuildBottomLevelAccelStruct()
```

**Constants** (`src/Constants.h`):
- `MAX_BLAS_UPDATES_BEFORE_MAINTENANCE = 256`
- `MAX_BLAS_MAINTENANCE_REBUILDS_PER_FRAME = 8`

### InstanceMask (`src/Types/InstanceMask.h`)
```cpp
enum InstanceMask : uint8_t {
    None    = 0,
    Default = 1 << 0,    // 0x01
    Water   = 1 << 1,    // 0x02
    All     = 0xFF
};
```
- `BLASCluster::MakeInstanceDesc()` sets `InstanceMask::Default` (0x01).
- GI shader defines `INSTANCE_MASK = All & ~Water = 0xFD` to exclude water.

### MaterialManager (`src/Core/MaterialManager.h/.cpp`)

Centralized GPU-friendly material database. Replaces the old per-Material structured-buffer approach.

**Architecture:**
```
MaterialManager (singleton via SceneGraph)
  ├── GPU buffer (byte-address, nvrhi::BufferHandle)
  ├── CPU mirror (eastl::vector<uint8_t>)
  ├── Bindless table at space3
  ├── Free-list for recycled offsets
  └── Flush() uploads dirty ranges each frame
```

**Slot system:** Uniform slot size = `max(sizeof(all material data structs))`. Starts at 1024 slots, grows by 512. Free offsets recycled.

**Material dispatch** (`MaterialManager::Get(shaderMaterial)`):
```
Type::kLighting → PBR (typeid) / PBRLandscape (typeid) / Lighting (GetFeature())
Type::kEffect   → EffectMaterial
Type::kWater    → WaterMaterial
else            → MaterialBase (fallback)
```

**Material types** (`src/Core/Material/Skyrim/` — 16 types):
```
LightingMaterial, EnvmapMaterial, GlowmapMaterial, ParallaxMaterial,
FacegenMaterial, FacegenTintMaterial, HairTintMaterial, ParallaxOccMaterial,
EyeMaterial, MultiLayerParallaxMaterial, LandscapeMaterial, LODLandscapeMaterial,
PBRMaterial, PBRLandscapeMaterial, EffectMaterial, WaterMaterial
```

**MaterialType enum** (in shared `interop/`):
```
Lighting=0, Effect=1, Grass=2, Water=3, BloodSplatter=4, DistantTree=5, Particle=6, TruePBR=7
```

**Key interop files:**
- `interop/Material/MaterialBaseData.hlsli` — base HLSL struct
- `interop/Material/Skyrim/*.hlsli` — per-type HLSL data structs
- `interop/Mesh.hlsli` — `MeshData` struct with `MaterialOffsetComp` (compressed byte offset into material buffer)
- `interop/Instance.hlsli` — `InstanceData` with `FirstGeometryID`, `NumGeometry`, `InstanceLightData`

**HLSL material reading** (`shaders/include/SurfaceMaker.hlsli`):
```hlsl
// Typed load from byte-address buffer
Materials[0].Load<ConcreteType>(mesh.GetMaterialOffset())
```
- Material data structs use HLSL inheritance matching C++ inheritance.
- Extra data beyond base size loaded at `offset + kLightingSize`.

### SceneGraph (`src/SceneGraph.h/.cpp`)

Central scene management class. Per-frame traversal + mesh/cluster/material update.

**Owns:**
- `m_DirectMeshes` (BSTriShape → BaseMesh map)
- `m_OwnerClusters` (TESObjectREFR → BLASCluster)
- `m_OrphanClusters` (BSTriShape → BLASCluster)
- `m_MaterialManager`, `m_TextureManager`
- `m_MeshData` / `m_InstanceData` (GPU ring buffers)
- `m_TriangleDescriptors`, `m_VertexDescriptors`, `m_SkinningDescriptors`, `m_DynamicVertexDescriptors` (BindlessTableManager)

**Per-frame flow:**
```
SceneGraph::Update()
  ├── UpdateLights() — collect active lights
  ├── Traversal::ScenegraphTriShapes(worldRoot, callback)
  │     └── ProcessGeometry(refr, bsTriShape)
  │           ├── Filter by type/alpha/skin
  │           ├── Find/create BaseMesh
  │           ├── GetOrCreateCluster(refr) → BLASCluster
  │           ├── AddMember(mesh), mesh->Update()
  │           └── ...
  ├── Hide stale meshes, drop empty clusters
  ├── Flush material manager
  └── Write m_MeshData / m_InstanceData buffers

SceneGraph::BuildClusters(commandList)
  └── for each dirty BLASCluster:
        cluster->BuildUpdate(commandList, this)
```

### Traversal (`src/Utils/Traversal.h`)

Three recursive scene graph traversal functions:
- `ScenegraphFadeNodes(a_object, func)` — visits `BSFadeNode` instances.
- `ScenegraphRTGeometries(a_object, validFadeNode, func)` — visits `BSGeometry`, skips billboards/ordered nodes.
- `ScenegraphTriShapes(a_object, func, parentRefr)` — visits `BSTriShape` with owner propagation, skips hidden objects.

`ScenegraphTriShapes` is the primary traversal used by `SceneGraph::Update()`. It propagates `TESObjectREFR*` owner through `BSFadeNode` boundaries and handles `ShadowSceneNode` portal graphs.

### Culling (`src/Utils/Culling.h/.cpp`)
```cpp
namespace Util::Culling {
    bool ShouldCull(RE::BSGeometry* geometry);
}
```
Simple RTTI-based blacklist: returns `false` (don't cull) for `BSSkyShaderProperty` and `BSParticleShaderProperty`, `true` for everything else. View-frustum culling handled by the game engine's own `BSCullingProcess`.

### Adapter (`src/Utils/Adapter.h/.cpp`)

Platform abstraction layer for Skyrim/Fallout4 dual builds. Every function has `#if defined(SKYRIM)` / `#elif defined(FALLOUT4)` branches.

Key functions: `GetGeometryRuntimeData()`, `GetLightRuntimeData()`, `AsGeometry()`, `AsTriShape()`, `AsNode()`, `GetOwner()`, `GetChildren()`, `IsExteriorCell()`, `IsNiAVObjectHidden()`, `GetVertexData()`, `GetIndexData()`, `GetDynamicResolutionRatios()`.

### Settings (`src/Types/Settings.h`)
```cpp
struct Settings {
    GeneralSettings      GeneralSettings;
    LightingSettings     LightingSettings;
    RaytracingSettings   RaytracingSettings;
    ReblurSettings       ReblurSettings;
    MaterialSettings     MaterialSettings;
    SHaRCSettings        SHaRCSettings;
    AdvancedSettings     AdvancedSettings;
    WaterSettings        WaterSettings;
    ExperimentalSettings ExperimentalSettings;
    ReSTIRGISettings     ReSTIRGI;
    DebugSettings        DebugSettings;
};
```

**Key enums:**
- `Mode`: `None, GlobalIllumination, PathTracing, Debug`
- `Denoiser`: `None, NRD, DLSS_RR, Accumulation`
- `DiffuseBRDF`: `Lambert, Burley, OrenNayar, Gotanda, Chan`
- `HairBSDF`: `None, ChiangBSDF, FarFieldBCSDF`
- `TextureMode`: `Share, Exclusive`

### TLAS Management (`src/Types/TopLevelAS.h`)

`TopLevelAS::Update(commandList, ownerClusters, orphanClusters)`:
1. Clears instance descs
2. For each cluster: `MakeInstanceDesc()` → push back
3. Resize TLAS when count exceeds threshold (step=512, min=2048, threshold=256)
4. `NotifyResized()` → notify `ITLASUpdateListener`s
5. `buildTopLevelAccelStruct()`

Per-light TLAS (`Light::UpdateTLAS(commandList)`) when `AdvancedSettings.PerLightTLAS` is enabled.

### Key Constants (`src/Constants.h`)
| Constant | Value | Purpose |
|---|---|---|
| `MAX_FRAMES_IN_FLIGHT` | 2 | Double buffering |
| `PLAYER_REFR_FORMID` | 0x00000014 | Player reference |
| `LIGHTS_MAX` | 256 | Max active lights |
| `INSTANCE_LIGHTS_MAX` | 32 | Max lights per instance |
| `NUM_MESHES_MIN` | 1024 | Initial mesh buffer capacity |
| `NUM_MESHES_MAX` | 16,384 | Max meshes |
| `NUM_INSTANCES_MAX` | 262,144 | Max instances |
| `NUM_MATERIALS_MIN/STEP/THRESHOLD` | 1024/512/256 | Material buffer sizing |
| `NUM_TEXTURES_MIN/MAX` | 512/8192 | Texture capacity |
| `NUM_CUBEMAPS_MAX` | 256 | Cube map capacity |
| `TLAS_INSTANCES_MIN/STEP` | 2048/512 | TLAS sizing |
| `MAX_BLAS_UPDATES_BEFORE_MAINTENANCE` | 256 | Force rebuild threshold |
| `MAX_BLAS_MAINTENANCE_REBUILDS_PER_FRAME` | 8 | Per-frame rebuild cap |
| `PT_DISPATCH_THREADS` | 8 | PT thread group size |
| `GI_DISPATCH_THREADS` | 16 | GI thread group size |

## Render Graph Architecture

### Execution Model

Passes are organized in a tree structure using `RenderNode`:

```
RootRenderNode  (RenderGraph)
  └── RenderNode[]  (children)
        ├── RenderPass*  (the actual pass)
        └── RenderNode[]  (sub-passes)
```

`RenderPass` (`src/Pass/RenderPass.h`) is abstract: `CreatePipeline()`, `SettingsChanged()`, `ResolutionChanged()`, `Execute()`.

### Mode-Based Pipeline Registration

`Scene` lazily creates render node trees per mode:

**PathTracing mode** (`Scene::GetPathTracing()`):
```
Skinning → LandLODOccluder → SceneTLAS → SHaRC → PathTracing → ReSTIRGI → NRD Reblur → PTComposite → Accumulation
```

**GlobalIllumination mode** (`Scene::GetGlobalIllumination()`):
```
Skinning → LandLODOccluder → SceneTLAS → FaceNormals → SHaRCGI → GlobalIllumination → NRD Reblur → GIComposite
```

**Debug mode** (`Scene::GetDebug()`):
```
Skinning → SceneTLAS → Debug
```

### Per-Pass Pipeline Files (C++)

| Pass | File | RayQuery? | Purpose |
|---|---|---|---|
| `Pass::PathTracing` | `src/Pass/Raytracing/PathTracing.cpp` | Both | Full path tracer (REFERENCE/BUILD/FILL) |
| `Pass::Raytracing::GlobalIllumination` | `src/Pass/Raytracing/GlobalIllumination.cpp` | Both | Hybrid GI - reads GBuffer, traces indirect |
| `Pass::Raytracing::GBuffer` | `src/Pass/Raytracing/GBuffer.cpp` | Both | Raytraced GBuffer (DEAD - never attached to render graph) |
| `Pass::Debug` | `src/Pass/Raytracing/Debug.cpp` | Both | Debug visualization |
| `Pass::Raytracing::ReSTIRGIPass` | `src/Pass/Raytracing/ReSTIRGIPass.cpp` | Compute only | ReSTIR GI resampling (4 sub-passes) |
| `Pass::Skinning` | `src/Pass/Raytracing/Common/Skinning.cpp` | N/A | Two-pass GPU skinning (bone compute + vertex skin) |
| `Pass::SceneTLAS` | `src/Pass/Raytracing/Common/SceneTLAS.cpp` | N/A | Builds TLAS from BLASCluster descs |
| `Pass::LightTLAS` | `src/Pass/Raytracing/Common/LightTLAS.cpp` | N/A | Per-light TLAS for shadow rays |
| `Pass::LandLODOccluder` | `src/Pass/Raytracing/Common/LandLODOccluder.cpp` | N/A | Land LOD vertex data for TLAS |
| `Pass::SHaRC` | `src/Pass/Raytracing/Common/SHaRC.cpp` | Compute only | SH radiance caching (PT mode) |
| `Pass::Raytracing::Common::SHaRCGI` | `src/Pass/Raytracing/Common/SHaRCGI.cpp` | Compute only | SH radiance caching (GI mode) |
| `Pass::Common::Accumulation` | `src/Pass/Raytracing/Common/Accumulation.cpp` | N/A | Temporal accumulation (frame blending) |
| `Pass::Common::PTComposite` | `src/Pass/Raytracing/Common/PTComposite.cpp` | N/A | Assembles NRD outputs for PT |
| `Pass::Common::GIComposite` | `src/Pass/Raytracing/Common/GIComposite.cpp` | N/A | Assembles NRD outputs for GI |
| `Pass::NRD::NRDIntegration` | `src/Pass/NRD/NRDIntegration.cpp` | N/A | NVIDIA Real-time Denoiser |
| `Pass::Utility::FaceNormals` | `src/Pass/Utility/FaceNormals.cpp` | N/A | Face normal texture from depth (GI mode) |
| `Pass::Raster::GBuffer` | `src/Pass/Raster/GBuffer.cpp` | N/A | Rasterized GBuffer (not raytraced) |

## Build System / Shader Compilation

- HLSL shaders compiled with DXC (DirectX Shader Compiler)
- Shader model: DXR 1.0/1.1 ray tracing
- Compile-time defines control features (see below)
- Shader entry points use `[shader("raygeneration")]` or `[numthreads(...)]` for ray query mode
- `USE_RAY_QUERY` toggles between ray tracing pipeline and inline ray queries

### Shader Compilation Pipeline — `src/Utils/Shader.cpp`

```
Settings → Util::Shader::GetRaytracingDefines(settings, sharc, sharcUpdate) [base defines]
  ├── GetPathTracingDefines()       → adds PT-specific (NRD, DLSS_RR, SSS, Hair, StablePlanes, ReSTIRGI)
  └── GetGlobalIlluminationDefines() → adds GI-specific (INSTANCE_MASK=0xFD, RAW_RADIANCE)
              │
              ▼
       GetDXCDefines(defines) [converts wstring → DxcDefine]
              │
              ▼
       DXC compilation (ShaderCache::GetShader / CompileShaderLibrary)
```

**Define hierarchy:**

| Define | RayTracing | PathTracing | GlobalIllumination |
|---|---|---|---|
| `MAX_BOUNCES` | Yes | Yes | Yes |
| `MAX_SAMPLES` | Yes | Yes | Yes |
| `SHARC_UPDATE` | Yes | Yes | Yes |
| `GGX_ENERGY_CONSERVATION` | Conditional | Conditional | Conditional |
| `USE_LIGHT_TLAS` | Conditional | Conditional | Conditional |
| `RIS` / `RIS_MAX_CANDIDATES` | Conditional | Conditional | Conditional |
| `DIFFUSE_MODE` | Yes | Yes | Yes |
| `SHARC` | Conditional | Conditional | Conditional |
| `THREAD_GROUP_SIZE` | — | 8 | 16 |
| `HAS_PREV_POSITIONS` | — | 1 | — |
| `HAIR_MODE` | — | Yes | — |
| `SUBSURFACE_SCATTERING` | — | Conditional | — |
| `NRD` / `DLSS_RR` | — | Conditional | Conditional |
| `RAW_RADIANCE` | — | — | Yes (with NRD) |
| `STABLE_PLANES` | — | Conditional | — |
| `RESTIR_GI` | — | Conditional | — |
| `INSTANCE_MASK` | — | — | `All & ~Water = 0xFD` |

### Compile-Time Defines

| Define | Effect |
|---|---|
| `SKYRIM` / `FALLOUT4` | Game-specific material system |
| `USE_RAY_QUERY` | Inline ray query vs DXR pipeline |
| `RAW_RADIANCE` | Demodulated diffuse output (denoiser-friendly, splits diffuse/specular) |
| `NRD` / `NRD_REBLUR` | NVIDIA denoiser integration |
| `DLSS_RR` | DLSS Ray Reconstruction (specular albedo + hit distance) |
| `SHARC` / `SHARC_UPDATE` | SH-based radiance caching |
| `LIGHTING_MODE` | `LIGHTING_MODE_DIFFUSE` for diffuse-only (cosine hemisphere sampling, skips BSDF) |
| `USE_SIA_INTERPOLATION` | NVIDIA SIA offset instead of standard |
| `SUBSURFACE_SCATTERING` | Enables SSS evaluation |
| `RESTIR_GI` | ReSTIR GI secondary surface capture |
| `EFFECT_PASSTHROUGH` | Pass through effect/particle materials in bounce loop |
| `STABLE_PLANES` | Stable planes denoising (BUILD/FILL/REFERENCE modes) |
| `GLINT` | Enables glint BSDF code |
| `HAS_PREV_POSITIONS` | Skinned/dynamic mesh motion vectors |
| `DISABLE_NORMAL_REJECTION` | Skips minimum cos-theta checks in BSDF |
| `DISABLE_SPECULAR_NORMAL_REJECTION` | Skips min cos-theta for specular |
| `DEBUG_WHITE_FURNACE` | Sets albedo to white (furnace test) |
| `DEBUG_TRACE_HEATMAP` | Trace time heatmap profiling |
| `INSTANCE_MASK` | DXR instance inclusion mask (0xFF default, GI uses 0xFD) |
| `THREAD_GROUP_SIZE` | 8 for PT, 16 for GI |
| `DIFFUSE_MODE` | Generalized diffuse lighting mode |
| `USE_LIGHT_TLAS` | Per-light TLAS for shadow optimization |
| `RIS` / `RIS_MAX_CANDIDATES` | Reservoir-based Importance Sampling |
| `HAIR_MODE` | Enables hair BSDF evaluation |
| `GROUP_TILING` | Thread group tiling for oversized dispatches |

## Shader Files

### Ray Generation Shaders
- `shaders/raytracing/Pathtracing/RayGeneration.hlsl` — Full path tracer (reference + stable planes modes)
- `shaders/raytracing/GlobalIllumination/RayGeneration.hlsl` — Hybrid GI pass (reads GBuffer, traces indirect only)
- `shaders/raytracing/GBuffer/RayGeneration.hlsl` — Raytraced GBuffer (not currently attached)
- `shaders/raytracing/Debug/RayGeneration.hlsl` — Debug visualization

### Shared Infrastructure (`shaders/`)
- `include/Common.hlsli` — Camera data, common constants
- `include/Surface.hlsli` — `Surface` struct (all PBR material properties), `BRDFContext`
- `include/SurfaceMaker.hlsli` — Three overloads to create `Surface`:
  1. From ray payload + geometry (used by PT shader)
  2. From raster data (texcoord, normals, mesh)
  3. From explicit GBuffer params (used by GI shader)
- `include/PBR.hlsli` — `PBR::F0()` overloads, `PBR::Roughness()`, defaults
- `include/Lighting.hlsli` — `EvaluateDirectRadiance()`, `EvalDirectionalLight()`, `EvalPointLight()`, `EvalDeltaLobeLighting()`
- `include/NRD.hlsli` — NVIDIA Real-time Denoiser integration (REBLUR packing)
- `include/SurfaceSkyrim.hlsli` — Skyrim material functions (DefaultMaterial, LandMaterial, WaterMaterial, EffectMaterial, etc.)
- `include/SurfaceFallout4.hlsli` — Fallout 4 material functions
- `include/Common/BRDF.hlsli` — BRDF evaluation helpers (diffuse models, GGX)
- `include/AdvancedSettings.hlsli` — Compile-time toggles for diffuse mode, light eval mode, SIA interpolation

### Ray Tracing Infrastructure (`shaders/raytracing/`)
- `include/Common.hlsli` — RT constants (RayTracing CB, RAY_TMAX, etc.)
- `include/Payload.hlsli` — `Payload` struct, `TraceRayStandard()`
- `include/ShadowPayload.hlsli` — `ShadowPayload` struct (separated from Payload)
- `include/Geometry.hlsli` — Mesh/vertex/instance data (uses `MeshData` with `MaterialOffsetComp`, `Type`, `DynamicID`)
- `include/RayOffset.hlsli` — Three offset methods: `OffsetRay()`, `OffsetRayAlt()`, `OffsetRaySIA()`
- `include/SIA.hlsli` — NVIDIA Self-Intersection Avoidance functions (separate file)
- `include/MonteCarlo.hlsli` — Random seeds, GGX sampling, env BRDF
- `include/Transparency.hlsli` — Transparent/effect material handling
- `include/SubsurfaceLighting.hlsli` — Subsurface scattering NEE
- `include/StablePlanes.hlsli` — Stable planes implementation (506 lines)
- `include/PathTracerStablePlanes.hlsli` — BUILD/FILL pass logic (623 lines)

### BSDF System (`shaders/raytracing/include/Materials/`)
- `BSDF.hlsli` — `StandardBSDF`, `DefaultBSDF`, sub-lobes
- `LobeType.hlsli` — Lobe flags (Diffuse, Specular, Delta, Transmission, Coat, Fuzz)
- `Fresnel.hlsli` — `evalFresnelSchlick()`, `evalFresnelDielectric()`, `F0toIOR()`
- `Microfacet.hlsli` — GGX NDF, VNDF sampling, Smith G
- `Glint.hlsli` — Discrete stochastic microfacet glint
- `TexLODHelpers.hlsli` — Ray cone LOD computation
- `Transmission.hlsli` — Transmission BSDF evaluation
- `SubsurfaceMaterial.hlsli` — Subsurface scattering material evaluation
- `SubsurfaceScattering.hlsli` — Subsurface scattering NEE evaluation
- `HairMaterial.hlsli` — Hair material evaluation (from NVIDIA RTXCR)
- `HairBsdfHelper.hlsli` — Hair BSDF helper utilities

### SHaRC (`shaders/raytracing/include/SHaRC/`)
- `Sharc.hlsli`, `SHaRCHelper.hlsli`, `HashGridCommon.h`, `SharcCommon.h`

### ReSTIR GI (`shaders/raytracing/RTXDI/`)
- `GIFinalShading.hlsli`, `GIFusedResampling.hlsli`, `GISpatialResampling.hlsli`, `GITemporalResampling.hlsli`

## DXR Pipeline Architecture

### Dual Path: Ray Query vs Ray Tracing Pipeline

| Property | Ray Query (`USE_RAY_QUERY=1`) | Ray Tracing Pipeline (`USE_RAY_QUERY=0`) |
|---|---|---|
| Shader model | `cs_6_5` (compute) | `lib_6_5` (shader library) |
| Entry point | `[numthreads(...)] void Main(uint2 idx)` | `[shader("raygeneration")] void Main()` |
| Ray dispatch | `TraceRayInline()` + `RayQuery<>.Proceed()` | `TraceRay()` → separate closest-hit/any-hit/miss |
| Pipeline | `nvrhi::ComputePipeline` | `nvrhi::rt::PipelineHandle` |
| Binding visibility | `nvrhi::ShaderType::Compute` | `nvrhi::ShaderType::AllRayTracing` |
| Index source | `Camera.RenderSize` | `DispatchRaysIndex()`, `DispatchRaysDimensions()` |
| Dispatch call | `commandList->dispatch(threadGroups)` | `commandList->dispatchRays(args)` |
| Thread group size | `THREAD_GROUP_SIZE x THREAD_GROUP_SIZE` | 1 thread per pixel (DXR-managed) |

### Pipeline Creation Pattern

1. **`CreatePipeline()`** — branches to `CreateComputePipeline()` or `CreateRayTracingPipeline()` based on `UseRayQuery`
2. **Shader library compilation** — `ShaderUtils::CompileShaderLibrary(device, path, defines)`
3. **Pipeline description** — populates `nvrhi::rt::PipelineDesc` with shaders, hit groups, global binding layouts, maxPayloadSize, maxRecursionDepth
4. **Shader table** — created from pipeline, populated with `setRayGenerationShader()`, `addMissShader()`, `addHitGroup()`

### Shader Table Index Semantics
```hlsl
#define DIFFUSE_RAY_HITGROUP_IDX 0
#define DIFFUSE_RAY_MISS_IDX 0
#define SHADOW_RAY_HITGROUP_IDX 1
#define SHADOW_RAY_MISS_IDX 1
```

### DXR Shader File Structure

**`shaders/raytracing/Common/`** — shared by all raytracing passes:

| File | Shader Type | Payload | Purpose |
|---|---|---|---|
| `ClosestHit.hlsl` | `[shader("closesthit")]` | `Payload` | Packs instance/geometry/primitive indices, barycentrics, hit distance |
| `AnyHit.hlsl` | `[shader("anyhit")]` | `Payload` | Tests alpha transparency via `ConsiderTransparentMaterial()` |
| `Miss.hlsl` | `[shader("miss")]` | `Payload` | No-op |
| `ShadowAnyHit.hlsl` | `[shader("anyhit")]` | `ShadowPayload` | Tests shadow transparency |
| `ShadowMiss.hlsl` | `[shader("miss")]` | `ShadowPayload` | Sets `payload.missed = 1.0f` |

### DXR Payload Types

Both exactly **20 bytes** (5 uint32):
```hlsl
struct Payload {
    float hitDistance;     // offset 0
    uint primitiveIndex;   // offset 4
    uint barycentricsPacked;           // offset 8
    uint instanceGeometryIndexPacked;  // offset 12
    uint randomSeed;       // offset 16
};

struct ShadowPayload {
    float missed;          // offset 0  (1.0 = missed, 0.0 = hit)
    float3 transmission;   // offset 4  (accumulated Beer-Lambert)
    uint randomSeed;       // offset 16
};
```

### Per-Pass Register/Space Layout (PathTracing)

| Space | Register | Content | Pipeline Layout |
|---|---|---|---|
| space0 | b0-b3, t0-t8, u0-u17, s0-s2 | Global: Camera, Raytracing, Features CBs; TLAS; all UAVs; samplers; sky/flow map SRVs; light/instance/mesh buffers | `m_BindingLayout` |
| space1 | t0 | `Triangles[]` (`StructuredBuffer<Triangle>`) | `TriangleDescriptors` |
| space2 | t0 | `Vertices[]` (`StructuredBuffer<Vertex>`) | `VertexDescriptors` |
| space3 | t0 | `Materials[]` (byte-address buffer, typed loads) | `MaterialDescriptors` |
| space4 | t0 | `Textures[]` (descriptor array) | `TextureDescriptors` |
| space5 | t0 | `PrevPositions[]` or `DynamicPositions[]` or `LightTLAS` | `PrevPositionDescriptors` |
| space6/7 | t0 | `CubeTextures[]` (environment maps) | `CubemapDescriptors` |

Note: `GBuffer` pipeline omits `MaterialDescriptors` (space3). `GlobalIllumination` omits `PrevPositionDescriptors`.

### Geometry.hlsli — Data Access Chain

```
GetInstance(instanceIdx) → Instances[instanceIdx] (InstanceData)
GetMesh(instanceIdx, geomIdx) → Meshes[instance.FirstGeometryID + geomIdx] (MeshData)
GetTriangle(meshIdx, primIdx) → Triangles[meshIdx][primIdx]
GetVertices(meshIdx, primIdx, ...) → v0, v1, v2 (from Vertices[] or DynamicPositions[])
GetMaterial(meshIdx) → Materials[0].Load<ConcreteType>(mesh.GetMaterialOffset())
```

`MeshData` includes: `VertexID`, `IndexID`, `DynamicID`, `MaterialOffsetComp`, `Type`, `VertexDesc`, `Properties`.
Dynamic mesh positions read from `DynamicPositions[mesh.DynamicID]` (float4 stride, RGB32_FLOAT).

### Ray Dispatch Flow (`shaders/raytracing/include/Rays.hlsli`)

Five functions, all branched on `USE_RAY_QUERY`:

| Function | Payload | Flags | Purpose |
|---|---|---|---|
| `TraceRayOpaque` | `Payload` | `FORCE_OPAQUE` | Always skips any-hit |
| `TraceRayStandard` | `Payload` | `SKIP_PROCEDURAL_PRIMITIVES` | Primary and bounce rays |
| `TraceRayShadow` | `ShadowPayload` | `SKIP_PROCEDURAL \| SKIP_CLOSEST_HIT` | NEE shadow rays |
| `TraceRayShadowFinite` | `ShadowPayload` | `SKIP_PROCEDURAL \| SKIP_CLOSEST_HIT` | Shadow with finite tmax |
| `SampleSubsurface` | `Payload` | `CULL_BACK_FACING \| SKIP_PROCEDURAL` | SSS probe ray |

### Ray Query Candidate Loop Pattern

```
payload.Init(randomSeed)
RayQuery<FLAGS> rayQuery; rayQuery.TraceRayInline(...)
while (rayQuery.Proceed())
    ConsiderTransparentMaterial*() → CommitNonOpaqueTriangleHit() or skip
Check CommittedStatus() → extract hit or mark miss
```

## Surface Struct

All PBR properties: `Primary`, `Position`, `PrevPosition`, `GeomNormal`, `GeomTangent`, `Normal`, `Tangent`, `Bitangent`, `FaceNormal`, `Albedo`, `Alpha` (DEAD), `DiffuseAlbedo`, `Roughness`, `Metallic`, `Emissive`, `AO`, `F0`, `IOR`, `TransmissionColor`, `VolumeAbsorption`, `SubsurfaceData`, `DiffTrans`, `SpecTrans`, `IsThinSurface`, `CoatColor`, `CoatStrength`, `CoatRoughness`, `CoatF0`, `CoatNormal`, `CoatTangent`, `CoatBitangent`, `FuzzColor`, `FuzzWeight`, `GlintScreenSpaceScale`, `GlintLogMicrofacetDensity`, `GlintMicrofacetRoughness`, `GlintDensityRandomization`, `GlintTexCoord`, `MipLevel`, `PositionError`, `SIAOffset` (conditional).

### PBR Key Values
- `PBR::Defaults::F0 = (0.04, 0.04, 0.04)`
- `PBR::Defaults::Roughness = 1.0`
- `PBR::Defaults::Metallic = 0.0`
- `kMinGGXAlpha = 0.0064` — roughness below this treated as delta
- `kMinCosTheta = 1e-6` — minimum cosine for numerical stability

### Methods
```hlsl
float3 Mul(float3 tangentSample)      // TangentToWorld
float3 ToLocal(float3 v)              // dot(v, T/B/N)
float3 FromLocal(float3 v)            // Same as Mul
float3 CoatToLocal/CoatFromLocal      // Coat-frame variants
void   FlipNormal()                   // Negates Normal, GeomNormal, FaceNormal
```

## Important: GI vs PT Shader Differences

1. **Surface creation**: GI reads from GBuffer textures; PT traces primary ray.
2. **Direct lighting on primary**: PT evaluates NEE on primary surface; GI only indirect.
3. **First-bounce offset**: GI uses `OffsetRayAlt()`; PT uses `OffsetRaySIA()`/`OffsetRay()`.
4. **Output gamma**: PT applies `LLTrueLinearToGamma()`; GI does not.
5. **isPrimary scaling**: `EvaluateDirectRadiance()` scales lights by `Raytracing.Directional`/`Raytracing.Point` when `isPrimary=false`. GI always passes `false`; PT passes `true` for primary surface.
6. **F0 metallic mismatch** (potential bug): In explicit SurfaceMaker overload, `surface.F0` uses RAW metallic while `DiffuseAlbedo` uses REMAPPED metallic. If `Raytracing.Metalness` has non-identity range, F0 and DiffuseAlbedo are computed with different values.

## Path Tracer Bounce Loop

### Execution Flow

```
1. SetupPrimaryRay() → TraceRayStandard() → primary hit or miss
2. If miss: sky/miss output
3. Effect passthrough loop (up to 16 iterations, if EFFECT_PASSTHROUGH)
4. SurfaceMaker::make() → primary Surface
5. BRDFContext::make() + StandardBSDF::make()
6. AdjustShadingNormal()
7. GBuffer write (NormalRoughness, DiffuseAlbedo, MotionVectors, Depth, ViewDepth, SpecularAlbedo)
8. Direct lighting on primary surface (NEE + delta lobe)
9. for (i < MAX_SAMPLES)
      for (j < MAX_BOUNCES)
          BSDF::SampleBSDF() → direction, weight, lobe flags
          throughput *= brdfWeight
          Russian roulette (if !SHARC_UPDATE)
          OffsetRay → new origin
          TraceRayStandard() → next hit
          Water volume absorption
          Effect passthrough sub-loop
          ReSTIR GI surface capture (FILL)
          SHaRC cache lookup → early termination on cache hit
          SurfaceMaker::make() → new surface
          BRDFContext + StandardBSDF + AdjustShadingNormal
          Direct lighting on bounce surface (NEE + delta lobe)
10. Accumulate, divide by MAX_SAMPLES
11. Output (NRD-packed or direct + gamma)
```

### Three Modes

| Mode | Define | Behavior |
|---|---|---|
| REFERENCE | Default | Standard MC path tracing |
| BUILD_STABLE_PLANES | Stable planes BUILD | Deterministic delta-path exploration |
| FILL_STABLE_PLANES | Stable planes FILL | Replay + narrow-window re-trace |

### Water Volume Tracking

Tracked via `insideWaterVolume` + `waterVolumeAbsorption`. On transmission with non-zero `VolumeAbsorption`, entering/leaving volume. Beer-Lambert applied per segment: `throughput *= exp(-absorption * hitDistance)`.

### Russian Roulette

```hlsl
float rrVal = sqrt(Color::RGBToLuminance(throughputColor));
float rrProb = saturate(0.85 - rrVal); rrProb *= rrProb;
rrProb = saturate(rrProb + max(0, float(j) / MAX_BOUNCES - 0.4));
if (Random(randomSeed) < rrProb) break;
throughput /= (1.0 - rrProb);
```

### Lobe Checks

- `isLobe(Delta)` — mirror/glass; no ray cone expansion, special offset
- `isLobe(Specular) || isDelta` — specular sample (affects NRD hit distance)
- `isLobe(Transmission)` — medium enter/exit (water volume)
- `surface.Primary && isDelta` — `isPrimaryReplacement` flag

### RayCone Propagation
```
Primary: RayCone(PixelConeSpreadAngle * hitDistance, PixelConeSpreadAngle)
Scatter: spreadAngle += expansion from BSDF scatter PDF (capped at 2*PI)
Trace:   rayCone = rayCone.propagateDistance(hitDistance)
```

### Output Modes

**NRD**: `Output` = direct lighting; `DiffuseRadiance`/`SpecularRadiance` = REBLUR-packed radiance+hit distance; `DiffuseFactor`/`SpecularFactor` = demodulation factors.

**DLSS-RR**: `Output` = gamma-corrected direct+radiance; `SpecularAlbedo` = integrated specular BRDF albedo; `SpecularHitDistance` = min specular hit distance.

**Direct**: `Output = float4(LLTrueLinearToGamma(direct+radiance), 1.0f)`.

## Transparency.hlsli

### ConsiderTransparentMaterial (primary/bounce rays)
Returns `true` (commit hit) or `false` (pass through). Evaluates alpha test/additive/blend, stochastic discard. Water always transparent.

### ConsiderTransparentMaterialShadow (shadow rays)
Returns `false` when hit's attenuation was factored into `transmitanceInOut`. Water: Beer-Lambert + Fresnel. Effect: passthrough. Transmission/refraction: RGB attenuation. Emissive/glow: Fresnel attenuation.

## SurfaceMaker::make() Overload 1 — Raytracing Path

1. Sets `surface.Primary`, defaults all properties
2. `GetMesh(payload)` → triple geometry lookup chain
3. `GetVertices()` → v0/v1/v2 with optional prev-positions
4. Computes barycentrics, interpolates texcoord
5. `objectToWorld3x3 = mul(instance.Transform, mesh.Transform)`
6. **SIA**: `SIA_SafeSpawnPointSimple()` for precise position/face normal/offset
7. **Non-SIA**: estimates `PositionError` from vertex magnitudes
8. Previous world position for motion vectors
9. Ray-cone triangle LOD
10. Object-space face normal, flips vertex normals if opposing
11. Interpolates handedness, normalWS, tangentWS → bitangentWS via cross
12. `surface.MipLevel` from ray cone + base texture dimensions + LOD bias
13. Sets `GeomNormal`/`GeomTangent`, defaults `Normal`/`Tangent`/`Bitangent`
14. Dispatches to material function: LandMaterial, EffectMaterial, WaterMaterial, DistantTreeMaterial, GrassMaterial, or DefaultMaterial
15. Roughness/metallic remapping: `PBR::Roughness()`, `Remap(metallic)`
16. `DiffuseAlbedo = Albedo * (1 - Metallic)`
17. `F0 = PBR::F0(surface.F0, Albedo, Metallic)` (3-arg)
18. `IOR = F0toIOR(F0)`

### SurfaceMaker Overload 3 — Explicit GBuffer Params

Used by GI shader. Simplified: `PositionError` from magnitude, `MipLevel = Raytracing.TexLODBias`, emissive scaled at construction, no glint/fuzz, `PBR::F0(albedo, metallic)` (2-arg).

## LobeType Enum

```hlsl
enum LobeType : uint {
    DiffuseReflection    = 1 << 0,
    DiffuseTransmission  = 1 << 1,
    SpecularReflection   = 1 << 2,
    SpecularTransmission = 1 << 3,
    DeltaReflection      = 1 << 4,
    DeltaTransmission    = 1 << 5,
    CoatReflection       = 1 << 6,
    FuzzReflection       = 1 << 7,
    Diffuse   = DiffuseReflection | DiffuseTransmission,
    Specular  = SpecularReflection | SpecularTransmission,
    Delta     = DeltaReflection | DeltaTransmission,
    Transmission = DiffuseTransmission | SpecularTransmission | DeltaTransmission,
    NonDelta  = ~Delta,
};
```

## BSDFSample Struct

| Field | Type | Meaning |
|---|---|---|
| `wo` | `float3` | Sampled outgoing direction (world space) |
| `weight` | `float3` | BSDF contribution weight |
| `pdf` | `float` | Probability density |
| `lobe` | `uint` | Bitmask of lobe type flags |

## RayOffset Strategies

| Function | Strategy | Used By |
|---|---|---|
| `OffsetRay()` | Position-error based (RT Gems §2.3) | PT bounces (when !USE_SIA), shadow rays |
| `OffsetRayAlt()` | Per-component integer trick | GI first bounce |
| `OffsetRaySIA()` | NVIDIA SIA (precomputed `siaOffset`) | PT bounces (when USE_SIA), shadow rays |

## Depth/Frustum Utils

- **`getLinearDepth()`** — reconstructs linear depth from clip-space depth using camera projection parameters.
- **`IsInside(viewDepth, instanceCenter, instanceRadius)`** — conservative frustum culling test: transforms instance bounding sphere center into clip space, then checks whether the view-space Z-range (±radius) overlaps current pixel's depth. Used inside the PathTracing ray-gen to skip NEE for lights whose bounding sphere does not overlap the pixel.

## Shader Utility Functions

- **`ShaderUtils::CompileShaderLibrary(device, path, defines)`** — compiles `.hlsl` to DXIL library via DXC
- **`ShaderCache::GetShader(path, defines, target)`** — cached shader compilation for compute/rayquery paths
- **`Util::Shader::GetDXCDefines(shaderDefines)`** — converts `eastl::vector<ShaderDefine>` to `eastl::vector<DxcDefine>`
- **`Util::Shader::GetPathTracingDefines(settings, hasSharc, isUpdate)`** — builds define list for PT pass
- **`Util::Shader::GetGlobalIlluminationDefines()`** — builds define list for GI pass

## Render Target Manager — `src/Renderer/RenderTargetManager.h`

Manages render target textures. Common targets:
- `Main` — color output
- `ClipDepth` — clip-space depth
- `MotionVectors3D` — motion vectors (RGBA16)
- `DiffuseAlbedo`, `ViewDepth`, `DiffuseRadiance`, `SpecularRadiance` — NRD denoiser inputs
- `RRSpecularAlbedo`, `RRSpecularHitDist` — DLSS Ray Reconstruction inputs

## Supported Features — `src/Types/SupportedFeatures.h`

```cpp
enum SupportedFeatures : uint32_t {
    Raytracing       = 1 << 0,  // DXR 1.0
    InlineRaytracing = 1 << 1,  // DXR 1.1 RayQuery
    OpacityMicroMaps = 1 << 2,  // NVIDIA OMM
    LinearSweptSpheres = 1 << 3,// NVIDIA LSS
    ShaderExecutionReordering = 1 << 4, // NVIDIA SER
};
```

Feature detection at `Renderer::Initialize()`. `ReSTIRGIPass`, `SHaRC`, `SHaRCGI` unconditionally use ray query.

## Critical Warning: Two Different ShaderType Enums

Material `ShaderType` (`interop/Material/MaterialBaseData.hlsli`):

| Value | Name |
|---|---|
| 0 | TruePBR |
| 1 | Lighting |
| 2 | Effect |
| 3 | Grass |
| 4 | Water |
| 5 | BloodSplatter |
| 6 | DistantTree |
| 7 | Particle |

Game engine `BSShader::Types::Type`:

| Value | Name |
|---|---|
| 0 | None |
| 1 | Grass |
| 2 | Sky |
| 3 | Water |
| 4 | BloodSplatter |

These are different enums with different numeric values. Water is 4 in the material enum but 3 in the engine enum. Always verify which enum a value belongs to before writing comparisons.

## DrawWorld_BuildSceneLists Hook

- Address: `0x1405B7C80`, REL::ID 35630
- Return type: `void*` (TLS/SEH plumbing value, callers ignore it)
- Gated by `Scene::GetSingleton()->ApplyPathTracingCull()`
- Uses `stl::detour_thunk` (Microsoft Detours)
- Skip CPU culling during path tracing for performance
