#include "Core/Material.h"

#include "Scene.h"
#include "Renderer.h"

MaterialData Material::GetData(const float3 externalEmittance, const float4* waterTexScroll) const
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

	auto vector0 = Vectors[0];
	auto vector1 = Vectors[1];
	auto vector2 = Vectors[2];

	if (shaderType == RE::BSShader::Type::Water)
	{
		auto* scene = Scene::GetSingleton();

		float frameIndex = static_cast<float>(Renderer::GetSingleton()->GetFrameIndex()) * 0.01f;

		// NormalsScroll0
		vector0.x *= frameIndex * waterTexScroll[0].x;
		vector0.y *= frameIndex * waterTexScroll[0].y;

		vector0.z *= frameIndex * waterTexScroll[1].x;
		vector0.w *= frameIndex * waterTexScroll[1].y;

		// NormalsScroll1
		vector1.x *= frameIndex * waterTexScroll[2].x;
		vector1.y *= frameIndex * waterTexScroll[2].y;

		// ObjectUV
		vector2.y = static_cast<float>(*scene->g_FlowMapSize);
		vector2.z = scene->g_DisplacementMeshFlowCellOffset->x, 
		vector2.w = scene->g_DisplacementMeshFlowCellOffset->y;
	}

	return MaterialData(
		TexCoordOffsetScale[0], TexCoordOffsetScale[1],
		Colors[0], color1, Colors[2],
		alphaThreshold,
		Scalars[0], Scalars[1], Scalars[2],
		vector0, vector1, vector2, Vectors[3],
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
		static_cast<uint32_t>(GetShaderFlags()));
}