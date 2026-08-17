#pragma once

#include "RE/N/NiRefObject.h"
#include "RE/N/NiPointer.h"

namespace RE
{
	class NiLight;
	class NiAVObject;

	class BSLight : public NiRefObject
	{
	public:
		~BSLight() override = default;  // 00
		void DeleteThis() override {}  // 01
		virtual void SetLight(NiLight*) {}                 // 02
		virtual bool IsShadowLight() const { return false; } // 03
		virtual void GetProjection(uint32_t, DirectX::XMFLOAT4X4A&) const {}  // 04

		// members
		float                  lodDimmer;       // 10
		std::uint8_t           pad14[0xA4];     // 14
		NiPointer<NiLight>     light;           // B8
		std::uint8_t           padC0[0xD0];     // C0
	};
	static_assert(offsetof(BSLight, lodDimmer) == 0x10);
	static_assert(offsetof(BSLight, light) == 0xB8);

	class BSShadowLight : public BSLight
	{
	public:
		bool IsShadowLight() const override { return true; }

		struct RuntimeData
		{
			std::uint32_t maskIndex;  // 00
		};

		~BSShadowLight() override = default;

		[[nodiscard]] RuntimeData& GetRuntimeData()
		{
			return *reinterpret_cast<RuntimeData*>(reinterpret_cast<std::uintptr_t>(this) + 0x1B0);
		}

		[[nodiscard]] const RuntimeData& GetRuntimeData() const
		{
			return *reinterpret_cast<const RuntimeData*>(reinterpret_cast<std::uintptr_t>(this) + 0x1B0);
		}
	};
}
