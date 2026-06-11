#pragma once

namespace RE
{
	class NiLight;
	class NiAVObject;

	class BSLight
	{
	public:
		virtual ~BSLight() = default;  // 00
		virtual void SetLight(NiLight*) {}                 // 01
		virtual bool IsShadowLight() { return false; }     // 02
		virtual void GetProjection(uint32_t, DirectX::XMFLOAT4X4A&) const {}  // 03

		NiPointer<NiLight>     light;           // 08
		bool                   pointLight;      // 10
		std::uint8_t           pad11[7];        // 11
		float                  lodDimmer;       // 18
		std::uint8_t           pad1C[0x74];     // 1C
		void*                  shadowLightData; // 0x90
	};
	static_assert(offsetof(BSLight, light) == 0x08);
	static_assert(offsetof(BSLight, pointLight) == 0x10);
	static_assert(offsetof(BSLight, lodDimmer) == 0x18);

	class BSShadowLight : public BSLight
	{
	public:
		bool IsShadowLight() override { return true; }

		struct RuntimeData
		{
			std::uint32_t maskIndex;  // 00
		};

		~BSShadowLight() override = default;

		[[nodiscard]] RuntimeData& GetRuntimeData()
		{
			return reinterpret_cast<RuntimeData&>(m_runtimeData);
		}

	private:
		RuntimeData m_runtimeData;  // 98
	};
}
