#pragma once

#include "Interop/Material/MaterialBaseData.hlsli"

class MaterialManager;

struct MaterialBase
{
	using Data = MaterialBaseData;

	enum Type : uint16_t
	{
		Lighting = 0,
		Effect = 1,
		Grass = 2,
		Water = 3,
		BloodSplatter = 4,
		DistantTree = 5,
		Particle = 6,
		TruePBR = 7
	};

	MaterialBase() = default;

	MaterialBase(RE::BSShaderMaterial* shaderMaterial, uint64_t offset);

	~MaterialBase();

	void SetManager(const eastl::shared_ptr<MaterialManager>& managerPtr);

	virtual void UpdateData(RE::BSShaderMaterial* shaderMaterial);

	virtual void UpdateTextures(RE::BSShaderMaterial* shaderMaterial);

	virtual Data* GetData() { return m_Data.get(); }

	virtual size_t GetDataSize() { return sizeof(Data); }

	uint64_t GetOffset() const { return m_Offset; }

	// Material has to be aligned to 4 bytes by design, so we compress the offset to send as a uint32_t
	uint32_t GetOffsetComp() const { return static_cast<uint32_t>(m_Offset / 4); }

	uint32_t GetHashKey() const { return m_HashKey; }

	void Update(RE::BSShaderMaterial* shaderMaterial);
protected:
	eastl::weak_ptr<MaterialManager> m_Manager;

	// Material buffer offset
	uint64_t m_Offset = 0;

	// Material data sent to the GPU
	eastl::unique_ptr<Data> m_Data;

	uint32_t m_HashKey = std::numeric_limits<uint32_t>::max();
};