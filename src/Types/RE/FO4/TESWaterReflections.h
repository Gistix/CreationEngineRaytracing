#pragma once

#include "RE/N/NiPlane.h"
#include "RE/N/NiRefObject.h"
#include "RE/N/NiSmartPointer.h"
#include "RE/N/NiTexture.h"

namespace RE
{
	class NiAVObject;

	class TESWaterReflections : public NiRefObject
	{
	public:
		static constexpr auto RTTI{ RTTI::TESWaterReflections };
		static constexpr auto VTABLE{ VTABLE::TESWaterReflections };

		enum class Flags : std::uint16_t
		{
			kNone = 0,
			kDirty = 1 << 0,
			kStaticCubemap = 1 << 1,
			kDynamicCubemap = 1 << 2,
			kInterior = 1 << 3,
			kSilhouette = 1 << 4,
			kLODScene = 1 << 5,
			kFullScene = 1 << 6,
			kLand = 1 << 7,
			kSky = 1 << 8,
			kExplosions = 1 << 9,
			kSelective = 1 << 10,
			kDontUpdate = 1 << 11,
			kWorldOrigin = 1 << 12
		};

		struct CubeMapSide
		{
		public:
			std::uint32_t faceIndex{ 0 };  // 00
			float         timer{ 0.0f };   // 04
		};
		static_assert(sizeof(CubeMapSide) == 0x8);

		// members
		REX::EnumSet<Flags, std::uint16_t> flags;              // 10
		std::uint16_t                      pad12;              // 12
		NiPlane                            plane;              // 14
		NiPointer<NiAVObject>              reflectionObject;   // 28
		NiPointer<NiTexture>               reflectionTexture;  // 30
		void*                              unk38;              // 38
		void*                              waterMaterial;      // 40
		float                              timer;              // 48
		std::uint32_t                      cubeMapSideIndex;   // 4C
		CubeMapSide                        cubeMapSides[6];    // 50
		std::uint8_t                       rendered;           // 80
		std::uint8_t                       pad81[7];           // 81
	};
	static_assert(sizeof(TESWaterReflections) == 0x88);
}
