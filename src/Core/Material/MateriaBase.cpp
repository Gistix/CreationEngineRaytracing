#include "Core/Material/MaterialBase.h"

#include "Util.h"

MaterialBase::MaterialBase(RE::BSShaderMaterial* shaderMaterial, uint64_t offset) 
{
	m_Offset = offset;

	m_Data = eastl::make_unique<Data>();

	Initialize(m_Data.get(), shaderMaterial);
}

void MaterialBase::Initialize(Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	data->Type = Type::Lighting;
	data->Feature = static_cast<uint16_t>(shaderMaterial->GetFeature());

	data->TexCoordOffset = Util::Math::Float2(shaderMaterial->texCoordOffset[0]);
	data->TexCoordScale = Util::Math::Float2(shaderMaterial->texCoordScale[0]);
}

void MaterialBase::UpdateTextures([[ maybe_unused ]] RE::BSShaderMaterial* shaderMaterial)
{

}