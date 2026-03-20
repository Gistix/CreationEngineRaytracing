#pragma once

#include <PCH.h>

#include "Types.h"
#include "Vertex.hlsli"
#include "Triangle.hlsli"
#include "Skinning.hlsli"
#include "Material.hlsli"
#include "Framework/DescriptorTableManager.h"
#include "Core/Texture.h"

#include "Types\CommunityShaders\BSLightingShaderMaterialPBR.h"
#include "Types\CommunityShaders\BSLightingShaderMaterialPBRLandscape.h"

struct Material
{
	static constexpr uint MAX_LAND_TEXTURES = 5u;
	static constexpr uint MAX_PBRLAND_TEXTURES = 6u;

	enum class AlphaFlags : uint8_t
	{
		None = 0,
		Blend = 1 << 0,
		Test = 1 << 1,
		Transmission = 1 << 2
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

	// We have a limited number of bits and not all types are necessary
	ShaderType GetShaderType() const
	{
		if (shaderFlags.any(RE::BSShaderProperty::EShaderPropertyFlag::kMenuScreen))
			return ShaderType::TruePBR;

		switch (shaderType) {
		case RE::BSShader::Type::Grass:
			return ShaderType::Grass;
		case RE::BSShader::Type::Water:
			return ShaderType::Water;
		case RE::BSShader::Type::BloodSplatter:
			return ShaderType::BloodSplatter;
		case RE::BSShader::Type::Effect:
			return ShaderType::Effect;
		case RE::BSShader::Type::DistantTree:
			return ShaderType::DistantTree;
		case RE::BSShader::Type::Particle:
			return ShaderType::Particle;
		default:
			return ShaderType::Lighting;
		}
	}

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
		kTreeAnim = 1 << 19
	};

	REX::EnumSet<RE::BSShaderProperty::EShaderPropertyFlag, std::uint64_t> shaderFlags;
	RE::BSShader::Type shaderType;
	RE::BSShaderMaterial::Feature Feature;
	stl::enumeration<PBRShaderFlags, uint16_t> PBRFlags;

	AlphaFlags alphaFlags = AlphaFlags::None;

	half alphaThreshold;

	eastl::array<half4, 3> Colors;
	eastl::array<half, 2> Scalars;

	eastl::array<half4, 2> TexCoordOffsetScale;

	eastl::array<Texture, 20> Textures;

	bool missingPBREmissiveColor;

	ShaderFlags GetShaderFlags() const
	{
		using EShaderPropertyFlag = RE::BSShaderProperty::EShaderPropertyFlag;

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

		return shaderFlagsLocal;
	}

	uint16_t GetTextureDescriptorIndex(uint32_t index) const
	{
		auto& texture = Textures[index];

		auto locked = texture.texture.lock();

		if (locked)
			return static_cast<uint16_t>(locked->Get());
		else
			return static_cast<uint16_t>(texture.defaultTexture->Get());
	}

	MaterialData GetData(float3 externalEmittance) const
	{
		half4 emissive = Colors[1];

		if (shaderFlags.all(RE::BSShaderProperty::EShaderPropertyFlag::kExternalEmittance)) {
			emissive.x = externalEmittance.x;
			emissive.y = externalEmittance.y;
			emissive.z = externalEmittance.z;
		}

		return MaterialData(
			TexCoordOffsetScale[0], TexCoordOffsetScale[1],
			Colors[0], emissive, Colors[2],
			alphaThreshold,
			Scalars[0], Scalars[1], Scalars[2],
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
};

DEFINE_ENUM_FLAG_OPERATORS(Material::AlphaFlags);