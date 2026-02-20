#include "core/Model.h"
#include "Scene.h"
#include "Renderer.h"

void Model::Update()
{
	auto& blasGeoms = blasDesc.bottomLevelGeometries;

	blasGeoms.clear();
	blasGeoms.reserve(meshes.size());

	for (auto& mesh : meshes) {
		auto updateFlags = mesh->Update();

		if (mesh->IsDirtyState()) {
			m_UpdateFlags.set(Model::UpdateFlags::Rebuild);
		}

		if ((updateFlags & Mesh::UpdateFlags::Vertices) != Mesh::UpdateFlags::None || (updateFlags & Mesh::UpdateFlags::Skinning) != Mesh::UpdateFlags::None) {
			m_UpdateFlags.set(Model::UpdateFlags::Update);
		}
	}
}

void Model::BuildBLAS(nvrhi::ICommandList* commandList)
{
	// Initial build with all shapes, visible or not, so the scratch buffer can be sized to fit all geometry
	for (size_t i = 0; i < meshes.size(); i++) {
		blasDesc.addBottomLevelGeometry(meshes[i]->geometryDesc);
	}

	blas = Renderer::GetSingleton()->GetDevice()->createAccelStruct(blasDesc);

	nvrhi::utils::BuildBottomLevelAccelStruct(commandList, blas, blasDesc);
}