#pragma once

namespace Constants
{
	static constexpr uint32_t MAX_CB_VERSIONS = 16;

	static constexpr uint32_t PLAYER_REFR_FORMID = 0x00000014;

	static constexpr uint32_t MATERIAL_NORMALMAP_ID = 1;

	static constexpr uint32_t NUM_LIGHTS_MAX = 255;

	static constexpr uint32_t NUM_MESHES_MIN = 1024;
	static constexpr uint32_t NUM_MESHES_MAX = 8 * 1024;

	static constexpr uint32_t NUM_INSTANCES_MIN = 1024;
	static constexpr uint32_t NUM_INSTANCES_THRESHOLD = 256;
	static constexpr uint32_t NUM_INSTANCES_STEP = 512;
	static constexpr uint32_t NUM_INSTANCES_MAX = 8 * 1024;

	static constexpr uint32_t NUM_TEXTURES_MIN = 512;
	static constexpr uint32_t NUM_TEXTURES_MAX = 8 * 1024;
}