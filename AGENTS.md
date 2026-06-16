# creation-engine-raytracing

## Overview

Ray-traced rendering extensions for Creation Engine (Skyrim, Fallout 4). Adds path tracing and global illumination via DXR ray tracing shaders. Windows platform, HLSL shaders.

## Build System / Shader Compilation

- HLSL shaders compiled with DXC (DirectX Shader Compiler)
- Shader model: DXR 1.0/1.1 ray tracing
- Compile-time defines control features (see below)
- Shader entry points use `[shader("raygeneration")]` or `[numthreads(...)]` for ray query mode
- `USE_RAY_QUERY` toggles between ray tracing pipeline and inline ray queries
- `THREAD_GROUP_SIZE` defaults to 32

## Key Shader Files

### Ray Generation Shaders
- `shaders/raytracing/Pathtracing/RayGeneration.hlsl` — Full path tracer (reference + stable planes modes)
- `shaders/raytracing/GlobalIllumination/RayGeneration.hlsl` — Hybrid GI pass (reads GBuffer, traces indirect only)

### Shared Infrastructure (`shaders/`)
- `include/Common.hlsli` — Camera data, common constants
- `include/Surface.hlsli` — `Surface` struct (all PBR material properties), `BRDFContext`
- `include/SurfaceMaker.hlsli` — Three overloads to create `Surface`:
  1. From ray payload + geometry (used by PT shader)
  2. From raster data (texcoord, normals, mesh)
  3. From explicit GBuffer params: `make(position, faceNormal, normal, tangent, bitangent, albedo, roughness, metallic, emissive, ao)` (used by GI shader)
- `include/PBR.hlsli` — `PBR::F0(albedo,metalness)` and `PBR::F0(specLevel,albedo,metalness)` overloads, `PBR::Roughness()`, defaults
- `include/Lighting.hlsli` — `EvaluateDirectRadiance()`, `EvalDirectionalLight()`, `EvalPointLight()`, `EvalDeltaLobeLighting()`
- `include/NRD.hlsli` — NVIDIA Real-time Denoiser integration (REBLUR packing)
- `include/SurfaceSkyrim.hlsli` — Skyrim material functions (DefaultMaterial, LandMaterial, WaterMaterial, EffectMaterial, etc.)
- `include/SurfaceFallout4.hlsli` — Fallout 4 material functions
- `include/Common/BRDF.hlsli` — BRDF evaluation helpers (diffuse models, GGX)

### Ray Tracing Infrastructure (`shaders/raytracing/`)
- `include/Common.hlsli` — RT constants (RayTracing CB, RAY_TMAX, etc.)
- `include/Payload.hlsli` — `Payload` struct, `TraceRayStandard()`
- `include/Geometry.hlsli` — Mesh/vertex/instance data
- `include/RayOffset.hlsli` — Three offset methods:
  - `OffsetRay()` — Position-error based (RT Gems)
  - `OffsetRayAlt()` — Per-component integer trick (GI shader first bounce)
  - `OffsetRaySIA()` — NVIDIA Self-Intersection Avoidance
- `include/MonteCarlo.hlsli` — Random seeds, GGX sampling, env BRDF
- `include/Transparency.hlsli` — Transparent/effect material handling
- `include/SubsurfaceLighting.hlsli` — Subsurface scattering NEE

### BSDF System (`shaders/raytracing/include/Materials/`)
- `BSDF.hlsli` — `StandardBSDF` (top-level), `DefaultBSDF` (main BSDF), sub-lobes:
  - `DiffuseReflection`, `DiffuseTransmissionLambert`
  - `SpecularReflectionMicrofacet`, `SpecularReflectionTransmissionMicrofacet`
  - Coat reflection, fuzz reflection
  - Hair BSDFs: `HairChiangBSDF.hlsli`, `HairFarFieldBCSDF.hlsli`
- `LobeType.hlsli` — Lobe flags: Diffuse, Specular, Delta, Transmission, Coat, Fuzz; masks `NonDelta`, `Delta`
- `Fresnel.hlsli` — `evalFresnelSchlick()`, `evalFresnelDielectric()`, `F0toIOR()`
- `Microfacet.hlsli` — GGX NDF, VNDF sampling, Smith G
- `Glint.hlsli` — Discrete stochastic microfacet glint
- `TexLODHelpers.hlsli` — Ray cone LOD computation

## Compile-Time Defines

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
| `INSTANCE_MASK` | DXR instance inclusion mask (default 0xFF, GI uses NonWater=1) |
| `THREAD_GROUP_SIZE` | Default 32 |

## Surface Struct Fields

All PBR properties on `Surface`: `Primary`, `Position`, `PrevPosition`, `GeomNormal`, `GeomTangent`, `Normal`, `Tangent`, `Bitangent`, `FaceNormal`, `Albedo`, `Alpha` (DEAD — unused, never initialized), `DiffuseAlbedo`, `Roughness`, `Metallic`, `Emissive`, `AO`, `F0`, `IOR`, `TransmissionColor`, `VolumeAbsorption`, `SubsurfaceData`, `DiffTrans`, `SpecTrans`, `IsThinSurface`, `CoatColor`, `CoatStrength`, `CoatRoughness`, `CoatF0`, `CoatNormal`, `CoatTangent`, `CoatBitangent`, `FuzzColor`, `FuzzWeight`, `GlintScreenSpaceScale`, `GlintLogMicrofacetDensity`, `GlintMicrofacetRoughness`, `GlintDensityRandomization`, `GlintTexCoord`, `MipLevel`, `PositionError`, `SIAOffset` (conditional).

## PBR Key Values

- `PBR::Defaults::F0 = (0.04, 0.04, 0.04)` (dielectric specular)
- `PBR::Defaults::Roughness = 1.0`
- `PBR::Defaults::Metallic = 0.0`
- `kMinGGXAlpha = 0.0064` — roughness below this treated as delta (mirror)
- `kMinCosTheta = 1e-6` — minimum cosine for numerical stability

## PBR::F0 Overloads

```hlsl
// 2-arg — used by explicit-param SurfaceMaker (GI shader)
float3 F0(float3 albedo, float metalness)
// → lerp(0.04, albedo, metalness)

// 3-arg — used by payload-based SurfaceMaker (PT shader)
float3 F0(float3 specularLevel, float3 albedo, float metalness)
// → lerp(specularLevel, albedo, metalness)
```

## Important: GI vs PT Shader Differences

1. **Surface creation**: GI reads from GBuffer textures (Albedo, NormalRoughness, MAO, FaceNormals) and creates Surface via explicit-param make. PT traces primary ray and creates Surface from geometry/material.
2. **Direct lighting on primary**: PT evaluates NEE on the primary surface before the bounce loop; GI only does indirect (bounces only). By design.
3. **First-bounce offset**: GI uses `OffsetRayAlt()` for j==0; PT uses `OffsetRaySIA()`/`OffsetRay()`.
4. **Output gamma**: PT applies `LLTrueLinearToGamma()` to output; GI does not.
5. **isPrimary scaling**: `EvaluateDirectRadiance()` scales directional lights by `Raytracing.Directional` and point lights by `Raytracing.Point` when `isPrimary=false`. GI always passes `false`; PT passes `true` for primary surface.
6. **F0 metallic mismatch** (potential bug): In the explicit SurfaceMaker overload, `surface.F0 = PBR::F0(albedo, metallic)` uses RAW metallic, while `surface.DiffuseAlbedo = albedo * (1 - surface.Metallic)` uses REMAPPED metallic. If `Raytracing.Metalness` has non-identity range, F0 and DiffuseAlbedo are computed with different metallic values.

## Instance Mask System

### Enum (C++)
`src/Types/InstanceMask.h` — DXR instance visibility mask (8-bit):
```cpp
enum InstanceMask : uint8_t
{
    None     = 0,
    NonWater = 1 << 0,
    All      = 0xFF
};
```

### Instance mask assignment — `src/Core/Instance.h:64-82`
`GetInstanceDesc()` sets the mask per instance. Non-water geometry gets `NonWater (1)`, water geometry gets `None (0)` by clearing the `NonWater` bit. The result is written into the TLAS via `nvrhi::rt::InstanceDesc::setInstanceMask()`.

