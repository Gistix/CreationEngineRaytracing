#pragma once

#include <PCH.h>

#include "Types.h"
#include "Vertex.hlsli"
#include "Triangle.hlsli"
#include "Material.hlsli"
#include "Framework/DescriptorTableManager.h"

#include "Core/Texture.h"
#include "Core/TextureManager.h"

#include "Types\CommunityShaders\BSLightingShaderMaterialPBR.h"
#include "Types\CommunityShaders\BSLightingShaderMaterialPBRLandscape.h"

#include "Types/GeometryRuntimeData.h"

struct Material
{
	using EShaderPropertyFlag = RE::BSShaderProperty::EShaderPropertyFlag;
	using Feature = RE::BSShaderMaterial::Feature;

	static constexpr uint MAX_LAND_TEXTURES = 6u;

	enum class AlphaFlags : uint8_t
	{
		None = 0,
		Blend = 1 << 0,
		Test = 1 << 1,
		Transmission = 1 << 2,
		Additive = 1 << 3
	};

	enum ShaderType : uint16_t
	{
		TruePBR = 0,
		Lighting = 1,
		Effect = 2,
		Grass = 3,
		Water = 4,
		BloodSplatter = 5,
		DistantTree = 6,
		Particle = 7
	};

	MaterialData m_MaterialData;
	MaterialData m_PrevMaterialData;

	nvrhi::BufferHandle buffer;

	Material(const eastl::string& name, const GeometryRuntimeData& runtimeData, RE::FormID formID);

	eastl::unique_ptr<Material> Clone(const eastl::string& name) const;

	void SetupLandMaterial(const RE::BSLightingShaderMaterialLandscape* landMaterial);
	void SetupLightingMaterial(RE::BSLightingShaderMaterialBase* lightingMaterial, RE::FormID formID);
	void SetupPBRLandscapeMaterial(const BSLightingShaderMaterialPBRLandscape* material);
	void SetupPBRMaterial(const BSLightingShaderMaterialPBR* material);
	void SetupEffectMaterial(const RE::BSEffectShaderMaterial* material);

	void SetupWaterProperty(RE::BSWaterShaderProperty* waterShaderProp);
	void SetupWaterMaterial(RE::BSWaterShaderMaterial* waterMaterial);

	void SetupProjectedUV(RE::BSLightingShaderProperty* lightingShaderProp);

	void CreateBuffer(const eastl::string& name, DescriptorIndex descriptorIndex);

	void UpdateWaterMaterial(RE::BSShaderProperty* shaderProperty);

	void Update(RE::BSShaderProperty* shaderProperty);

	void UpdateData(nvrhi::ICommandList* commandList, const float3& externalEmittance);

	enum ShaderFlags : uint32_t
	{
		None = 0,
		kSpecular = 1 << 0,
		kTempRefraction = 1 << 1,
		kVertexAlpha = 1 << 2,
		kGrayscaleToPaletteColor = 1 << 3,
		kGrayscaleToPaletteAlpha = 1 << 4,
		kFalloff = 1 << 5,
		kEnvMap = 1 << 6,
		kFace = 1 << 7,
		kModelSpaceNormals = 1 << 8,
		kRefraction = 1 << 9,
		kProjectedUV = 1 << 10,
		kExternalEmittance = 1 << 11,
		kVertexColors = 1 << 12,
		kMultiTextureLandscape = 1 << 13,
		kEyeReflect = 1 << 14,
		kHairTint = 1 << 15,
		kTwoSided = 1 << 16,
		kAssumeShadowmask = 1 << 17,
		kBackLighting = 1 << 18,
		kTreeAnim = 1 << 19,
		kSoftLighting = 1 << 20,
		kLODLandscape = 1 << 21,
		kLODObjects = 1 << 22,
		kHDLODObjects = 1 << 23,
		kSnow = 1 << 24
	};

	enum class WaterShaderFlags : uint32_t
	{
		kNone = 0,
		kDisplacement = 1 << 0,
		kLod = 1 << 1,
		kDepth = 1 << 2,
		kActorInWater = 1 << 3,
		kActorMovingInWater = 1 << 4,
		kUnderwater = 1 << 5,
		kUseReflections = 1 << 6,
		kRefractions = 1 << 7,
		kVertexUV = 1 << 8,
		kVertexAlphaDepth = 1 << 9,
		kProcedural = 1 << 10,
		kFog = 1 << 11,
		kUpdateConstants = 1 << 12,
		kCubemap = 1 << 13,
		kUseCubemapReflections = 1 << 14,
		kEnableFlowmap = 1 << 15,
		kBlendNormals = 1 << 16
	};

