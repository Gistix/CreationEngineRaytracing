#pragma once

namespace WaterFlags
{
	enum WaterFlag
	{
		kDisplacement = 0x1,
		kLod = 0x2,
		kDepth = 0x4,
		kActorInWater = 0x8,
		kActorMovingInWater = 0x10,
		kUnderwater = 0x20,
		kUseReflections = 0x40,
		kRefractions = 0x80,
		kVertexUV = 0x100,
		kVertexAlphaDepth = 0x200,
		kProcedural = 0x400,
		kFog = 0x800,
		kUpdateConstants = 0x1000,
		kCubemap = 0x2000,
		kUseCubemapReflections = 0x4000,
		kEnableFlowmap = 0x8000,
		kBlendNormals = 0x10000
	};
};

DEFINE_ENUM_FLAG_OPERATORS(WaterFlags::WaterFlag);