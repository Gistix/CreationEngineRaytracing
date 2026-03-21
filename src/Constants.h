#pragma once

namespace Constants
{
	namespace Material
	{
		static constexpr uint32_t EMISSIVE_COLOR = 1;

		static constexpr uint32_t NORMALMAP_TEXTURE = 1;
	}

	static constexpr uint32_t MAX_CB_VERSIONS = 16;

	static constexpr uint32_t PLAYER_REFR_FORMID = 0x00000014;

	static constexpr uint32_t LIGHTS_MAX = 256;
	static constexpr uint32_t INSTANCE_LIGHTS_MAX = 32;

	static constexpr uint32_t NUM_MESHES_MIN = 1024;
	static constexpr uint32_t NUM_MESHES_MAX = 8 * 1024;

	static constexpr uint32_t TLAS_INSTANCES_MIN = 2048;
	static constexpr uint32_t TLAS_INSTANCES_THRESHOLD = 256;
	static constexpr uint32_t TLAS_INSTANCES_STEP = 512;

	static constexpr uint32_t LIGHT_TLAS_INSTANCES_MIN = 64;
	static constexpr uint32_t LIGHT_TLAS_INSTANCES_THRESHOLD = 16;
	static constexpr uint32_t LIGHT_TLAS_INSTANCES_STEP = 32;

	static constexpr uint32_t NUM_INSTANCES_MAX = 8 * 1024;

	static constexpr uint32_t NUM_TEXTURES_MIN = 512;
	static constexpr uint32_t NUM_TEXTURES_MAX = 8 * 1024;

	static constexpr uint32_t INVALID_FRAME_ID = UINT32_MAX;

	static constexpr uint32_t OMM_SUBDIV_LEVEL = 3;
}