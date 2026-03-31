#if defined(FALLOUT4)
#include "Core/Fallout4/Material.h"

#include "Scene.h"
#include "Renderer.h"

void Material::UpdateWaterMaterial(RE::BSShaderProperty* shaderProperty)
{
	auto* scene = Scene::GetSingleton();

	int32_t flowMapSize = *scene->g_FlowMapSize;

	// ObjectUV
	Vectors[2].y = static_cast<float>(flowMapSize);
	Vectors[2].z = scene->g_DisplacementMeshFlowCellOffset->x,
	Vectors[2].w = 1.0f - scene->g_DisplacementMeshFlowCellOffset->y;

	auto* bsWaterProperty = reinterpret_cast<RE::BSWaterShaderProperty*>(shaderProperty);
	if (!bsWaterProperty)
		return;

	auto* bsWaterMaterial = reinterpret_cast<RE::BSWaterShaderMaterial*>(bsWaterProperty->material);
	if (!bsWaterMaterial)
		return;

	auto normalScroll1 = *reinterpret_cast<float2*>(&bsWaterMaterial->unk100);
	auto normalScroll2 = *reinterpret_cast<float2*>(&bsWaterMaterial->unk108);
	auto normalScroll3 = *reinterpret_cast<float2*>(&bsWaterMaterial->unk110);

	// NormalsScroll0
	Vectors[0].x = normalScroll1.x;
	Vectors[0].y = normalScroll1.y;

	Vectors[0].z = normalScroll2.x;
	Vectors[0].w = normalScroll2.y;

	// NormalsScroll1
	Vectors[1].x = normalScroll3.x;
	Vectors[1].y = normalScroll3.y;

	// CellTexCoordOffset 
	Vectors[3] = {
		static_cast<float>(bsWaterProperty->flowX),
		static_cast<float>(flowMapSize - bsWaterProperty->flowY - 1),
		static_cast<float>(bsWaterProperty->cellX),
		static_cast<float>(-bsWaterProperty->cellY)
	};
}

MaterialData Material::GetData(const float3 externalEmittance, RE::BSShaderProperty* shaderProperty)
{
	auto color1 = Colors[1];

	if (shaderFlags.all(RE::BSShaderProperty::EShaderPropertyFlag::kExternalEmittance)) {
		if (shaderFlags.all(RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit)) {
			color1.x *= externalEmittance.x;
			color1.y *= externalEmittance.y;
			color1.z *= externalEmittance.z;
		}
		else {
			color1.x = externalEmittance.x;
			color1.y = externalEmittance.y;
			color1.z = externalEmittance.z;
		}
	}

	if (shaderType == RE::BSShader::Type::Water)
		UpdateWaterMaterial(shaderProperty);

	return MaterialData(
		TexCoordOffsetScale[0], TexCoordOffsetScale[1],
		Colors[0], color1, Colors[2],
		alphaThreshold,
		Scalars[0], Scalars[1], Scalars[2],
		Vectors[0], Vectors[1], Vectors[2], Vectors[3],
		GetTextureDescriptorIndex(0),
		GetTextureDescriptorIndex(1),
		GetTextureDescriptorIndex(2),
		GetTextureDescriptorIndex(3),
		GetTextureDescriptorIndex(4),
		GetTextureDescriptorIndex(5),
		GetTextureDescriptorIndex(6),
		GetTextureDescriptorIndex(7),
		GetTextureDescriptorIndex(8),
		GetTextureDescriptorIndex(9),
		GetTextureDescriptorIndex(10),
		GetTextureDescriptorIndex(11),
		GetTextureDescriptorIndex(12),
		GetTextureDescriptorIndex(13),
		GetTextureDescriptorIndex(14),
		GetTextureDescriptorIndex(15),
		GetTextureDescriptorIndex(16),
		GetTextureDescriptorIndex(17),
		GetTextureDescriptorIndex(18),
		GetTextureDescriptorIndex(19),
		static_cast<uint16_t>(alphaFlags),
		GetShaderType(),
		static_cast<uint16_t>(Feature),
		PBRFlags.underlying(),
		GetShaderFlags());
}
#endif