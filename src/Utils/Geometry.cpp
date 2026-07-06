#pragma once

#include "Geometry.h"

namespace
{
	enum class BlocklistMatch
	{
		Exact,
		Contains
	};

	struct BlocklistedGeometryName
	{
		const char* name;
		BlocklistMatch match;
	};

	constexpr BlocklistedGeometryName GEOMETRY_NAME_BLOCKLIST[] = {
		// Skyrim Markers
		{ "EditorMarker", BlocklistMatch::Exact },
		{ "LRTMarker", BlocklistMatch::Exact },
		{ "AnimInteractionMarker", BlocklistMatch::Exact },
		{ "FurnitureMarker", BlocklistMatch::Exact },

		// Eye overlays/shadows
		{ "AEAC_LDD_Eye_Outer", BlocklistMatch::Exact },
		{ "AEAC_LDD_Eye_Shadow", BlocklistMatch::Exact },
		{ "FemaleEyesHuman_outer", BlocklistMatch::Contains },
		{ "FemaleEyesHuman_shadows", BlocklistMatch::Contains },
		{ "EyeShadow", BlocklistMatch::Contains },

		// SMP Collision
		{ "VirtualGround", BlocklistMatch::Contains },
		{ "colHeadBDO", BlocklistMatch::Contains },
		{ "colHFullBDO", BlocklistMatch::Contains },
		{ "colHMidBDO", BlocklistMatch::Contains },
		{ "colHMiniBDO", BlocklistMatch::Contains },
		{ "colWigMiniBDO", BlocklistMatch::Contains }
	};

	char ToLower(char c)
	{
		return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}

	bool EqualsIgnoreCase(const char* value, const char* pattern)
	{
		while (*value && *pattern) {
			if (ToLower(*value) != ToLower(*pattern))
				return false;

			++value;
			++pattern;
		}

		return *value == '\0' && *pattern == '\0';
	}

	bool ContainsIgnoreCase(const char* value, const char* pattern)
	{
		const auto patternLength = std::strlen(pattern);

		if (patternLength == 0)
			return true;

		for (auto* valueIt = value; *valueIt; ++valueIt) {
			size_t patternIndex = 0;
			while (patternIndex < patternLength &&
				valueIt[patternIndex] &&
				ToLower(valueIt[patternIndex]) == ToLower(pattern[patternIndex])) {
				++patternIndex;
			}

			if (patternIndex == patternLength)
				return true;
		}

		return false;
	}
}

namespace Util
{
	namespace Geometry
	{
		std::uint16_t GetSkyrimVertexSize(RE::BSGraphics::Vertex::Flags flags)
		{
			using RE::BSGraphics::Vertex;

			std::uint16_t vertexSize = 0;

			if (flags & Vertex::VF_VERTEX) {
				vertexSize += sizeof(float) * 4;
			}
			if (flags & Vertex::VF_UV) {
				vertexSize += sizeof(std::uint16_t) * 2;
			}
			if (flags & Vertex::VF_UV_2) {
				vertexSize += sizeof(std::uint16_t) * 2;
			}
			if (flags & Vertex::VF_NORMAL) {
				vertexSize += sizeof(std::uint16_t) * 2;
				if (flags & Vertex::VF_TANGENT) {
					vertexSize += sizeof(std::uint16_t) * 2;
				}
			}
			if (flags & Vertex::VF_COLORS) {
				vertexSize += sizeof(std::uint8_t) * 4;
			}
			if (flags & Vertex::VF_SKINNED) {
				vertexSize += sizeof(std::uint16_t) * 4 + sizeof(std::uint8_t) * 4;
			}
			if (flags & Vertex::VF_EYEDATA) {
				vertexSize += sizeof(float);
			}
			if (flags & Vertex::VF_LANDDATA) {
				vertexSize += sizeof(uint32_t) * 2;
			}

			return vertexSize;
		}

		uint16_t GetStoredVertexSize(RE::BSGraphics::VertexDesc vertexDesc)
		{
			const auto vertexDescUInt = *reinterpret_cast<uint64_t*>(&vertexDesc);
			return ((vertexDescUInt & 0xF) << 2);
		}

		bool IsDismemberSkinInstance(RE::NiSkinInstance* skinInstance)
		{
#if defined(SKYRIM)
			if (!skinInstance)
				return false;

			return skinInstance->GetRTTI() == Constants::rtti::BSDismemberSkinInstance.get();
#else
			(void)skinInstance;
			return false;
#endif
		}

		void GetDismemberPartitionVisibility(RE::NiSkinInstance* skinInstance, eastl::vector<bool>& outVisibility)
		{
			outVisibility.clear();

#if defined(SKYRIM)
			if (!skinInstance)
				return;

			auto& runtime = reinterpret_cast<RE::BSDismemberSkinInstance*>(skinInstance)->GetRuntimeData();
			outVisibility.resize(runtime.numPartitions);

			for (int32_t i = 0; i < runtime.numPartitions; ++i)
				outVisibility[i] = runtime.partitions[i].editorVisible;
#else
			(void)skinInstance;
#endif
		}

		bool IsBlocklisted(const char* name)
		{
			if (!name)
				return false;

			for (const auto& entry : GEOMETRY_NAME_BLOCKLIST) {
				switch (entry.match) {
				case BlocklistMatch::Exact:
					if (EqualsIgnoreCase(name, entry.name))
						return true;
					break;
				case BlocklistMatch::Contains:
					if (ContainsIgnoreCase(name, entry.name))
						return true;
					break;
				}
			}

			return false;
		}
	}
}
