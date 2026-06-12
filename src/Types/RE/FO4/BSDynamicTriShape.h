#pragma once

#include "RE/B/BSTriShape.h"
#include "REX/W32/BASE.h"

namespace RE
{
	class BSGeometrySegmentData;

	class __declspec(novtable) BSDynamicTriShape :
		public BSTriShape
	{
	public:
		static constexpr auto RTTI{ RTTI::BSDynamicTriShape };
		static constexpr auto VTABLE{ VTABLE::BSDynamicTriShape };
		static constexpr auto Ni_RTTI{ Ni_RTTI::BSDynamicTriShape };

		BSDynamicTriShape() { REX::EMPLACE_VTABLE(this); }
		virtual ~BSDynamicTriShape() = default;  // NOLINT(modernize-use-override) 00

		// override (BSTriShape)
		const NiRTTI*      GetRTTI() const override;                           // 02
		BSDynamicTriShape* IsDynamicTriShape() override;                       // 0C
		NiObject*          CreateClone(NiCloningProcess& a_cloning) override;  // 17
		void               LoadBinary(NiStream& a_stream) override;            // 18
		void               LinkObject(NiStream& a_stream) override;            // 19
		bool               RegisterStreamables(NiStream& a_stream) override;   // 1A
		void               SaveBinary(NiStream& a_stream) override;            // 1B
		bool               IsEqual(NiObject* a_object) override;               // 1C

		// override (BSGeometry)
		BSGeometrySegmentData* GetSegmentData() override;        // 3B
		void                   SetSegmentData(BSGeometrySegmentData* a_data) override;  // 3C

		static NiObject* CreateObject();

		// add
		void*       LockDynamicData();
		const void* LockDynamicDataForRead();
		void        UnlockDynamicData();
		void        SetDynamicDataSize(std::uint32_t a_size);
		void        UpdateDynamicPositions(NiPoint3* a_positions, bool a_updateBounds);
		void        UpdateDynamicPositionsEyeData(NiPoint3* a_positions, float* a_eyeData, bool a_updateBounds);

		// members
		std::uint32_t                        dynamicDataSize;                 // 170
		std::uint32_t                        unk174;                          // 174
		std::uint64_t                        lock;                            // 178
		void*                                dynamicData;                     // 180
		NiPointer<BSGeometrySegmentData>     segmentData;                     // 188
		void*                                unk190;                          // 190
		std::uint32_t                        unk198;                          // 198
		std::uint32_t                        pad19C;						  // 19C
		void*                                unk1A0;                          // 1A0
		std::uint32_t                        unk1A8;                          // 1A8
		std::uint32_t                        pad1AC;						  // 1AC
	};
	static_assert(sizeof(BSDynamicTriShape) == 0x1B0);
}
