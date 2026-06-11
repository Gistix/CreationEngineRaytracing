#pragma once

struct LightRuntimeData
{
	RE::NiColor ambient;
	RE::NiColor diffuse;
	RE::NiColor specular;
	RE::NiPoint3 radius;
	float fade;
	float fadeZone;
};
