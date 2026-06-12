#pragma once

struct GeometryRuntimeData
{
	RE::NiAlphaProperty* alphaProperty;
	RE::BSShaderProperty* shaderProperty;
#if defined(SKYRIM)
	RE::NiSkinInstance* skinInstance;
#elif (FALLOUT4)
	RE::BSSkin::Instance* skinInstance;
#endif
	RE::BSGraphics::TriShape* rendererData;
	RE::BSGraphics::VertexDesc	vertexDesc;
};