### Shader-side filtering — `shaders/raytracing/include/Rays.hlsli:17-19`
```hlsl
#ifndef INSTANCE_MASK
#   define INSTANCE_MASK (0xFF)
#endif
```
The `INSTANCE_MASK` define is the ray's `InstanceInclusionMask`, sent to `TraceRay()` / `TraceRayInline()`. DXR intersects only when `(instance.InstanceMask & ray.InstanceInclusionMask) != 0`.

### Per-pass defines — `src/Utils/Shader.cpp`
- **`GetRaytracingDefines()`** — Does NOT set `INSTANCE_MASK`; default `0xFF` applies (all geometry).
- **`GetPathTracingDefines()`** — Does NOT set `INSTANCE_MASK`; default `0xFF` applies.
- **`GetGlobalIlluminationDefines()`** — Sets `INSTANCE_MASK = NonWater (1)` so water instances (mask `0`) are excluded from GI.

### Water filtering logic
1. `Model::UpdateMeshFlags()` (`src/Core/Model.cpp:52`) accumulates `mesh->material->shaderType` via `|=` into `model->shaderTypes`.
2. `GetInstanceDesc()` checks `model->GetShaderTypes() & RE::BSShader::Type::Water` to decide whether to clear the `NonWater` bit.
3. GI shader is compiled with `INSTANCE_MASK=1`, so `0 & 1 = 0` → water excluded from GI rays.

### Critical warning: Two different ShaderType enums
Material `ShaderType` (`src/Core/Skyrim/Material.h:37-47` or `Fallout4/Material.h`):

| Value | Name         |
|-------|-------------|
| 0     | TruePBR     |
| 1     | Lighting    |
| 2     | Effect      |
| 3     | Grass       |
| **4** | **Water**   |
| 5     | BloodSplatter |
| 6     | DistantTree |
| 7     | Particle    |

Game engine `BSShader::Types::Type` (`extern/CommonLibSSE-NG/include/RE/B/BSShader.h:131-145`):

| Value | Name         |
|-------|-------------|
| 0     | None        |
| 1     | Grass       |
| 2     | Sky         |
| **3** | **Water**   |
| 4     | BloodSplatter |
| ...   | ...         |

**These are different enums with different numeric values.** Water is `4` in the material enum but `3` in the engine enum. If a `Model` expresses its `shaderTypes` in material-enum values but `GetInstanceDesc()` checks against engine-enum values, `4 & 3 = 0` and water is never detected. Always verify which enum `GetShaderTypes()` returns before writing a bitwise test against it.

Note: both enums use sequential values (not powers of 2), making `|=` accumulation and `&` bitwise tests potentially ambiguous when multiple shader types are OR'd together.

## TopLevelAS — `src/Types/TopLevelAS.h`
- `Update()` iterates visible instances, calls `instance->model->BuildUpdateBLAS()` then `instance->GetInstanceDesc()`.
- Instance descs are passed to `commandList->buildTopLevelAccelStruct()`.
- TLAS is rebuilt/resized when instance count exceeds `m_NumInstances - Constants::TLAS_INSTANCES_THRESHOLD`.
- Listeners (`ITLASUpdateListener`) are notified on TLAS resize (e.g. GI pass rebinds the handle).

## Shader Compilation Pipeline — `src/Utils/Shader.cpp`
- `ShaderDefine` holds a `wstring name` + `wstring value`. Integer values are converted via `std::to_wstring()`.
- `GetDXCDefines()` converts `eastl::vector<ShaderDefine>` → `eastl::vector<DxcDefine>` for DXC.
- GI pass compiles `RayGeneration.hlsl`, `Miss.hlsl`, `ClosestHit.hlsl`, `AnyHit.hlsl` with the same defines.
- Compute pipeline (ray query) only compiles `RayGeneration.hlsl` at shader model `cs_6_5`.

## C++ Material System — `src/Core/Skyrim/Material.h`, `src/Core/Skyrim/Material.cpp`

### Architecture

`struct Material : MaterialBase` is the bridge between Creation Engine's `BSShaderMaterial` types and the raytracing shader's PBR surface. It:
1. Copies texture/color/scalar data from game engine material pointers into local arrays (`textures[20]`, `colors[3]`, `scalars[3]`, `vectors[4]`)
2. Writes that data to a GPU-visible `MaterialData` structured buffer each frame via `UpdateData()`
3. Re-reads game engine state when shader flags change via `Update()`

### Constructor flow

`Material(name, runtimeData, formID)`:
1. Default-initializes all arrays (textures filled with `blackTexture`, colors/scalars/vectors filled with identity values)
2. Reads `alphaProperty` → sets `AlphaFlags` (Blend, Test, Transmission, Additive)
3. Reads `shaderProperty`:
   - **`BSLightingShaderProperty`**: sets `shaderType = Lighting` (or `Grass` for grass forms), reads emissive color, calls `GetFeature()` on the material, then dispatches to a `Setup*()` function based on material type
   - **`BSEffectShaderProperty`**: sets `shaderType = Effect`, calls `SetupEffectMaterial()`
   - **`BSWaterShaderProperty`**: calls `SetupWaterProperty()` + `SetupWaterMaterial()`
   - **`BSDistantTreeShaderProperty`**: sets `shaderType = DistantTree`, binds tree LOD atlas
4. Post-processing: corrects alpha flags for additive blend, refraction, window transparency

### Material type dispatch in constructor

```
BSLightingShaderProperty:
  ├── skyrim_cast<BSLightingShaderMaterialLandscape>  → SetupLandMaterial()
  ├── typeid == BSLightingShaderMaterialPBRLandscape   → SetupPBRLandscapeMaterial()
  ├── typeid == BSLightingShaderMaterialPBR            → SetupPBRMaterial()
  └── skyrim_cast<BSLightingShaderMaterialBase>        → SetupLightingMaterial()
```

PBR landscape and PBR use `typeid` because `skyrim_cast` is unreliable for these types (no RTTI — `BSLightingShaderMaterialPBR` does not override `GetFeature()`).

### Setup functions

Every material type has a dedicated `Setup*()` function that populates `textures[]`, `colors[]`, `scalars[]`, `vectors[]`, and `pbrFlags` from the raw engine material pointer. Each function is self-contained — callable from both the constructor and `Update()`:

| Function | Material type | Key data populated |
|---|---|---|
| `SetupLandMaterial` | `BSLightingShaderMaterialLandscape` | Texture0-19 landscape layout, colors[2], scalars[0], vectors[0-3] |
| `SetupPBRLandscapeMaterial` | `BSLightingShaderMaterialPBRLandscape` | Sets `shaderType=TruePBR`, Texture0-19 PBR landscape layout, pbrFlags, vectors[0-3] |
| `SetupPBRMaterial` | `BSLightingShaderMaterialPBR` | Sets `shaderType=TruePBR`, Texture0-3, pbrFlags, Subsurface/TwoLayer/Fuzz/Glint flags |
| `SetupLightingMaterial` | `BSLightingShaderMaterialBase` (vanilla) | Texture0-7, specular/SSS/envmap/eye/glow/hair/facegen features |
| `SetupEffectMaterial` | `BSEffectShaderMaterial` | Emissive color, baseColorScale, source/grayscale textures |
| `SetupWaterProperty` | `BSWaterShaderProperty` | Water shader flags |
| `SetupWaterMaterial` | `BSWaterShaderMaterial` | Shallow/deep/reflection colors, amplitudes, normal textures |

### TextureDefaults helper

`Material::TextureDefaults` struct holds references to the six default texture descriptor handles (gray, normal, black, white, rmaos, detail). `GetDefaultTextures()` returns a populated struct from the Renderer singleton. All `Setup*` functions use `auto defaults = GetDefaultTextures();` then access `defaults.gray`, `defaults.normal`, etc. directly — no intermediate local variables.

### Landscape texture layout (GPU-side `MaterialData`)

`MAX_LAND_TEXTURES = 6` (Skyrim), `5` (Fallout 4).

