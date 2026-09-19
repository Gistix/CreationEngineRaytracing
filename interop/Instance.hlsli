#ifndef INSTANCE_HLSL
#define INSTANCE_HLSL

#include "Interop.h"

// Per-instance light list range, written by the GPU InstanceLightCulling pass.
// LightOffset indexes into the global InstanceLightList buffer; both fields are
// 16-bit to keep the per-instance payload compact.
struct InstanceLightData
{
	uint16_t LightOffset;
	uint16_t LightCount;
};

INTEROP_DATA_STRUCT(Instance, 4)
{
	INTEROP_ROW_MAJOR(float3x4) Transform;
	INTEROP_ROW_MAJOR(float3x4) PrevTransform;
    InstanceLightData LightData;
	uint FirstGeometryID;
    uint NumGeometry;
    float Alpha;
};
VALIDATE_TRIVIAL(InstanceData);
VALIDATE_ALIGNMENT(InstanceData, 4);

#endif // INSTANCE_HLSL
