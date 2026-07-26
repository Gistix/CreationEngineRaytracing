#pragma once

#include "RE/N/NiNode.h"
#include "RE/N/NiTArray.h"

namespace RE
{
	class NiSwitchNode : public NiNode
	{
	public:
		static constexpr auto RTTI{ RTTI::NiSwitchNode };
		static constexpr auto VTABLE{ VTABLE::NiSwitchNode };
		static constexpr auto Ni_RTTI{ Ni_RTTI::NiSwitchNode };

		NiSwitchNode() { REX::EMPLACE_VTABLE(this); }
		virtual ~NiSwitchNode() = default;

		const NiRTTI* GetRTTI() const override;
		NiSwitchNode* AsSwitchNode() { return this; }
		NiObject*     CreateClone(NiCloningProcess& a_cloning) override;
		void          LoadBinary(NiStream& a_stream) override;
		void          LinkObject(NiStream& a_stream) override;
		bool          RegisterStreamables(NiStream& a_stream) override;
		void          SaveBinary(NiStream& a_stream) override;
		bool          IsEqual(NiObject* a_object) override;

		// members
		uint16_t                    flags;       // 0x140
		uint16_t                    pad142;      // 0x142
		int32_t                     index;       // 0x144
		float                       savedTime;   // 0x148
		uint32_t                    revID;       // 0x14C
		NiTPrimitiveArray<uint32_t> childRevID;  // 0x150
	};
}
