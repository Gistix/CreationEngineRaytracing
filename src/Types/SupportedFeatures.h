#pragma once

enum SupportedFeatures : uint32_t
{
	Raytracing = 1 << 0,
	InlineRaytracing = 1 << 1,
	OpacityMicroMaps = 1 << 2,
	DisplacementMicroMeshes = 1 << 3,
	LinearSweptSpheres = 1 << 4,
	ShaderExecutionReordering = 1 << 5,
	RayTracingClusters = 1 << 6
};

DEFINE_ENUM_FLAG_OPERATORS(SupportedFeatures);