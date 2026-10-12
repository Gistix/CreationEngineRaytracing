#define HLSL 1
#define RTXGI_COORDINATE_SYSTEM 3
#define RTXGI_DDGI_RESOURCE_MANAGEMENT 0
#define RTXGI_DDGI_BINDLESS_RESOURCES 0
#define RTXGI_DDGI_SHADER_REFLECTION 0
#define RTXGI_PUSH_CONSTS_TYPE 1

#if !defined(__spirv__)
#define CONSTS_REGISTER b1
#define CONSTS_SPACE space0
#endif

#if defined(__spirv__)
#define VOLUME_CONSTS_REGISTER 0
#define VOLUME_CONSTS_SPACE 0
#define RAY_DATA_REGISTER 1
#define RAY_DATA_SPACE 0
#define PROBE_DATA_REGISTER 2
#define PROBE_DATA_SPACE 0
#else
#define VOLUME_CONSTS_REGISTER t0
#define VOLUME_CONSTS_SPACE space0
#define RAY_DATA_REGISTER u0
#define RAY_DATA_SPACE space0
#define PROBE_DATA_REGISTER u1
#define PROBE_DATA_SPACE space0
#endif

#include "ddgi/ProbeRelocationCS.hlsl"

