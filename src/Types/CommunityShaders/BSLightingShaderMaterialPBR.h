#pragma once

struct GlintParameters
{
	bool enabled = false;
	float screenSpaceScale = 1.5f;
	float logMicrofacetDensity = 40.f;
	float microfacetRoughness = .015f;
	float densityRandomization = 2.f;
};

enum class PBRFlags : uint32_t
{
	Subsurface = 1 << 0,
	TwoLayer = 1 << 1,
	ColoredCoat = 1 << 2,
	InterlayerParallax = 1 << 3,
	CoatNormal = 1 << 4,
	Fuzz = 1 << 5,
	HairMarschner = 1 << 6,
};

enum class PBRShaderFlags : uint32_t
{
	HasEmissive = 1 << 0,
	HasDisplacement = 1 << 1,
	HasFeaturesTexture0 = 1 << 2,
	HasFeaturesTexture1 = 1 << 3,
	Subsurface = 1 << 4,
	TwoLayer = 1 << 5,
	ColoredCoat = 1 << 6,
	InterlayerParallax = 1 << 7,
	CoatNormal = 1 << 8,
	Fuzz = 1 << 9,
	HairMarschner = 1 << 10,
	Glint = 1 << 11,
	ProjectedGlint = 1 << 12,
};

class BSLightingShaderMaterialPBR : public RE::BSLightingShaderMaterialBase
{
public:
	struct MaterialExtensions
	{
		void* textureSetData = nullptr;
		void* materialObjectData = nullptr;
	};

	inline static constexpr auto FEATURE = static_cast<RE::BSShaderMaterial::Feature>(32);

	inline static constexpr auto RmaosTexture = static_cast<RE::BSTextureSet::Texture>(5);
	inline static constexpr auto EmissiveTexture = static_cast<RE::BSTextureSet::Texture>(2);
	inline static constexpr auto DisplacementTexture = static_cast<RE::BSTextureSet::Texture>(3);
	inline static constexpr auto FeaturesTexture0 = static_cast<RE::BSTextureSet::Texture>(7);
	inline static constexpr auto FeaturesTexture1 = static_cast<RE::BSTextureSet::Texture>(6);

	float GetRoughnessScale() const
	{
		return specularColorScale;
	}

	float GetSpecularLevel() const
	{
		return specularPower;
	}

	float GetDisplacementScale() const
	{
		return rimLightPower;
	}

	const RE::NiColor& GetSubsurfaceColor() const
	{
		return specularColor;
	}

	float GetSubsurfaceOpacity() const
	{
		return subSurfaceLightRolloff;
	}

	const GlintParameters& GetProjectedMaterialGlintParameters() const
	{
		return projectedMaterialGlintParameters;
	}

	const GlintParameters& GetGlintParameters() const
	{
		return glintParameters;
	}

	inline static std::unordered_map<BSLightingShaderMaterialPBR*, MaterialExtensions> All;

	// members
	RE::BSShaderMaterial::Feature loadedWithFeature = RE::BSShaderMaterial::Feature::kDefault;

	stl::enumeration<PBRFlags> pbrFlags;

	float coatRoughness = 1.f;
	float coatSpecularLevel = 0.04f;

	RE::NiColor fuzzColor;
	float fuzzWeight = 0.f;

	GlintParameters glintParameters;

	// Roughness in r, metallic in g, AO in b, nonmetal reflectance in a
	RE::NiPointer<RE::NiSourceTexture> rmaosTexture;

	// Emission color in rgb
	RE::NiPointer<RE::NiSourceTexture> emissiveTexture;

	// Displacement in r
	RE::NiPointer<RE::NiSourceTexture> displacementTexture;

	// Subsurface map (subsurface color in rgb, thickness in a) / Coat map (coat color in rgb, coat strength in a)
	RE::NiPointer<RE::NiSourceTexture> featuresTexture0;

	// Fuzz map (fuzz color in rgb, fuzz weight in a) / Coat normal map (coat normal in rgb, coat roughness in a)
	RE::NiPointer<RE::NiSourceTexture> featuresTexture1;

	std::array<float, 3> projectedMaterialBaseColorScale = { 1.f, 1.f, 1.f };
	float projectedMaterialRoughness = 1.f;
	float projectedMaterialSpecularLevel = 0.04f;
	GlintParameters projectedMaterialGlintParameters;
	std::string inputFilePath = "";
};
