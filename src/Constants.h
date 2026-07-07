#pragma once

namespace Constants
{
	namespace Material
	{
		static constexpr uint32_t EMISSIVE_COLOR = 1;

		static constexpr uint32_t NORMALMAP_TEXTURE = 1;
	}

	static constexpr uint32_t MAX_CB_VERSIONS = 16;

	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

	static constexpr uint32_t PLAYER_REFR_FORMID = 0x00000014;

	static constexpr uint32_t LIGHTS_MAX = 256;
	static constexpr uint32_t INSTANCE_LIGHTS_MAX = 32;

	static constexpr uint32_t NUM_MESHES_MIN = 1024;
	static constexpr uint32_t NUM_MESHES_MAX = 16 * 1024;

	static constexpr uint32_t NUM_MATERIALS_MIN = 1024;
	static constexpr uint32_t NUM_MATERIALS_THRESHOLD = 256;
	static constexpr uint32_t NUM_MATERIALS_STEP = 512;

	static constexpr uint32_t TLAS_INSTANCES_MIN = 2048;
	static constexpr uint32_t TLAS_INSTANCES_THRESHOLD = 256;
	static constexpr uint32_t TLAS_INSTANCES_STEP = 512;

	static constexpr uint32_t LIGHT_TLAS_INSTANCES_MIN = 64;
	static constexpr uint32_t LIGHT_TLAS_INSTANCES_THRESHOLD = 16;
	static constexpr uint32_t LIGHT_TLAS_INSTANCES_STEP = 32;

	static constexpr uint32_t NUM_INSTANCES_MAX = 256 * 1024;

	static constexpr uint32_t NUM_TEXTURES_MIN = 512;
	static constexpr uint32_t NUM_TEXTURES_MAX = 8 * 1024;

	static constexpr uint32_t NUM_CUBEMAPS_MAX = 256;

	static constexpr uint32_t INVALID_FRAME_ID = UINT32_MAX;

	static constexpr uint64_t INVALID_FRAME_INDEX = UINT64_MAX;

	static constexpr uint32_t MAX_BLAS_UPDATES_BEFORE_MAINTENANCE = 256;

	static constexpr uint32_t MAX_BLAS_MAINTENANCE_REBUILDS_PER_FRAME = 8;

	static constexpr uint32_t OMM_SUBDIV_LEVEL = 3;

	static constexpr float WATER_ABSORPTION_REFERENCE_DEPTH = 600.0f;

	namespace ExtraData
	{
		static constexpr auto LandLOD = "CERT::LandLOD";
	}

	static constexpr uint32_t PT_DISPATCH_THREADS = 8;

	static constexpr uint32_t GI_DISPATCH_THREADS = 16;

	namespace rtti
	{
		static REL::Relocation<const RE::NiRTTI*> NiBillboardNode{ NiRTTI(NiBillboardNode) };
		static REL::Relocation<const RE::NiRTTI*> BSOrderedNode{ NiRTTI(BSOrderedNode) };
		static REL::Relocation<const RE::NiRTTI*> BSDistantTreeShaderProperty{ NiRTTI(BSDistantTreeShaderProperty) };
		static REL::Relocation<const RE::NiRTTI*> BSGrassShaderProperty{ NiRTTI(BSGrassShaderProperty) };
		static REL::Relocation<const RE::NiRTTI*> BSDismemberSkinInstance{ NiRTTI(BSDismemberSkinInstance) };
		static REL::Relocation<const RE::NiRTTI*> ShadowSceneNode{ NiRTTI(ShadowSceneNode) };
		static REL::Relocation<const RE::NiRTTI*> BSFadeNode{ NiRTTI(BSFadeNode) };
	}

	static constexpr float3x4 kIdentityTransform = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f
	};
}