#include "Core/Model.h"
#include "Scene.h"
#include "Renderer.h"

Model::Model(eastl::string name, RE::NiAVObject* node, RE::TESForm* form, eastl::vector<eastl::unique_ptr<Mesh>>& meshes) :
	m_Name(name), meshes(eastl::move(meshes))
{
	for (auto& mesh : this->meshes) {
		meshFlags.set(mesh->flags.get());
		shaderTypes |= mesh->material.shaderType;
		features |= static_cast<int>(mesh->material.Feature);
		shaderFlags.set(mesh->material.shaderFlags.get());
	}

	// Models with these flags cannot be instanced directly
	if (meshFlags.any(Mesh::Flags::Dynamic, Mesh::Flags::Skinned))
		m_Name.append(Model::KeySuffix(node).c_str());

	blasDesc.setDebugName(std::format("{} - BLAS", m_Name))
		.setIsTopLevel(false);

	if (meshFlags.any(Mesh::Flags::Dynamic, Mesh::Flags::Skinned))
		blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace | nvrhi::rt::AccelStructBuildFlags::AllowUpdate;
	else
		blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace | nvrhi::rt::AccelStructBuildFlags::AllowCompaction;

	auto* refr = form->AsReference();

	if (auto* extra = refr->extraList.GetByType<RE::ExtraEmittanceSource>())
		m_EmittanceColor = reinterpret_cast<float3*>(&extra->source->As<RE::TESRegion>()->emittanceColor);
}

void Model::CreateBuffers(SceneGraph* sceneGraph, nvrhi::ICommandList* commandList)
{
	for (auto& mesh : meshes) {
		mesh->CreateBuffers(sceneGraph, commandList);
	}
}

void Model::Update()
{
	/*auto& blasGeoms = blasDesc.bottomLevelGeometries;

	blasGeoms.clear();
	blasGeoms.reserve(meshes.size());*/

	for (auto& mesh : meshes) {
		auto dirtyFlags = mesh->Update();

		if (mesh->IsDirtyState())
			m_DirtyFlags |= DirtyFlags::Visibility;

		m_DirtyFlags |= dirtyFlags;
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

void Model::UpdateBLAS(nvrhi::ICommandList* commandList)
{
	if (meshFlags.none(Mesh::Flags::Dynamic, Mesh::Flags::Skinned))
		return;

	if (m_DirtyFlags.none(DirtyFlags::Vertex, DirtyFlags::Skin))
		return;

	auto blasUpdateDesc = nvrhi::rt::AccelStructDesc()
		.setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace | nvrhi::rt::AccelStructBuildFlags::PerformUpdate)
		.setIsTopLevel(false);

	for (size_t i = 0; i < meshes.size(); i++) {
		blasUpdateDesc.addBottomLevelGeometry(meshes[i]->geometryDesc);
	}

	nvrhi::utils::BuildBottomLevelAccelStruct(commandList, blas, blasUpdateDesc);
}