	CESEAdapter::REX::EnumSet<EShaderPropertyFlag, std::uint64_t> shaderFlags;
	CESEAdapter::REX::EnumSet<WaterShaderFlags, std::uint32_t> waterShaderFlags;
	ShaderType shaderType;
	Feature feature;
	CESEAdapter::REX::EnumSet<PBRShaderFlags, uint16_t> pbrFlags;

	AlphaFlags alphaFlags = AlphaFlags::None;

	half alphaThreshold;

	eastl::array<half4, 3> colors;
	eastl::array<half, 3> scalars;

	eastl::array<half4, 4> vectors;

	eastl::array<half4, 2> texCoordOffsetScale;

	eastl::array<Texture, 20> textures;

	uint32_t GetShaderFlags() const
	{
		if (shaderType == ShaderType::Water)
			return waterShaderFlags.underlying();

		auto shaderFlagsLocal = ShaderFlags::None;

		if (shaderFlags.any(EShaderPropertyFlag::kSpecular)) {
			shaderFlagsLocal |= ShaderFlags::kSpecular;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kTempRefraction)) {
			shaderFlagsLocal |= ShaderFlags::kTempRefraction;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kVertexAlpha)) {
			shaderFlagsLocal |= ShaderFlags::kVertexAlpha;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kGrayscaleToPaletteColor)) {
			shaderFlagsLocal |= ShaderFlags::kGrayscaleToPaletteColor;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kGrayscaleToPaletteAlpha)) {
			shaderFlagsLocal |= ShaderFlags::kGrayscaleToPaletteAlpha;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kFalloff)) {
			shaderFlagsLocal |= ShaderFlags::kFalloff;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kEnvMap)) {
			shaderFlagsLocal |= ShaderFlags::kEnvMap;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kFace)) {
			shaderFlagsLocal |= ShaderFlags::kFace;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kModelSpaceNormals)) {
			shaderFlagsLocal |= ShaderFlags::kModelSpaceNormals;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kRefraction)) {
			shaderFlagsLocal |= ShaderFlags::kRefraction;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kProjectedUV)) {
			shaderFlagsLocal |= ShaderFlags::kProjectedUV;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kExternalEmittance)) {
			shaderFlagsLocal |= ShaderFlags::kExternalEmittance;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kVertexColors)) {
			shaderFlagsLocal |= ShaderFlags::kVertexColors;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kMultiTextureLandscape)) {
			shaderFlagsLocal |= ShaderFlags::kMultiTextureLandscape;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kEyeReflect)) {
			shaderFlagsLocal |= ShaderFlags::kEyeReflect;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kHairTint)) {
			shaderFlagsLocal |= ShaderFlags::kHairTint;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kTwoSided)) {
			shaderFlagsLocal |= ShaderFlags::kTwoSided;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kAssumeShadowmask)) {
			shaderFlagsLocal |= ShaderFlags::kAssumeShadowmask;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kBackLighting)) {
			shaderFlagsLocal |= ShaderFlags::kBackLighting;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kTreeAnim)) {
			shaderFlagsLocal |= ShaderFlags::kTreeAnim;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kSoftLighting)) {
			shaderFlagsLocal |= ShaderFlags::kSoftLighting;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kLODLandscape)) {
			shaderFlagsLocal |= ShaderFlags::kLODLandscape;
		}
		
		if (shaderFlags.any(EShaderPropertyFlag::kLODObjects)) {
			shaderFlagsLocal |= ShaderFlags::kLODObjects;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kHDLODObjects)) {
			shaderFlagsLocal |= ShaderFlags::kHDLODObjects;
		}

		if (shaderFlags.any(EShaderPropertyFlag::kSnow)) {
			shaderFlagsLocal |= ShaderFlags::kSnow;
		}

		return static_cast<uint32_t>(shaderFlagsLocal);
	}

	uint16_t GetTextureDescriptorIndex(uint32_t index) const
	{
		const auto& texture = textures[index];

		auto locked = texture.texture.lock();

		if (locked)
			return static_cast<uint16_t>(locked->Get());
		else
			return static_cast<uint16_t>(texture.defaultTexture->Get());
	}

	static Texture GetTexture(const RE::NiPointer<RE::NiSourceTexture>& niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, TextureType textureType = TextureType::Standard);
private:
	MaterialData* GetData();
};

DEFINE_ENUM_FLAG_OPERATORS(Material::AlphaFlags);