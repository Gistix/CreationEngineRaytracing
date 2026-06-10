#pragma once

#include "RE/N/NiObject.h"
#include "REX/W32/BASE.h"

namespace RE
{
	struct SegmentData
	{
		std::uint32_t index;        // 00
		std::uint32_t unkTriCount;  // 04
		std::uint8_t  unkFlags;     // 08
		std::uint32_t numTris;      // 0C
		std::uint8_t  flags;        // 10
	};

	class __declspec(novtable) BSSubIndexTriShape :
		public BSTriShape
	{
	public:
		static constexpr auto RTTI{ RTTI::BSSubIndexTriShape };
		static constexpr auto VTABLE{ VTABLE::BSSubIndexTriShape };
		static constexpr auto Ni_RTTI{ Ni_RTTI::BSSubIndexTriShape };

		BSSubIndexTriShape() { REX::EMPLACE_VTABLE(this); }
		virtual ~BSSubIndexTriShape() = default;  // NOLINT(modernize-use-override) 00

		// override (BSTriShape)
		const NiRTTI* GetRTTI() const override;                           // 02
		NiObject* CreateClone(NiCloningProcess& a_cloning) override;	  // 17
		void          LoadBinary(NiStream& a_stream) override;            // 18
		void          LinkObject(NiStream& a_stream) override;            // 19
		bool          RegisterStreamables(NiStream& a_stream) override;   // 1A
		void          SaveBinary(NiStream& a_stream) override;            // 1B
		bool          IsEqual(NiObject* a_object) override;               // 1C

		// members
		SegmentData*  segmentData;  /* 00 */
		std::uint32_t numSegments;  /* 08 */
		std::uint32_t unkSegCount;  /* 0C */
		bool          unk170;       /* 10 */
		bool          nonSegmented; /* 14 */
	};
}