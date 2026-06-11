#if defined(FALLOUT4)
#include "Core/Fallout4/Material.h"

#include "Scene.h"
#include "Renderer.h"

void Material::UpdateWaterMaterial([[maybe_unused]] RE::BSShaderProperty* shaderProperty)
{
}

void Material::Update([[maybe_unused]] RE::BSShaderProperty* shaderProperty)
{
}

void Material::UpdateData([[maybe_unused]] nvrhi::ICommandList* commandList, [[maybe_unused]] const float3& externalEmittance)
{
}

Material::Material([[maybe_unused]] const eastl::string& name, [[maybe_unused]] const GeometryRuntimeData& runtimeData, [[maybe_unused]] RE::FormID formID)
{
}

void Material::CreateBuffer([[maybe_unused]] const eastl::string& name, [[maybe_unused]] DescriptorIndex descriptorIndex)
{
}
#endif
