#pragma once

#include "PCH.h"

namespace RE
{
	class BSSkinnedDecalTriShape : public BSTriShape {};
	class BSLODTriShape : public BSTriShape {};
	class BSSegmentedTriShape : public BSTriShape {};
	class BSMeshLODTriShape : public BSTriShape {};
	class BSLODMultiIndexTriShape : public BSTriShape {};
	class BSSubIndexLandTriShape : public BSTriShape {};

	class NiBinaryExtraData : public NiExtraData
	{
	public:
		inline static constexpr auto RTTI{ RTTI_NiBinaryExtraData };
		inline static constexpr auto VTABLE{ VTABLE_NiBinaryExtraData };
		inline static constexpr auto Ni_RTTI{ NiRTTI_NiBinaryExtraData };

		~NiBinaryExtraData() override = default;

		void*         value{ nullptr };  // 0x18
		std::uint32_t size{ 0 };         // 0x20
	};
	static_assert(sizeof(NiBinaryExtraData) == 0x28);
}