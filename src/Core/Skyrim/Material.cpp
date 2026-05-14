#if defined(SKYRIM)
#include "Core/Skyrim/Material.h"

#include "Scene.h"
#include "Renderer.h"

void Material::UpdateWaterMaterial(RE::BSShaderProperty* shaderProperty)
{
	auto* scene = Scene::GetSingleton();

	auto* bsWaterProperty = reinterpret_cast<RE::BSWaterShaderProperty*>(shaderProperty);
	if (!bsWaterProperty)
		return;

	auto* bsWaterMaterial = reinterpret_cast<RE::BSWaterShaderMaterial*>(bsWaterProperty->material);
	if (!bsWaterMaterial)
		return;

	int32_t flowMapSize = *scene->g_FlowMapSize;

	// ObjectUV
	if (waterShaderFlags.all(WaterShaderFlags::kVertexUV)) {
		Vectors[2].y = 1.0f;
	}
	else if (waterShaderFlags.all(WaterShaderFlags::kEnableFlowmap)) {
		Vectors[2].y = static_cast<float>(flowMapSize);
		Vectors[2].z = scene->g_DisplacementMeshFlowCellOffset->x,
		Vectors[2].w = 1.0f - scene->g_DisplacementMeshFlowCellOffset->y;
	}
	else {
		Vectors[2].y = 0.0f;
	}


	if (waterShaderFlags.all(WaterShaderFlags::kEnableFlowmap)) {
		// CellTexCoordOffset 
		Vectors[3] = {
			static_cast<float>(bsWaterProperty->flowX),
			static_cast<float>(flowMapSize - bsWaterProperty->flowY - 1),
			static_cast<float>(bsWaterProperty->cellX),
			static_cast<float>(-bsWaterProperty->cellY)
		};

		if (scene->g_FlowScroll)
			Colors[2].w = *scene->g_FlowScroll;

		auto* defaultNormal = RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureNormalMap.get();

		if (bsWaterMaterial->normalTexture4.get() && bsWaterMaterial->normalTexture4.get() != defaultNormal) {
			auto& normalTexture = Renderer::GetSingleton()->GetNormalTextureIndex();
			Textures[3] = Mesh::GetTexture(bsWaterMaterial->normalTexture4, normalTexture);
		}
	}
	else {
		// NormalsScroll0
		Vectors[0].x = bsWaterMaterial->normalScroll1.x;
		Vectors[0].y = bsWaterMaterial->normalScroll1.y;

		Vectors[0].z = bsWaterMaterial->normalScroll2.x;
		Vectors[0].w = bsWaterMaterial->normalScroll2.y;

		// NormalsScroll1
		Vectors[1].x = bsWaterMaterial->normalScroll3.x;
		Vectors[1].y = bsWaterMaterial->normalScroll3.y;
	}
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