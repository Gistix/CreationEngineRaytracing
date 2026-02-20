#include "core/Model.h"
#include "Scene.h"
#include "Renderer.h"

void Model::BuildBLAS(nvrhi::ICommandList* commandList)
{
	blasDesc.setDebugName("BLAS")
		.setIsTopLevel(false);

	// Initial build with all shapes, visible or not, so the scratch buffer can be sized to fit all geometry
	for (size_t i = 0; i < meshes.size(); i++) {
		blasDesc.addBottomLevelGeometry(meshes[i]->geometryDesc);
	}

	blas = Renderer::GetDevice()->createAccelStruct(blasDesc);

	auto& geometries = blasDesc.bottomLevelGeometries;

	commandList->buildBottomLevelAccelStruct(blas, geometries.data(), geometries.size());
}