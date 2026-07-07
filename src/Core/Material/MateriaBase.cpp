#include "Core/Material/MaterialBase.h"
#include "Core/MaterialManager.h"
#include "Util.h"

MaterialBase::MaterialBase(RE::BSShaderMaterial* shaderMaterial, uint64_t offset)
{
	m_Offset = offset;
	m_HashKey = shaderMaterial->hashKey;

	m_Data = eastl::make_unique<Data>();

	UpdateData(shaderMaterial);
}

MaterialBase::~MaterialBase()
{
	auto managerPtr = m_Manager.lock();
	if (!managerPtr) {
		logger::error("Missing material manager during material destruction");
		return;
	}

	managerPtr->Release(m_Offset);
}

void MaterialBase::SetManager(const eastl::shared_ptr<MaterialManager>& managerPtr)
{
	m_Manager = managerPtr;
}

void MaterialBase::UpdateData(RE::BSShaderMaterial* shaderMaterial)
{
	auto data = m_Data.get();

	data->Type = Type::Lighting;
	data->Feature = static_cast<uint16_t>(shaderMaterial->GetFeature());

	data->TexCoordOffset = Util::Math::Float2(shaderMaterial->texCoordOffset[0]);
	data->TexCoordScale = Util::Math::Float2(shaderMaterial->texCoordScale[0]);
}

void MaterialBase::UpdateTextures([[ maybe_unused ]] RE::BSShaderMaterial* shaderMaterial)
{

}

void MaterialBase::Update(RE::BSShaderMaterial* shaderMaterial)
{
	UpdateData(shaderMaterial);

	auto manager = m_Manager.lock();
	manager->Update(this);
}