| GPU field | C++ array index | Vanilla landscape source | PBR landscape source |
|---|---|---|---|
| Texture0-5 | 0..5 | `diffuseTexture` + `landscapeDiffuseTexture[0..4]` | `landscapeBaseColorTextures[0..5]` |
| Texture6-11 | 6..11 | `normalTexture` + `landscapeNormalTexture[0..4]` | `landscapeNormalTextures[0..5]` |
| Texture12-17 | 12..17 | (unused) | `landscapeRMAOSTextures[0..5]` |
| Texture18 | 18 | `terrainOverlayTexture` | `terrainOverlayTexture` |
| Texture19 | 19 | `terrainNoiseTexture` | `terrainNoiseTexture` |

### Shader-side consuming of landscape textures

`LandMaterial()` in `SurfaceSkyrim.hlsli`:
- Diffuse/BaseColor: `material.Texture0` through `material.Texture5`
- Normals: `material.Texture6` through `material.Texture11`
- RMAOS (PBR): `material.Texture12` through `material.Texture17`
- Overlay/Noise: `material.OverlayTexture()` (=Texture18), `material.NoiseTexture()` (=Texture19)

`DefaultMaterial()` in `SurfaceSkyrim.hlsli` (non-landscape):
- Base texture: `material.BaseTexture()` (=Texture0)
- Normal texture: `material.NormalTexture()` (=Texture1)

### Material::Update() dispatch

Called when shader flags change at runtime. Dispatches to the correct `Setup*()` function based on the material's type:

```
shaderType == Water       → UpdateWaterMaterial()
shaderType == Effect      → SetupEffectMaterial()
shaderType == Lighting:
    feature is landscape  → SetupLandMaterial()
    else                  → SetupLightingMaterial()
shaderType == TruePBR:
    typeid is PBR landscape → SetupPBRLandscapeMaterial()
    else                    → SetupPBRMaterial()
// Grass, DistantTree — no runtime updates needed
```

The landscape check for `Lighting` uses `feature == Feature::kMultiTexLand || Feature::kMultiTexLandLODBlend`. The PBR-landscape check uses `typeid` (matching the constructor). After material setup, `SetupProjectedUV()` is called for all Lighting/TruePBR materials.

### UpdateData()

Writes `m_MaterialData` to the GPU buffer each frame via `nvrhi::ICommandList::writeBuffer()`. Skips the write if data hasn't changed since last frame (`m_PrevMaterialData` comparison). Descriptor indices are resolved from the weak `Texture` pointers via `GetTextureDescriptorIndex()`.

### Key class hierarchy relationships

```
BSLightingShaderMaterialBase
├── BSLightingShaderMaterialLandscape      (vanilla landscape)
├── BSLightingShaderMaterialPBR            (PBR non-landscape)
└── BSLightingShaderMaterialPBRLandscape   (PBR landscape — NOT a child of the vanilla landscape class!)
```

`BSLightingShaderMaterialPBRLandscape` inherits from `BSLightingShaderMaterialBase` directly. It does NOT inherit from `BSLightingShaderMaterialLandscape`. Offsets for `terrainOverlayTexture`, `terrainNoiseTexture`, `landBlendParams`, `terrainTexOffsetX/Y`, `terrainTexFade` are preserved at the same offsets as the vanilla landscape class (verified via `static_assert` in `BSLightingShaderMaterialPBRLandscape.h`).

This means:
- `skyrim_cast<BSLightingShaderMaterialLandscape>` catches vanilla landscape but NOT PBR landscape
- PBR landscape detection requires `typeid` check
- `BSLightingShaderMaterialLandscape::GetFeature()` returns `kMultiTexLandLODBlend` (19)
- `BSLightingShaderMaterialPBRLandscape` does not override `GetFeature()` — returns `kDefault` (0) from `BSLightingShaderMaterialBase`

### Rendering-side material data struct

`interop/MaterialSkyrim.hlsli` defines `MaterialData` with matching C++ layout:
- `Texture0` through `Texture19` (uint16_t descriptor indices)
- `Color0-2` (half4), `Scalar0-2` (half), `Vector0-3` (half4)
- `AlphaFlags`, `ShaderType`, `Feature`, `PBRFlags`, `ShaderFlags`
- Helper methods: `BaseTexture()` (=Texture0), `NormalTexture()` (=Texture1), `OverlayTexture()` (=Texture18), `NoiseTexture()` (=Texture19), `SpecularColor()` (=Color2), etc.

## DXR Pipeline Architecture

### Dual Path: Ray Query vs Ray Tracing Pipeline

The engine supports two DXR modes controlled by `RendererSettings::UseRayQuery`:

| Property | Ray Query (`USE_RAY_QUERY=1`) | Ray Tracing Pipeline (`USE_RAY_QUERY=0`) |
|---|---|---|
| Shader model | `cs_6_5` (compute) | `lib_6_5` (shader library) |
| Entry point | `[numthreads(...)] void Main(uint2 idx)` | `[shader("raygeneration")] void Main()` |
| Ray dispatch | `TraceRayInline()` + `RayQuery<>.Proceed()` | `TraceRay()` → separate closest-hit/any-hit/miss shaders |
| Pipeline | `nvrhi::ComputePipeline` | `nvrhi::rt::PipelineHandle` |
| Binding visibility | `nvrhi::ShaderType::Compute` | `nvrhi::ShaderType::AllRayTracing` |
| Index source | `Camera.RenderSize` | `DispatchRaysIndex()`, `DispatchRaysDimensions()` |
| Dispatch call | `commandList->dispatch(threadGroups)` | `commandList->dispatchRays(args)` |
| Thread group size | `THREAD_GROUP_SIZE x THREAD_GROUP_SIZE` | 1 thread per pixel (DXR-managed) |

### Pipeline Creation Pattern

For each raytracing pass (PathTracing, GBuffer, GlobalIllumination), the C++ code follows this pattern:

1. **`CreatePipeline()`** — entry point, branches to `CreateComputePipeline()` or `CreateRayTracingPipeline()` based on `UseRayQuery`
2. **Shader library compilation** — `ShaderUtils::CompileShaderLibrary(device, path, defines)` compiles a `.hlsl` file into a DXIL library
3. **Pipeline description** — populates `nvrhi::rt::PipelineDesc` with:
   - `shaders`: ray-gen, miss shader export names
   - `hitGroups`: hit group export names with closest-hit + any-hit + intersection shaders
   - `globalBindingLayouts`: descriptor layouts (spaces) for SRVs, UAVs, CBVs, samplers
   - `maxPayloadSize`: in bytes (must be ≥ size of largest payload struct)
   - `maxRecursionDepth`: defaults to 1 in NVRHI
4. **Shader table** — created from the pipeline, populated with `setRayGenerationShader()`, `addMissShader()`, `addHitGroup()`. Indices are assigned in the order they're added.

### Shader Table Index Semantics

