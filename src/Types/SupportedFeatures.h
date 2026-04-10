#pragma once

enum SupportedFeatures : uint32_t
{
	Raytracing = 1 << 0,
	OpacityMicroMaps = 1 << 1,
	DisplacementMicroMeshes = 1 << 1,
	LinearSweptSpheres = 1 << 2
};

DEFINE_ENUM_FLAG_OPERATORS(SupportedFeatures);