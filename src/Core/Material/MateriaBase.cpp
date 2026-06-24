#include "Core/Material/MaterialBase.h"

#include "Util.h"

MaterialBase::MaterialBase(RE::BSShaderMaterial* shaderMaterial)
{
	m_Data = eastl::make_unique<Data>();

	Initialize(m_Data.get(), shaderMaterial);
}

void MaterialBase::Initialize(Data* data, RE::BSShaderMaterial* shaderMaterial)
{
	data->TexCoordOffset = Util::Math::Float2(shaderMaterial->texCoordOffset[0]);
	data->TexCoordScale = Util::Math::Float2(shaderMaterial->texCoordScale[0]);
}