The shader table is a GPU buffer containing shader identifiers + local root arguments. At `dispatchRays`, DXR uses:
- **RayGeneration index**: always 0 (there's exactly one ray-gen entry)
- **Miss index**: offset into the miss shader table (0-based, order of `addMissShader` calls)
- **HitGroup index**: offset into the hit group table (0-based, order of `addHitGroup` calls)

These indices are passed as arguments to `TraceRay()` in HLSL. The defines in `shaders/raytracing/include/Common.hlsli` hardcode them:

```hlsl
#define DIFFUSE_RAY_HITGROUP_IDX 0
#define DIFFUSE_RAY_MISS_IDX 0
#define SHADOW_RAY_HITGROUP_IDX 1
#define SHADOW_RAY_MISS_IDX 1
```

Shadow rays use index 1 because they use `RAY_FLAG_SKIP_CLOSEST_HIT_SHADER` (closest-hit shader is never invoked) and need shadow-specific any-hit/miss behavior. Both regular and shadow hit groups + miss shaders must exist in the pipeline and shader table for `TraceRayShadow` to work.

### DXR Shader File Structure

**`shaders/raytracing/Common/`** — shared by all raytracing passes:

| File | Shader Type | Payload Type | Purpose |
|---|---|---|---|
| `ClosestHit.hlsl` | `[shader("closesthit")]` | `Payload` | Packs `InstanceID()`, `GeometryIndex()`, `PrimitiveIndex()`, barycentrics, and `RayTCurrent()` into payload |
| `AnyHit.hlsl` | `[shader("anyhit")]` | `Payload` | Tests alpha transparency via `ConsiderTransparentMaterial()`; calls `IgnoreHit()` if transparent |
| `Miss.hlsl` | `[shader("miss")]` | `Payload` | No-op (payload retains default values from `TraceRayStandard` initialization) |
| `ShadowAnyHit.hlsl` | `[shader("anyhit")]` | `ShadowPayload` | Tests shadow transparency via `ConsiderTransparentMaterialShadow()` using DXR built-ins |
| `ShadowMiss.hlsl` | `[shader("miss")]` | `ShadowPayload` | Sets `payload.missed = 1.0f` to signal the ray missed |

**`shaders/raytracing/Pathtracing/`**, **`GBuffer/`**, **`GlobalIllumination/`** — per-pass ray generation shaders and register declarations.

### DXR Payload Types

Two payload structs, both exactly **20 bytes** (5 uint32):

```hlsl
struct Payload {           // Used by primary and bounce rays
    float hitDistance;     // offset 0
    uint primitiveIndex;   // offset 4
    uint barycentricsPacked;           // offset 8
    uint instanceGeometryIndexPacked;  // offset 12
    uint randomSeed;       // offset 16
};

struct ShadowPayload {     // Used by shadow/occlusion rays
    float missed;          // offset 0  (1.0 = missed, 0.0 = hit)
    float3 transmission;   // offset 4  (accumulated Beer-Lambert)
    uint randomSeed;       // offset 16
};
```

Both must be ≤ `maxPayloadSize` (set to 20 in pipeline descs). The `Payload` type is read/written by closest-hit, any-hit, and miss shaders for primary/bounce rays. `ShadowPayload` is read/written by shadow any-hit and shadow miss shaders.

### Per-Pass Register/Space Layout (PathTracing)

The `Registers.hlsli` files declare GPU resources at specific registers and DXR "spaces." The space number corresponds to the position in `pipelineDesc.globalBindingLayouts` (0-indexed after the global binding layout at space0):

| Space | Register | Content | Pipeline Layout |
|---|---|---|---|
| space0 | b0-b3, t0-t8, u0-u17, s0-s2, t0 | Global: Camera, Raytracing, Features CBs; TLAS; all UAVs; samplers; sky/flow map SRVs; light/instance/mesh buffers | `m_BindingLayout` |
| space1 | t0 | `Triangles[]` (`StructuredBuffer<Triangle>`) | `TriangleDescriptors` |
| space2 | t0 | `Vertices[]` (`StructuredBuffer<Vertex>`) | `VertexDescriptors` |
| space3 | t0 | `Materials[]` (per-mesh `StructuredBuffer<Material>`) | `MaterialDescriptors` |
| space4 | t0 | `Textures[]` (all material textures as descriptor array) | `TextureDescriptors` |
| space5 | t0 | `PrevPositions[]` (optional, for skinned meshes) or `LightTLAS` (RTXDI) | `PrevPositionDescriptors` |
| space6/7 | t0 | `CubeTextures[]` (environment maps) | `CubemapDescriptors` |

Note: `GBuffer` pipeline omits `MaterialDescriptors` (space3) — its ray-gen shader reads materials differently. `GlobalIllumination` pipeline omits `PrevPositionDescriptors`.

### DXR Built-In Functions Per Shader Type

| Built-in | RayGen | AnyHit | ClosestHit | Miss | Intersection |
|---|---|---|---|---|---|
| `DispatchRaysIndex()` | ✓ | — | — | — | — |
| `DispatchRaysDimensions()` | ✓ | — | — | — | — |
| `WorldRayOrigin()` | ✓ | ✓ | ✓ | ✓ | ✓ |
| `WorldRayDirection()` | ✓ | ✓ | ✓ | ✓ | ✓ |
| `RayTCurrent()` | — | ✓ | ✓ | — | ✓ |
| `RayTMin()` | — | ✓ | ✓ | ✓ | ✓ |
| `InstanceID()` | — | ✓ | ✓ | — | ✓ |
| `GeometryIndex()` | — | ✓ | ✓ | — | ✓ |
| `PrimitiveIndex()` | — | ✓ | ✓ | — | ✓ |
| `ObjectRayOrigin()` | — | ✓ | ✓ | ✓ | ✓ |
| `ObjectRayDirection()` | — | ✓ | ✓ | ✓ | ✓ |

Key: **Any-hit shader runs before closest-hit shader.** Any-hit should use built-ins (`InstanceID()`, `GeometryIndex()`, `PrimitiveIndex()`) for geometry identification, not payload fields — the closest-hit shader hasn't packed them yet.

### Renderer & Command List Execution — `src/Renderer.cpp`

- **`StartExecution()`**: creates a new `GraphicsCommandList` each frame, opens it
- **`EndExecution()`**: copies depth/MV targets if in PT mode, closes command list, executes on **Graphics queue** via `executeCommandList(commandList, CommandQueue::Graphics)`
- **`WaitExecution()`**: fences on the render graph event query
- **`PostExecution()`**: polls timer queries, increments frame index, runs garbage collection
- **`GetDynamicResolution()`**: applies `m_DynamicResolutionRatio` to `m_RenderSize`
- All passes (including raytracing `dispatchRays`) execute on the **Graphics** command queue — NVRHI handles the DXR state transitions alongside raster/compute work

### Per-Pass Pipeline Files (C++)

| Pass | File | RayQuery? | Notes |
|---|---|---|---|
| `Pass::PathTracing` | `src/Pass/Raytracing/PathTracing.cpp` | Both | Three modes: Reference, BUILD, FILL (stable planes). Creates separate pipeline+table per mode. |
| `Pass::Raytracing::GBuffer` | `src/Pass/Raytracing/GBuffer.cpp` | Both | Single pipeline/table. |
| `Pass::Raytracing::GlobalIllumination` | `src/Pass/Raytracing/GlobalIllumination.cpp` | Both | Single pipeline/table. GI uses `INSTANCE_MASK=1` (NonWater). |
| `Pass::Raytracing::ReSTIRGIPass` | `src/Pass/Raytracing/ReSTIRGIPass.cpp` | Compute only | 4 sub-passes (temporal, spatial, fused, final shading). Always uses `USE_RAY_QUERY=1`. |
| `Pass::Raytracing::Common::SHaRC` | `src/Pass/Raytracing/Common/SHaRC.cpp` | Compute only | SH radiance cache update + resolve. |
| `Pass::Raytracing::Common::SHaRCGI` | `src/Pass/Raytracing/Common/SHaRCGI.cpp` | Compute only | SH radiance cache for GI. |

### Ray Dispatch Flow (`shaders/raytracing/include/Rays.hlsli`)

Five ray dispatch functions, all branched on `USE_RAY_QUERY`:

| Function | Payload | Flags | Purpose |
|---|---|---|---|
| `TraceRayOpaque` | `Payload` | `FORCE_OPAQUE` | Always skips any-hit (fully opaque rays) |
| `TraceRayStandard` | `Payload` | `SKIP_PROCEDURAL_PRIMITIVES` | Primary and bounce rays; any-hit tests alpha |
| `TraceRayShadow` | `ShadowPayload` | `SKIP_PROCEDURAL \| SKIP_CLOSEST_HIT` | NEE shadow rays; any-hit tests shadow transparency, accumulates Beer-Lambert |
| `TraceRayShadowFinite` | `ShadowPayload` | `SKIP_PROCEDURAL \| SKIP_CLOSEST_HIT` | Shadow rays with finite tmax (point/spot lights) |
| `SampleSubsurface` | `Payload` | `CULL_BACK_FACING \| SKIP_PROCEDURAL` | SSS probe ray, shoots from surface toward interior |

For `TraceRayShadow`, shadow transmission is computed as `exp(-VolumeAbsorption * hitDistance)` for water materials, and `F_Schlick * (1-F)/(1+F)` for specular transparent materials. The `ShadowPayload.transmission` accumulates multiplicatively across transparent hits.

### Shader Utility Functions

- **`ShaderUtils::CompileShaderLibrary(device, path, defines)`** — compiles a `.hlsl` file to a DXIL library via DXC, returns `nvrhi::ShaderLibraryHandle`
- **`ShaderCache::GetShader(path, defines, target)`** — cached shader compilation, returns `IDxcBlob*`. Used by compute/rayquery paths (`cs_6_5` target)
- **`Util::Shader::GetDXCDefines(shaderDefines)`** — converts `eastl::vector<ShaderDefine>` to `eastl::vector<DxcDefine>`
- **`Util::Shader::GetPathTracingDefines(settings, hasSharc, isUpdate)`** — builds define list for PT pass
- **`Util::Shader::GetGlobalIlluminationDefines()`** — builds define list for GI pass (includes `INSTANCE_MASK=1`)

### Render Target Manager — `src/Renderer/RenderTargetManager.h`

Manages render target textures shared across passes. Uses an enum `RenderTarget` to identify targets. Common targets:
- `Main` — color output
- `ClipDepth` — clip-space depth
- `MotionVectors3D` — motion vectors (RGBA16)
- `DiffuseAlbedo`, `ViewDepth`, `DiffuseRadiance`, `SpecularRadiance`, etc. — NRD denoiser inputs
- `RRSpecularAlbedo`, `RRSpecularHitDist` — DLSS Ray Reconstruction inputs

### Supported Features Detection — `src/Types/SupportedFeatures.h`

```cpp
enum SupportedFeatures : uint32_t {
    Raytracing       = 1 << 0,  // DXR 1.0 pipeline (RayTracingPipeline)
    InlineRaytracing = 1 << 1,  // DXR 1.1 RayQuery
    OpacityMicroMaps = 1 << 2,  // NVIDIA OMM
    LinearSweptSpheres = 1 << 3, // NVIDIA LSS
    ShaderExecutionReordering = 1 << 4, // NVIDIA SER
};
```

Feature detection runs at `Renderer::Initialize()` via `queryFeatureSupport()`. The `UseRayQuery` setting in `RendererSettings` defaults to `false` but can be overridden. `ReSTIRGIPass`, `SHaRC`, and `SHaRCGI` unconditionally use ray query regardless of this setting.

## Payload Helper Methods

The `Payload` struct in `shaders/raytracing/include/Payload.hlsli` provides two helper methods that consolidate repeated initialization and result-extraction patterns:

```hlsl
void Init(inout uint seed)
```
Sets `hitDistance = -1.0f`, zeroes `primitiveIndex`, packs zero barycentrics and (0,0) instance/geometry index, and stores the seed. Used by all ray dispatch functions instead of manual field-by-field initialization.

```hlsl
void SetCommittedHit(float hitT, uint primIndex, float2 barycentrics, uint instanceIndex, uint geometryIndex)
```
Extracts committed-triangle-hit data from a `RayQuery` result into the payload. Replaces the 5-line pattern of manually reading `CommittedRayT()`, `CommittedPrimitiveIndex()`, `CommittedTriangleBarycentrics()`, `CommittedInstanceIndex()`, `CommittedGeometryIndex()`.

## Rays.hlsli — Ray Dispatch Functions

Five functions in `shaders/raytracing/include/Rays.hlsli`, all branching on `USE_RAY_QUERY`:

| Function | Payload Type | Extra Flags | Purpose |
|---|---|---|---|
| `TraceRayOpaque` | `Payload` | `RAY_FLAG_FORCE_OPAQUE` | Opaque rays that skip any-hit entirely |
| `TraceRayStandard` | `Payload` | (none beyond `RAY_FLAGS`) | Primary and bounce rays; any-hit evaluates `ConsiderTransparentMaterial()` for alpha |
| `TraceRayShadow` | `ShadowPayload` | `RAY_FLAG_SKIP_CLOSEST_HIT` | NEE shadow ray with `SHADOW_RAY_TMAX=1e5f`; delegates to `TraceRayShadowFinite` |
| `TraceRayShadowFinite` | `ShadowPayload` | `RAY_FLAG_SKIP_CLOSEST_HIT` | Shadow ray with caller-supplied `tmax` (used for point/spot light falloff ranges) |
| `SampleSubsurface` | `Payload` | `RAY_FLAG_CULL_BACK_FACING` | SSS probe ray shooting toward the surface from inside; any-hit evaluates transparency |

### TraceRayShadow / TraceRayShadowFinite relationship

`TraceRayShadow` is a thin wrapper that calls `TraceRayShadowFinite(scene, surface, direction, SHADOW_RAY_TMAX, randomSeed)`. All shadow ray logic lives in `TraceRayShadowFinite`. The `ShadowPayload.transmission` accumulates multiplicatively across transparent hits (via `ConsiderTransparentMaterialShadow` which multiplies into the `transmitanceInOut` inout parameter). The return value is `shadowPayload.transmission * shadowPayload.missed` — fully attenuated (0,0,0) when fully occluded, (1,1,1) when unoccluded, and partial in between.

### Ray Query Candidate Loop Pattern (all functions)

Every `USE_RAY_QUERY` branch follows the same pattern:
1. `payload.Init(randomSeed)` or equivalent for `ShadowPayload`
2. `RayQuery<RAY_FLAGS | EXTRA_FLAGS> rayQuery; rayQuery.TraceRayInline(...)`
3. `while (rayQuery.Proceed())` — for each candidate non-opaque triangle, call the appropriate `ConsiderTransparentMaterial*()` function; if it returns true, `rayQuery.CommitNonOpaqueTriangleHit()`
4. After loop: check `CommittedStatus()` and extract hit data or mark as miss

## Transparency.hlsli — Transparent Material Handling

Two functions in `shaders/raytracing/include/Transparency.hlsli`:

### `ConsiderTransparentMaterial(instanceIndex, geometryIndex, primitiveIndex, barycentrics, inout randomSeed) → bool`

Used for primary/bounce rays (`TraceRayStandard`, `TraceRayOpaque`, `SampleSubsurface`). For each candidate non-opaque triangle hit:

1. Calls `GetMesh()` + `GetVertices()` + `GetMaterial()` to identify the material
2. If `material.ShaderType == ShaderType::Water`: immediately returns `true` (water is always transparent — treated via `WaterMaterial()` at the surface construction stage)
3. Samples the base texture alpha at mip 0, multiplies by `material.BaseColor().a * instance.Alpha`
4. If `kVertexAlpha`: multiplies by interpolated vertex color alpha
5. **Alpha Test**: if alpha < material threshold, returns `false` (ray passes through)
6. **Alpha Additive**: sets alpha to 0 (fully transparent)
7. **Alpha Blend**: stochastic discard — `Random() < alpha` passes (hit), otherwise passes through (returns `false`)
8. Returns `true` → ray commits the hit

### `ConsiderTransparentMaterialShadow(instanceIndex, geometryIndex, primitiveIndex, barycentrics, inout randomSeed, direction, hitDistance, inout transmitanceInOut) → bool`

Used for shadow rays. Returns `false` when the function has already factored the hit's attenuation into `transmitanceInOut` (no need to commit the hit):

**Water path** (returns `false`):
- Calls `WaterMaterial()` on the fly to get `surface.VolumeAbsorption`, `surface.F0`, and `surface.Normal`
- Computes Beer-Lambert: `transmittance = exp(-VolumeAbsorption * hitDistance)`
- Computes Fresnel: `F = F_Schlick(F0, NdotV)`, applies `(1-F)/(1+F)` factor
- Multiplies `transmitanceInOut *= transmittance` — absorbs light, no further hits needed

**Effect passthrough** (when `EFFECT_PASSTHROUGH` is defined, returns `false`):
- Effect/particle materials never cast shadows — ray passes through

**Non-water path** (may return `true` or `false`):
- Same alpha evaluation as `ConsiderTransparentMaterial` (alpha test, additive, blend)
- **Transmission/Refraction materials** (`AlphaFlags::Transmission` or `kRefraction`): samples base texture RGB, computes normal-mapped Fresnel attenuation, multiplies into `transmitanceInOut`, returns `false`
- **Emissive/glow windows** (`kGlowMap` or PBR emissive + `kAssumeShadowmask`): samples glow/emissive textures, computes Fresnel, sets `transmitanceInOut *= transmittance`, returns `false`
- Otherwise returns `true` (opaque hit, ray commits normally)

## Path Tracer Bounce Loop Structure

`shaders/raytracing/Pathtracing/RayGeneration.hlsli` — the full path tracing shader:

### Execution Flow

```
1. SetupPrimaryRay() → TraceRayStandard() → primary hit or miss
2. If miss: write sky/miss outputs, return
3. Effect passthrough loop (up to 16 iterations, if EFFECT_PASSTHROUGH)
4. SurfaceMaker::make() → construct primary Surface
5. BRDFContext::make() + StandardBSDF::make()
6. AdjustShadingNormal()
7. GBuffer write (NormalRoughness, DiffuseAlbedo, MotionVectors, Depth, ViewDepth, SpecularAlbedo)
8. Direct lighting on primary surface (NEE + delta lobe lighting)
9. for (i < MAX_SAMPLES)        // outer loop: sample accumulation
      for (j < MAX_BOUNCES)     // inner loop: path tracing bounces
          BSDF::SampleBSDF() → direction, weight, lobe flags
          throughput *= brdfWeight
          Russian roulette (if !SHARC_UPDATE)
          OffsetRay/OffsetRaySIA → new origin
          TraceRayStandard() → next hit
          Water volume absorption on throughput
          Effect passthrough sub-loop (up to 16 iterations)
          ReSTIR GI surface capture (FILL mode)
          SHaRC cache lookup → early termination on cache hit
          SurfaceMaker::make() → new surface
          BRDFContext + StandardBSDF + AdjustShadingNormal
          Direct lighting on bounce surface (NEE + delta lobe lighting)
10. Sample radiance accumulated, divided by MAX_SAMPLES
11. Output (NRD-packed or direct + gamma)
```

### Three Path Tracer Modes

| Mode | Define | Behavior |
|---|---|---|
| `PATH_TRACER_MODE_REFERENCE` | Default | Standard Monte Carlo path tracing, `MAX_SAMPLES` samples per pixel, `MAX_BOUNCES` bounces each |
| `PATH_TRACER_MODE_BUILD_STABLE_PLANES` | Stable planes BUILD pass | Deterministic delta-path exploration; records surface hits into stable plane buffer for later FILL pass |
| `PATH_TRACER_MODE_FILL_STABLE_PLANES` | Stable planes FILL pass | Replays stable plane paths; narrow-window re-trace to re-hit surfaces; denoises with spatial/temporal data |

### Effect Passthrough

When `EFFECT_PASSTHROUGH` is defined and a ray hits an Effect-shader material:
- A sub-loop (max 16 iterations) accumulates the effect material's `Emissive` into the path radiance
- The ray continues forward from beyond the effect surface in the same direction
- On sky miss: sky radiance + accumulated emissive is the result
- This is used on the primary ray hit and every bounce hit
- Effect materials never receive bounce lighting or NEE

### Water Volume Tracking

Throughout the bounce loop, two variables track whether the ray is inside a water volume:
- `insideWaterVolume`: bool, set initially from `Camera.IsUnderwater`
- `waterVolumeAbsorption`: float3, the current volume's absorption coefficients

On each transmission event (BSDF sample with `LobeType::Transmission` and non-zero `VolumeAbsorption`):
- `isEnter` (front-face hit) means the ray is entering the volume → `insideWaterVolume = true`
- Back-face hit means exiting → `insideWaterVolume = false`
- `waterVolumeAbsorption` is updated to the surface's `VolumeAbsorption` when entering

On every ray segment while `insideWaterVolume == true`:
- `throughput *= exp(-waterVolumeAbsorption * payload.hitDistance)` (Beer-Lambert)

### Russian Roulette

Applied per bounce (when `!SHARC_UPDATE` and `Raytracing.RussianRoulette == 1`):

```hlsl
float rrVal = sqrt(Color::RGBToLuminance(throughputColor));
float rrProb = saturate(0.85 - rrVal);
rrProb *= rrProb;                                          // quadratic scaling
rrProb = saturate(rrProb + max(0, float(j) / MAX_BOUNCES - 0.4));  // ramps up with bounce count
if (Random(randomSeed) < rrProb) break;                    // terminate
throughput /= (1.0 - rrProb);                              // compensate for termination
```

### Lobe Checks in Bounce Loop

After `BSDF::SampleBSDF()` returns a `BSDFSample`:
- `bsdfSample.isLobe(LobeType::Delta)` — mirror/glass; no ray cone expansion, special offset handling
- `bsdfSample.isLobe(LobeType::Specular) || isDelta` — marks sample as specular (affects NRD hit distance splitting)
- `bsdfSample.isLobe(LobeType::Transmission)` — entering/exiting a medium; triggers water volume tracking
- `bsdfSample.isLobe(LobeType::DiffuseReflection)` — diffuse; used to split `brdfWeight.diffuse`
- `bsdfSample.isLobe(LobeType::SpecularReflection) || isLobe(LobeType::DeltaReflection)` — sets `brdfWeight.specular`
- `surface.Primary && isDelta` — `isPrimaryReplacement` flag: delta surface replaces the primary hit for bounce numbering

`surface.AO` is applied to throughput for all non-transmission lobes.

### RayCone Propagation

`RayCone` tracks the pixel footprint spread for texture LOD computation:

```
Primary hit: RayCone = RayCone::make(PixelConeSpreadAngle * hitDistance, PixelConeSpreadAngle)
After scatter: width stays same, spreadAngle += expansion from BSDF scatter PDF (capped at 2*PI)
After trace:    rayCone = rayCone.propagateDistance(payload.hitDistance)
```

LOD is computed from the ray cone in `SurfaceMaker::make()` and stored in `surface.MipLevel`, later used by all texture samples in material functions (DefaultMaterial, LandMaterial, etc.).

### Direct Lighting on Bounce Surfaces

Every bounce surface evaluates:
1. **Non-delta NEE** via `EvaluateDirectRadiance()` — if the surface has non-delta lobes, directional/point lights are sampled and the BSDF is evaluated at the light direction
2. **Delta lobe lighting** via `EvalDeltaLobeLighting()` — for surfaces with delta lobes, checks if delta reflection/refraction directions fall within light source solid angles
3. **Subsurface scattering** (when `SUBSURFACE_SCATTERING`): if the surface has `HasSubsurface`, `EvaluateSubsurfaceDiffuseNEE()` replaces the diffuse portion of NEE
4. Direct radiance + surface emissive are added to `sampleRadiance` multiplied by the current `throughput`

### BSDFSample Struct

Returned by `BSDF::SampleBSDF()`, consumed in the bounce loop:

| Field | Type | Meaning |
|---|---|---|
| `wo` | `float3` | Sampled outgoing direction (world space) |
| `weight` | `float3` | BSDF contribution weight (throughput multiplier) |
| `pdf` | `float` | Probability density of the sampled direction |
| `lobe` | `uint` | Bitmask of lobe type flags |

```hlsl
bool isLobe(LobeType type) { return (lobe & (uint)type) != 0; }
```

### SHaRC Integration Points

When `SHARC` is defined:

**SHARC_UPDATE mode**: Each sample uses `SharcInit()`, `SharcSetThroughput()`, `SharcUpdateHit()`, and `SharcUpdateMiss()` to update the SH radiance cache in a single bounce.

**Non-update mode (SHARC only)**: After each bounce, `SharcGetCachedRadiance()` is queried. On a cache hit, the cached radiance is added to the sample and the bounce loop breaks early. The hit validity check includes:
- Hit distance must exceed voxel size * sqrt(3)
- Footprint computed from accumulated roughness must exceed voxel size
- Hair materials (`kHairTint`) are excluded

### NRD Hit Distance Tracking

When `NRD` is defined:
- `diffHitDist`: accumulates `REBLUR_FrontEnd_GetNormHitDist()` for non-specular samples
- `specHitDist`: accumulated via `NRD_FrontEnd_SpecHitDistAveraging_Begin()`/`_Add()` for specular samples
- Hit distance is normalized by view-space depth and surface roughness

### ReSTIR GI Secondary Surface Capture (FILL mode)

During FILL mode bounces, when `RESTIR_GI` is defined and `Raytracing.EnableReSTIRGI` is true:
- Before the first non-delta BSDF sample in each path, the current scattering surface and view direction are snapshotted
- After the first non-delta sample and trace, `SecondaryGBuf*` UAVs are written with the secondary surface geometry (position, normal, diffuse albedo, specular/roughness, and accumulated radiance with PDF)
- Once `giSecStarted == true`, all subsequent radiance (NEE, emissive, sky, SHaRC) is diverted to `giSecRadiance` rather than `fillPathL`
- Hair materials and surfaces with specular transmission are excluded from capture
- Secondary radiance is scaled by `throughput / giSecThroughput` to isolate the contribution from the GI bounce onward

## RayOffset.hlsli — Offset Strategies

Three ray origin offset functions in `shaders/raytracing/include/RayOffset.hlsli`:

| Function | Strategy | Used By |
|---|---|---|
| `OffsetRay(position, normal, positionError, hasTransmission)` | Position-error based (RT Gems §2.3) — computes floating-point error from the world-space position magnitude, offsets along the geometric face normal | PT shader bounces (when !USE_SIA), shadow rays |
| `OffsetRayAlt(position, normal, positionError, hasTransmission)` | Per-component integer trick — manipulates float bits directly for a fixed offset along the normal | GI shader first bounce only |
| `OffsetRaySIA(position, normal, siaOffset, hasTransmission)` | NVIWFA Self-Intersection Avoidance — uses a precomputed `siaOffset` that accounts for the full transform chain error | PT shader bounces (when USE_SIA), shadow rays (when USE_SIA) |

All three functions take `hasTransmission` to negate the normal direction when the ray is transmitting through a surface (exiting a volume). When `hasTransmission` is true, the offset direction is flipped: `normal` becomes `dot(faceNormal, direction) >= 0 ? faceNormal : -faceNormal` (offset outward from the exit face).

The offset follows the pattern: `newOrigin = position + faceNormal * max(offset * 512, epsilon)` where `epsilon` handles near-zero offsets.

## Geometry.hlsli — Data Access Chain

`shaders/raytracing/include/Geometry.hlsli` provides the full geometry lookup chain:

```
GetInstance(instanceIdx) → Instances[instanceIdx]      // from StructuredBuffer<Instance>
GetMesh(instanceIdx, geomIdx, out instance)            // → Meshes[instance.FirstGeometryID + geomIdx]
GetTriangle(meshIdx, primIdx) → Triangles[meshIdx][primIdx]
GetVertices(meshIdx, primIdx, out v0, v1, v2)          // → Vertices[meshIdx][triangle.xyz]
GetMaterial(meshIdx) → Materials[meshIdx][0]           // per-mesh material
```

Key lookup details:
- `GetMesh()` with payload: `GetMesh(payload, out instance)` calls `GetInstance(payload.GetInstanceIndex())` then indexes `Meshes[instance.FirstGeometryID + payload.GetGeometryIndex()]`
- All buffer accesses use `NonUniformResourceIndex()` since instance/geometry indices are non-uniform across the wave
- When `HAS_PREV_POSITIONS` is defined, an additional `GetVertices()` overload reads `PrevPositions[]` buffer for skinned/dynamic mesh motion vector computation
- SIA provides two functions: `SIA_SafeSpawnPoint()` (full, requires w2o inverse) and `SIA_SafeSpawnPointSimple()` (simplified, uses o2w only). Both compute object-space and world-space positions via precise MAD chains, plus a tight error-bound offset to prevent self-intersection.

## Ray Generation Entry Point Details

### Ray Query Mode (`USE_RAY_QUERY=1`)

```hlsl
[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
```
When `GROUP_TILING` is defined, uses `ThreadGroupTilingX()` to dispatch more threads than display pixels, with early `any(idx >= size)` rejection. Thread group size defaults to 32×32.

### Ray Tracing Pipeline Mode (`USE_RAY_QUERY=0`)

```hlsl
[shader("raygeneration")]
void Main()
```
Index from `DispatchRaysIndex().xy`, dimensions from `DispatchRaysDimensions().xy`.

### SHaRC Update Mode

When `SHARC_UPDATE` is defined, the pixel index is remapped to sparse 5×5 blocks for cache update distribution:
- `startIndex = Hash(idx) % 25`
- `blockOrigin = idx * 5`
- `pixelIndex = (startIndex + Camera.FrameIndex) % 25`
- `idx = blockOrigin + (pixelIndex % 5, pixelIndex / 5)`

### Output Modes

**NRD path** (`NRD` defined, REFERENCE mode):
- `Output[idx]` = direct lighting only (pre-denoise)
- `DiffuseRadiance[idx]` = REBLUR-packed diffuse radiance + hit distance
- `SpecularRadiance[idx]` = REBLUR-packed specular radiance + hit distance
- `DiffuseFactor[idx]` / `SpecularFactor[idx]` = NRD material-demodulation factors

**DLSS-RR path** (`DLSS_RR` defined, no NRD):
- `Output[idx]` = `LLTrueLinearToGamma(direct + radiance)`
- `SpecularAlbedo[idx]` = integrated specular BRDF albedo (F0 * envBRDF.x + envBRDF.y)
- `SpecularHitDistance[idx]` = min hit distance across specular samples, or `RAY_TMAX` if none

**Direct output** (no NRD, no DLSS-RR):
- `Output[idx] = float4(LLTrueLinearToGamma(direct + radiance), 1.0f)`

## BRDFContext & Surface Normal Handling

`BRDFContext` is constructed with `BRDFContext::make(surface, -rayDirection)` where `-rayDirection` is the view direction (points from hit point back to the ray origin):

```hlsl
struct BRDFContext {
    float3 ViewDirection;   // direction from hit to camera/previous bounce
    float NdotV;            // saturate(dot(surface.Normal, ViewDirection))
};
```

The back-face check pattern repeated throughout the shader:
```hlsl
bool isEnter = dot(surface.FaceNormal, brdfContext.ViewDirection) >= 0.0f;
if (!isEnter) {
    surface.FlipNormal();    // Negates Normal, GeomNormal, FaceNormal
    brdfContext.NdotV = saturate(dot(surface.Normal, brdfContext.ViewDirection));
}
```
`FlipNormal()` negates `Normal`, `GeomNormal`, and `FaceNormal`. `Tangent` and `Bitangent` are not flipped — the handedness sign handles the orientation.

After flipping, `AdjustShadingNormal(surface, brdfContext, true, false)` is called to prevent black pixels from back-facing view directions relative to the shading normal.

## SurfaceMaker::make() Overload 1 — Raytracing Path

The primary overload used by the PT shader (from ray payload + geometry):

1. Sets `surface.Primary = primary` (true for primary ray hit, false for bounces)
2. Defaults all surface properties (Position, Albedo = white, Roughness = 1, Metallic = 0, F0 = 0.04, Emissive = 0, Transmission = 0, Coat = absent, Fuzz = absent, etc.)
3. Calls `GetMesh(payload, out instance)` → triang/e material lookup chain
4. Calls `GetVertices()` → reads v0, v1, v2 (optionally with prev positions for skinned meshes)
5. Computes barycentrics from the packed payload
6. Interpolates texCoord0 from vertex texcoords
7. Computes `objectToWorld3x3 = mul((float3x3)instance.Transform, (float3x3)mesh.Transform)`
8. **SIA path**: calls `SIA_SafeSpawnPointSimple()` for precise world position, face normal, and offset; overrides `surface.Position`, `surface.FaceNormal`, `surface.SIAOffset`
9. **Non-SIA path**: estimates `PositionError` from vertex and world position magnitudes for ray offset use
10. Computes previous world position for motion vectors (applies `mesh.PrevTransform` and `instance.PrevTransform`, preserves residual from quantization)
11. Computes ray-cone triangle LOD value from vertex positions and texcoords
12. Computes object-space flat face normal; flips vertex normals if they oppose the face normal
13. Interpolates handedness, normalWS, tangentWS; computes bitangentWS via cross
14. Computes `surface.MipLevel` from ray cone + base texture dimensions + `Raytracing.TexLODBias`
15. Sets `surface.GeomNormal`, `surface.GeomTangent`, then defaults `surface.Normal/Tangent/Bitangent` to those
16. Dispatches to material function based on `material.Feature` and `material.ShaderType`: LandMaterial, EffectMaterial, WaterMaterial, DistantTreeMaterial, GrassMaterial, or DefaultMaterial
17. Applies roughness/metallic remapping: `PBR::Roughness(surface.Roughness, Raytracing.Roughness.x, Raytracing.Roughness.y)` and `Remap(surface.Metallic, Raytracing.Metalness.x, Raytracing.Metalness.y)`
18. Computes `surface.DiffuseAlbedo = surface.Albedo * (1 - surface.Metallic)`
19. Computes `surface.F0 = PBR::F0(surface.F0, surface.Albedo, surface.Metallic)` (3-arg overload)
20. Computes `surface.IOR = F0toIOR(surface.F0)`

## LobeType Enum Reference

Defined in `shaders/raytracing/include/Materials/LobeType.hlsli`:

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
    // Composite categories
    Diffuse   = DiffuseReflection | DiffuseTransmission,
    Specular  = SpecularReflection | SpecularTransmission,
    Delta     = DeltaReflection | DeltaTransmission,
    Transmission = DiffuseTransmission | SpecularTransmission | DeltaTransmission,
    Coat      = CoatReflection,
    Fuzz      = FuzzReflection,
    NonDelta  = ~Delta,   // All lobes except delta
};
```

In the bounce loop, `bsdfSample.isLobe(LobeType::Delta)` gates behavior divergence: delta surfaces skip ray cone expansion, use different `isPrimaryReplacement` logic, and `arrivedViaDelta` suppresses SHaRC cache queries.

## MonteCarlo Namespace

`shaders/raytracing/include/MonteCarlo.hlsli` provides:
- `BRDFWeight` struct — `{ float3 diffuse; float3 specular; float3 transmission; }` — RGB weights per lobe category
- GGX VNDF sampling functions
- `BRDF::EnvBRDF(roughness, NdotV)` → `float2(F0_scale, F0_bias)` for environment BRDF integration
- `ComputeRayConeSpreadAngleExpansionByScatterPDF(pdf)` → spread angle increase from a scatter event
- Random seed utilities

## Surface Struct Methods

```hlsl
float3 Mul(float3 tangentSample)    // TangentToWorld: Tangent*x + Bitangent*y + Normal*z
float3 ToLocal(float3 v)            // dot(v, Tangent/Bitangent/Normal)
float3 FromLocal(float3 v)          // Same as Mul
float3 CoatToLocal(float3 v)        // dot(v, CoatTangent/CoatBitangent/CoatNormal)
float3 CoatFromLocal(float3 v)      // CoatTangent*x + CoatBitangent*y + CoatNormal*z
void   FlipNormal()                 // Negates Normal, GeomNormal, FaceNormal
```

`Mul()` is used in `LIGHTING_MODE_DIFFUSE` to transform cosine-hemisphere samples to world space: `direction = surface.Mul(SampleCosineHemisphere(randomSeed))`.

## Stable Planes System Overview

Stable planes is a temporal denoising technique for path tracing that decomposes light transport into deterministic delta paths (mirror/glass reflections/refractions) and stochastic diffuse paths:

- **BUILD pass**: Explores all delta paths deterministically from the camera, forking at each delta surface. Records each surface hit into the stable plane buffer (`StablePlanesHandleHit`). Delta continuation continues via `hitResult.continueTracing` loop. After all delta forks are recorded, `FindNextToExplore` iterates remaining paths.
- **FILL pass**: Replays stored stable plane paths with narrow-window re-tracing (`FirstHitFromVBuffer` gives `TMin`/`TMax` around the expected hit). Diffuse components are Monte Carlo sampled and accumulated with plane-scoped throughput (`fillPlaneThp`). Each plane's denoiser radiance is committed via `spCtx.CommitDenoiserRadiance()`.
- **Dominant plane**: The plane with the lowest roughness history is flagged as dominant and provides the GBuffer normal/roughness for the denoiser.
- Delta lobe lighting captured during BUILD is baked into the plane's stable radiance; FILL mode only evaluates non-delta NEE to avoid double-counting.

## Surface Creation from Explicit GBuffer Params (Overload 3)

Used by the GI shader, this is the simplest overload:

```hlsl
static Surface make(float3 position, float3 faceNormal, float3 normal, float3 tangent,
                    float3 bitangent, float3 albedo, float roughness, float metallic,
                    float3 emissive, float ao)
