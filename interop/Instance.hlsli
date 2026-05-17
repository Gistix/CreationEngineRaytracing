#ifndef INSTANCE_HLSL
#define INSTANCE_HLSL

#include "Interop.h"

struct InstanceLightData
{
	uint Count;
	uint Data[8];

    uint GetGroup(uint index)
    {
        return index >> 2;
    }

    uint GetOffset(uint index)
    {
        return (index & 3) << 3;
    }

    uint GetID(uint index)
    {
        uint group = GetGroup(index);
        uint offset = GetOffset(index);

        return (Data[group] >> offset) & 0xFFu;
    }

#ifdef __cplusplus
	InstanceLightData() = default;

	InstanceLightData(uint8_t* ids, uint8_t numLights)
	{
		Count = numLights;

		for (uint8_t i = 0; i < numLights; ++i) {
			SetID(i, ids[i]);
		}
	}

	void SetID(uint index, uint val)
	{
		uint group = GetGroup(index);
		uint offset = GetOffset(index);
		uint mask = ~(0xFFu << offset);
		Data[group] = (Data[group] & mask) | ((val & 0xFFu) << offset);
	}
#endif
};

INTEROP_DATA_STRUCT(Instance, 4)
{
	INTEROP_ROW_MAJOR(float3x4) Transform;
	INTEROP_ROW_MAJOR(float3x4) PrevTransform;
    InstanceLightData LightData;
	uint FirstGeometryID;
    float Alpha;
};
VALIDATE_ALIGNMENT(InstanceData, 4);

#endif