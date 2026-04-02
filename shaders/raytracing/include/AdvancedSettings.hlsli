#ifndef ADVANCED_SETTINGS_HLSL
#define ADVANCED_SETTINGS_HLSL

#define DIFFUSE_MODE_LAMBERT    0
#define DIFFUSE_MODE_BURLEY     1
#define DIFFUSE_MODE_ORENNAYAR  2
#define DIFFUSE_MODE_GOTANDA    3
#define DIFFUSE_MODE_CHAN       4

#define LIGHTEVAL_MODE_DIFFUSE  0
#define LIGHTEVAL_MODE_BRDF     1

#define LIGHTING_MODE_DIFFUSE   0
#define LIGHTING_MODE_PBR       1

#ifndef DIFFUSE_MODE
#define DIFFUSE_MODE (DIFFUSE_MODE_BURLEY)
#endif

#ifndef LIGHTEVAL_MODE
#define LIGHTEVAL_MODE (LIGHTEVAL_MODE_BRDF)
#endif

#ifndef LIGHTING_MODE
#define LIGHTING_MODE (LIGHTING_MODE_PBR)
#endif

#ifndef RIS_MAX_CANDIDATES
#define RIS_MAX_CANDIDATES (4)
#endif

// Self-Intersection Avoidance method:
//   0 = Original method (CalculatePositionError + CalculateRayOffset)
//   1 = NVIDIA SIA (precise interpolation + tight error bounds from transform chain)
// 
// When enabled, position interpolation and ray offset in SurfaceMaker use the
// NVIDIA self-intersection avoidance algorithm which provides:
//   - Precise MAD-based barycentric interpolation (reduces floating-point error)
//   - Tight error bounds through the full object-to-world transform chain
//   - A computed safe offset instead of heuristic scaling
//
// Reference: https://github.com/NVIDIA/self-intersection-avoidance
#ifndef USE_SIA_INTERPOLATION
#define USE_SIA_INTERPOLATION (1)
#endif

#endif // ADVANCED_SETTINGS_HLSL