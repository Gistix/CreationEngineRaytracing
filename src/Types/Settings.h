#pragma once

struct LightSettings
{
	float Directional = 1.0f;
	float Point = 1.0f;
	bool LodDimmer = false;
};

struct LightingSettings
{
	float Emissive = 1.0f;
	float Effect = 1.0f;
	float Sky = 1.0f;
};

struct RaytracingSettings
{
	int Bounces = 2;
	int SamplesPerPixel = 1;
	bool RussianRoulette = true;
	float TexLODBias = -1.0f;
};

struct MaterialSettings
{
	float2 Roughness = { 0.0f, 1.0f };
	float2 Metalness = { 0.0f, 1.0f };
};

struct SHaRCSettings
{
	float SceneScale = 1.0f;
	int AccumFrameNum = 10;
	int StaleFrameNum = 64;
	float RadianceScale = 1e3f;
	bool AntifireflyFilter = true;
};

struct DebugSettings
{
	bool PathTracingCull = false;
};

struct Settings
{
	bool Enabled = true;
	bool PathTracing = true;
	LightSettings LightSettings;
	LightingSettings LightingSettings;
	RaytracingSettings RaytracingSettings;
	MaterialSettings MaterialSettings;
	SHaRCSettings SHaRCSettings;
	DebugSettings DebugSettings;
};