#pragma once

enum class Mode
{
	None,
	GlobalIllumination,
	PathTracing
};

enum class Denoiser
{
	None,
	NRD_REBLUR,
	DLSS_RR
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

struct ReblurSettings
{
	// [0; REBLUR_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames
	// Always accumulate in "seconds" not in "frames", use "GetMaxAccumulatedFrameNum" for conversion
	uint32_t maxAccumulatedFrameNum = 30;

	// [0; maxAccumulatedFrameNum] - maximum number of linearly accumulated frames for fast history
	// Values ">= maxAccumulatedFrameNum" disable fast history
	// Usually 5x-7x times shorter than the main history (casting more rays, using SHARC or other signal improving techniques help to accumulate less)
	uint32_t maxFastAccumulatedFrameNum = 6;

	// [0; maxAccumulatedFrameNum] - maximum number of linearly accumulated frames for stabilized radiance
	// "0" disables the stabilization pass
	// Values ">= maxAccumulatedFrameNum" get clamped to "maxAccumulatedFrameNum"
	uint32_t maxStabilizedFrameNum = 63;

	// [0; maxFastAccumulatedFrameNum) - number of reconstructed frames after history reset
	uint32_t historyFixFrameNum = 3;

	// (> 0) - base stride between pixels in 5x5 history reconstruction kernel
	uint32_t historyFixBasePixelStride = 14;
	uint32_t historyFixAlternatePixelStride = 14; // see "historyFixAlternatePixelStrideMaterialID"

	// [1; 3] - standard deviation scale of the color box for clamping slow "main" history to responsive "fast" history
	// REBLUR clamps the spatially processed "main" history to the spatially unprocessed "fast" history. It implies using smaller variance scaling than in RELAX.
	// A bit smaller values (> 1) may be used with clean signals. The implementation will adjust this under the hood if spatial sampling is disabled
	float fastHistoryClampingSigmaScale = 2.0f; // 2 is old default, 1.5 works well even for dirty signals, 1.1 is a safe value for occlusion denoising

	// (pixels) - pre-accumulation spatial reuse pass blur radius (0 = disabled, must be used in case of badly defined signals and probabilistic sampling)
	float diffusePrepassBlurRadius = 30.0f;
	float specularPrepassBlurRadius = 50.0f;

	// (0; 0.2] - bigger values reduce sensitivity to shadows in spatial passes, smaller values are recommended for signals with relatively clean hit distance (like RTXDI/RESTIR)
	float minHitDistanceWeight = 0.1f;

	// (pixels) - min denoising radius (for converged state)
	float minBlurRadius = 1.0f;

	// (pixels) - base (max) denoising radius (gets reduced over time)
	float maxBlurRadius = 30.0f;

	// (normalized %) - base fraction of diffuse or specular lobe angle used to drive normal based rejection
	float lobeAngleFraction = 0.15f;

	// (normalized %) - base fraction of center roughness used to drive roughness based rejection
	float roughnessFraction = 0.15f;

	// (normalized %) - represents maximum allowed deviation from the local tangent plane
	float planeDistanceSensitivity = 0.02f;

	// "IN_MV = lerp(IN_MV, specularMotion, smoothstep(this[0], this[1], specularProbability))"
	std::array<float, 2> specularProbabilityThresholdsForMvModification = { 0.5f, 0.9f };

	// [1; 3] - undesired sporadic outliers suppression to keep output stable (smaller values maximize suppression in exchange of bias)
	float fireflySuppressorMinRelativeScale = 2.0f;

	// Helps to mitigate fireflies emphasized by DLSS. Very cheap and unbiased in most of the cases, better keep in enabled to maximize quality
	bool enableAntiFirefly = true;

	// In rare cases, when bright samples are so sparse that any other bright neighbor can't
	// be reached, pre-pass transforms a standalone bright pixel into a standalone bright blob,
	// worsening the situation. Despite that it's a problem of sampling, the denoiser needs to
	// handle it somehow on its side too. Diffuse pre-pass can be just disabled, but for specular
	// it's still needed to find optimal hit distance for tracking. This boolean allow to use
	// specular pre-pass for tracking purposes only (use with care)
	bool usePrepassOnlyForSpecularMotionEstimation = false;

	// Allows to get diffuse or specular history length in ".w" channel of the output instead of denoised ambient/specular occlusion (normalized hit distance).
	// Diffuse history length shows disocclusions, specular history length is more complex and includes accelerations of various kinds caused by specular tracking.
	// History length is measured in frames, it can be in "[0; maxAccumulatedFrameNum]" range
	bool returnHistoryLengthInsteadOfOcclusion = false;
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
	bool Enabled = true;
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
	ReblurSettings ReblurSettings;
	MaterialSettings MaterialSettings;
	SHaRCSettings SHaRCSettings;
	AdvancedSettings AdvancedSettings;
	WaterSettings WaterSettings;
	DebugSettings DebugSettings;
	ReSTIRGISettings ReSTIRGI;
};