#pragma once

#include <PCH.h>

#include "Types.h"

#include "interop/Properties.hlsli"

// Per-mesh data derived from the BSShaderProperty + NiAlphaProperty (which the per-type
// material wrappers don't have access to). Written into MeshData so the surface shader can
// branch on the shader flags / alpha state and read the emissive color.
struct Properties
{
	using EShaderPropertyFlag = RE::BSShaderProperty::EShaderPropertyFlag;
	using Feature = RE::BSShaderMaterial::Feature;

	// Mirrors the interop ShaderFlags namespace (interop/Properties.hlsli).
	// Water flags reuse the same bits as lighting flags (kHairTint/kTwoSided/kModelSpaceNormals)
	// because the shader interprets them differently based on material Type.
	enum ShaderFlags : uint32_t
	{
		kSpecular = 1 << 0,
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

	// Water shader flags (mirrors interop/Properties.hlsli WaterShaderFlags namespace).
	// Written into the same ShaderFlags field; the shader disambiguates by material Type::Water.
	enum WaterShaderFlags : uint32_t
	{
		kActorInWater         = 1 << 0,
		kActorMovingInWater   = 1 << 1,
		kWaterVertexUV        = 1 << 2,
		kWaterEnableFlowmap   = 1 << 3,
		kWaterBlendNormals    = 1 << 4,
		kWaterDisplacement    = 1 << 5,
		kWaterVertexAlphaDepth = 1 << 6,
		kWaterDepth           = 1 << 7
	};

	enum AlphaFlags : uint16_t
	{
		None = 0,
		Blend = 1 << 0,
		Test = 1 << 1,
		Transmission = 1 << 2,
		Additive = 1 << 3
	};

	PropertiesData m_Data;

	Properties() = default;

	Properties(RE::BSTriShape* triShape, bool isEye);

	bool IsAlpha() const { return m_Data.AlphaFlags != AlphaFlags::None; }

	auto& GetData() const { return m_Data; }
private:
	static uint32_t MapShaderFlags(RE::BSShaderProperty* shaderProperty);
	static uint16_t MapWaterShaderFlags(RE::BSWaterShaderProperty* waterShaderProp);
};