```

Key differences from the raytracing overload:
- `PositionError = max(abs(position.x), max(abs(position.y), abs(position.z)))` (simple magnitude-based)
- `MipLevel = 0.0f + Raytracing.TexLODBias` (no ray cone — GI GBuffer data is already rasterized)
- `emissive` is scaled by `Raytracing.Emissive` at construction time
- Does NOT set Glint/Fuzz properties (zeroed by default; not used by GI)
- Uses `PBR::F0(albedo, metallic)` (2-arg overload) instead of the 3-arg

## BLAS Maintenance Rebuild System (`src/Core/Model.cpp`, `src/SceneGraph.h/.cpp`, `src/Constants.h`)

### Problem

Per `nvrhi::rt::AccelStructBuildFlags::PerformUpdate`, repeated BLAS updates (in-place refits) degrade ray traversal performance over time compared to fresh rebuilds (scratch builds). Models flagged `Dynamic`, `Skinned`, or `LandLOD` get `PreferFastBuild` and `PerformUpdate`, meaning their BLAS is refit in-place every time vertex/skin/transform data changes — often every frame. Without periodic full rebuilds, traversal quality degrades.

### Solution

`Model::BuildUpdateBLAS()` now tracks consecutive update-only operations and forces a full rebuild after a configurable threshold, gated by a per-frame limit to avoid rebuild spikes.

### Constants (`Constants.h`)

| Constant | Default | Purpose |
|---|---|---|
| `MAX_BLAS_UPDATES_BEFORE_MAINTENANCE` | 256 | Number of consecutive update-only BLAS operations before a maintenance rebuild is triggered |
| `MAX_BLAS_MAINTENANCE_REBUILDS_PER_FRAME` | 8 | Maximum maintenance rebuilds allowed per frame (prevents rebuild spikes when many models hit the threshold simultaneously) |

### Model Counter (`Model.h`)

```cpp
uint32_t m_NumUpdatesSinceRebuild = 0;
```

Tracks how many consecutive `BuildUpdateBLAS` calls resulted in an update (not rebuild) since the last rebuild of any kind (Visibility, Mesh, initial create, or maintenance). Resets to 0 on any rebuild. Increments only when a normal update is performed (not when already past the maintenance threshold).

### SceneGraph Per-Frame Gating (`SceneGraph.h`)

```cpp
uint64_t m_LastMaintenanceFrame = Constants::INVALID_FRAME_INDEX;
uint32_t m_MaintenanceRebuildsThisFrame = 0;

