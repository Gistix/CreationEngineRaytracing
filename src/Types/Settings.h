#pragma once

enum class Mode
{
	GlobalIllumination,
	PathTracing
};

enum class Denoiser
{
	None,
	DLSS_RR,
	Other
};

struct GeneralSettings
{
	Denoiser Denoiser = Denoiser::None;
	Mode Mode = Mode::GlobalIllumination;
	bool RaytracedShadows = false;
};

struct RaytracingSettings
{
	int Bounces = 2;
	int SamplesPerPixel = 1;
	bool RussianRoulette = true;
};

struct MaterialSettings
{
	float2 Roughness = { 0.0f, 1.0f };
	float2 Metalness = { 0.0f, 1.0f };
};

struct LightingSettings
{
	float Directional = 1.0f;
	float Point = 1.0f;
	bool LodDimmer = false;
	float Emissive = 1.0f;
	float Effect = 1.0f;
	float Sky = 1.0f;
};


struct SHaRCSettings
{
	float SceneScale = 1.0f;
	int AccumFrameNum = 10;
	int StaleFrameNum = 64;
	float RadianceScale = 1e3f;
	bool AntifireflyFilter = true;
};

struct RISSettings
{
	bool Enabled = true;
	int MaxCandidates = 4;
};

enum struct DiffuseBRDF : int32_t
{
	Lambert,
	Burley,
	OrenNayar,
	Gotanda,
	Chan
};

enum struct HairBSDF : int32_t
{
	None,
	ChiangBSDF,
	FarFieldBCSDF
};

struct SSSSettings
{
	bool Enabled = true;
	int SampleCount = 1;
	float MaxSampleRadius = 1.0f;
	bool EnableTransmission = true;

	bool MaterialOverride = false;
	float3 OverrideTransmissionColor = float3(1.0f, 0.735f, 0.612f);
	float3 OverrideScatteringColor = float3(1.0f, 1.0f, 1.0f);
	float OverrideScale = 40.0f;
	float OverrideAnisotropy = -0.5f;
};

struct AdvancedSettings
{
	float TexLODBias = -1.0f;
	bool VariableUpdateRate = true;
	bool GGXEnergyConservation = true;
	bool PerLightTLAS = false;
	RISSettings RIS;
	HairBSDF HairBSDF = HairBSDF::FarFieldBCSDF;
	DiffuseBRDF DiffuseBRDF = DiffuseBRDF::Burley;
	SSSSettings SSSSettings;
};

struct WaterSettings
{
	float AbsorptionScale = 1.0f;
};

enum struct ReSTIRGIResamplingMode : int32_t
{
	None = 0,
	Temporal = 1,
	Spatial = 2,
	TemporalAndSpatial = 3,
	FusedSpatiotemporal = 4,
};

enum struct ReSTIRGIBiasCorrection : int32_t
{
	Off = 0,
	Basic = 1,
	Raytraced = 3
};

struct ReSTIRGISettings
{
	bool Enabled = true;
	ReSTIRGIResamplingMode ResamplingMode = ReSTIRGIResamplingMode::TemporalAndSpatial;

	// Temporal
	float TemporalDepthThreshold = 0.1f;
	float TemporalNormalThreshold = 0.5f;
	int MaxHistoryLength = 20;
	int MaxReservoirAge = 100;
	bool EnablePermutationSampling = true;
	bool EnableFallbackSampling = true;
	ReSTIRGIBiasCorrection TemporalBiasCorrection = ReSTIRGIBiasCorrection::Basic;

	// Spatial
	float SpatialDepthThreshold = 0.1f;
	float SpatialNormalThreshold = 0.5f;
	int SpatialNumSamples = 2;
	float SpatialSamplingRadius = 32.0f;
	ReSTIRGIBiasCorrection SpatialBiasCorrection = ReSTIRGIBiasCorrection::Basic;

	// Boiling filter
	bool EnableBoilingFilter = true;
	float BoilingFilterStrength = 0.4f;

	// Final shading
	bool EnableFinalVisibility = true;
	bool EnableFinalMIS = false;
};

struct DebugSettings
{
	bool PathTracingCull = false;
	bool EnableWater = false;
	bool StablePlanes = true;
};

struct Settings
{
	bool Enabled = true;
	GeneralSettings GeneralSettings;
	LightingSettings LightingSettings;
	RaytracingSettings RaytracingSettings;
	MaterialSettings MaterialSettings;
	SHaRCSettings SHaRCSettings;
	AdvancedSettings AdvancedSettings;
	WaterSettings WaterSettings;
	DebugSettings DebugSettings;
	ReSTIRGISettings ReSTIRGI;
};