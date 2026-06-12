#pragma once

#include "RE/N/NiObject.h"

namespace RE
{
	class BSGeometrySegmentSharedData;

	class __declspec(novtable) BSGeometrySegmentData :
		public NiObject
	{
	public:
		static constexpr auto RTTI{ RTTI::BSGeometrySegmentData };
		static constexpr auto VTABLE{ VTABLE::BSGeometrySegmentData };
		static constexpr auto Ni_RTTI{ Ni_RTTI::BSGeometrySegmentData };

		BSGeometrySegmentData() { REX::EMPLACE_VTABLE(this); }
		virtual ~BSGeometrySegmentData() = default;  // NOLINT(modernize-use-override) 00

		struct Segment
		{
			std::uint32_t startIndex;		// 00
			std::uint32_t numPrimitives;	// 04
			std::uint32_t parentArrayIndex;	// 08
			std::uint32_t childCount;		// 0C
			std::uint8_t  disabledCount;	// 10
			std::uint8_t  pad11;			// 11
			std::uint16_t pad12;			// 12
		};
		static_assert(sizeof(Segment) == 0x14);

		struct DrawData
		{
			std::uint32_t startIndex;	  // 00
			std::uint32_t numPrimitives;  // 04
		};
		static_assert(sizeof(DrawData) == 0x08);

		BSGeometrySegmentSharedData* sharedData;			// 10
		Segment*					 segments;				// 18
		DrawData*					 segmentDrawData;		// 20
		std::uint32_t				 numDraws;				// 28
		std::uint32_t				 numSegments;			// 2C
		std::uint32_t				 totalNumSegments;		// 30
		std::uint32_t				 totalNumPrimitives;	// 34
		std::uint32_t				 segToZeroMap;			// 38
		bool						 segmentsChanged;		// 3C
		bool						 ignoreSegments;		// 3D
	};
	static_assert(sizeof(BSGeometrySegmentData) == 0x40);
}