bool TryMaintenanceRebuild(uint64_t frameIndex);
```

`TryMaintenanceRebuild()` auto-resets the per-frame counter when `frameIndex` changes, then returns `true` if under the limit (and increments the counter). Returns `false` if the per-frame quota is exhausted; the model falls back to a normal update and retries next frame.

### Decision Flow in `BuildUpdateBLAS()`

```
1. Guard: skip if already processed this frame (m_LastBLASUpdate == frameIndex)
2. create = !m_BLAS  (initial build needed)
3. Decision tree:
   ├── create || Visibility dirty || Mesh dirty
   │   → rebuild, reset m_NumUpdatesSinceRebuild to 0
   ├── Vertex dirty || Skin dirty || Transform dirty
   │   ├── m_NumUpdatesSinceRebuild >= MAX_BLAS_UPDATES_BEFORE_MAINTENANCE
   │   │   ├── SceneGraph::TryMaintenanceRebuild() == true  → maintenance rebuild, reset counter
   │   │   └──                                              → fallback update (retry next frame)
   │   └──                                                 → increment counter, normal update
   └── no relevant dirty flags → return (no-op)
4. MakeBLASDesc(!rebuild)  →  AllowUpdate vs PerformUpdate flag on the descriptor
5. BuildBottomLevelAccelStruct()
6. Reset all consumed dirty flags
7. Set m_LastBLASUpdate = frameIndex
```

### Key Behaviors

- **Natural rebuilds** (Visibility/Mesh dirty, initial create) always happen immediately and reset the counter.
- **Maintenance rebuilds** are gated by the per-frame limit to avoid spikes. If the limit is hit, the model does a normal update instead and will try a maintenance rebuild on the next frame it has dirty update flags.
- **The counter does not keep climbing** when past the threshold — it stays at the threshold value until a rebuild (of any kind) resets it. This avoids unbounded counter growth and unnecessary comparisons.
- At 60fps with the default threshold of 256, dynamically-updating models get a fresh rebuild approximately every 4.3 seconds.
