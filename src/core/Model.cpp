#include "Core/Model.h"
#include "Scene.h"
#include "Renderer.h"

#include "Pass/Raytracing/Common/Skinning.h"

Model::Model(eastl::string name, RE::NiAVObject* node, RE::TESForm* form, eastl::vector<eastl::unique_ptr<Mesh>>& meshes) :
	m_Name(name), meshes(eastl::move(meshes))
{
	UpdateMeshFlags();

	// Models with these flags cannot be instanced directly
	if (meshFlags.any(Mesh::Flags::Dynamic, Mesh::Flags::Skinned))
		m_Name.append(Model::KeySuffix(node).c_str());

	// Water and LOD models have no form
	if (form) {
		auto* refr = form->AsReference();

		if (refr && refr->extraList.HasType(RE::ExtraDataType::kEmittanceSource)) {
			if (auto* extra = refr->extraList.GetByType<RE::ExtraEmittanceSource>()) {
				if (auto* tesRegion = extra->source->As<RE::TESRegion>()) {
					m_EmittanceColor = reinterpret_cast<float3*>(&tesRegion->emittanceColor);
				}
			}
		}
	}
}

void Model::UpdateMeshFlags()
{
	meshFlags.reset();
	m_MeshTypes.reset();
	shaderTypes = RE::BSShader::Type::None;
	features = static_cast<int>(RE::BSShaderMaterial::Feature::kNone);
	shaderFlags.reset();

	for (auto& mesh : meshes) {
		meshFlags.set(mesh->flags.get());
		m_MeshTypes.set(mesh->m_Type);
		shaderTypes |= mesh->material.shaderType;
		features |= static_cast<int>(mesh->material.Feature);
		shaderFlags.set(mesh->material.shaderFlags.get());
	}
}

nvrhi::rt::AccelStructDesc Model::MakeBLASDesc(bool update)
{
	auto blasDesc = nvrhi::rt::AccelStructDesc()
		.setIsTopLevel(false)
		.setDebugName(std::format("{} - BLAS", m_Name.c_str()));

	if (meshFlags.any(Mesh::Flags::Dynamic, Mesh::Flags::Skinned) || m_MeshTypes.any(Mesh::Type::LandLOD))
		blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastBuild | (update ? nvrhi::rt::AccelStructBuildFlags::PerformUpdate : nvrhi::rt::AccelStructBuildFlags::AllowUpdate);
	else {
		blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;

		// BLASes built with allow compaction cannot be rebuilt
		if (meshFlags.none(Mesh::Flags::LOD))
			blasDesc.buildFlags |= nvrhi::rt::AccelStructBuildFlags::AllowCompaction;
	}

	return blasDesc;
}

void Model::CreateBuffers(SceneGraph* sceneGraph)
{
	auto* renderer = Renderer::GetSingleton();
	m_BufferCopyCommandList = renderer->GetCopyCommandList();
	m_BufferCopyCommandList->open();

	for (auto& mesh : meshes) {
		mesh->CreateBuffers(sceneGraph, m_BufferCopyCommandList);
	}

	m_BufferCopyCommandList->close();
	m_SubmittedCopyInstance = renderer->GetDevice()->executeCommandList(m_BufferCopyCommandList, nvrhi::CommandQueue::Copy);
}

void Model::Update(RE::NiAVObject* object, bool isPlayer)
{
	if (!(m_Flags & Flags::BLASBuilt)) {
		if (Renderer::GetSingleton()->GetDevice()->pollEventQuery(m_BLASBuildQuery)) {
			m_Flags |= Flags::BLASBuilt;

			m_BufferCopyCommandList->Release();
			m_BufferCopyCommandList = nullptr;

			m_BLASBuildCommandList->Release();
			m_BLASBuildCommandList = nullptr;
		}
	}

	const auto frameIndex = Renderer::GetSingleton()->GetFrameIndex();

	if (m_LastUpdate == frameIndex)
		return;

	auto skinningPass = Renderer::GetSingleton()->GetRenderGraph()->GetRootNode()->GetPass<Pass::Skinning>();

	for (auto& mesh : meshes) {
		auto dirtyFlags = mesh->Update(object, isPlayer, meshFlags.get());

		bool vertexUpdate = (dirtyFlags & DirtyFlags::Vertex) != DirtyFlags::None;
		bool skinUpdate = (dirtyFlags & DirtyFlags::Skin) != DirtyFlags::None;

		if (skinningPass && (vertexUpdate || skinUpdate)) {
			skinningPass->QueueUpdate(dirtyFlags, mesh.get());
		}

		m_DirtyFlags |= dirtyFlags;
	}

	m_LastUpdate = frameIndex;
}

void Model::SetData(MeshData* meshData, uint32_t& index)
{
	float3 externalEmittance = GetExternalEmittance();

	for (auto& mesh : meshes) {
		if (mesh->IsHidden())
			continue;

		meshData[index] = mesh->GetData(externalEmittance);
		index++;
	}
}

void Model::BuildBLAS()
{
	auto blasDesc = MakeBLASDesc(false);

	for (auto& mesh: meshes) {
		if (mesh->IsHidden())
			continue;

		blasDesc.addBottomLevelGeometry(mesh->geometryDesc);
	}

	auto* renderer = Renderer::GetSingleton();
	auto device = renderer->GetDevice();

	blas = renderer->GetDevice()->createAccelStruct(blasDesc);

	// Compute Command - Waits for copy
	m_BLASBuildCommandList = renderer->GetComputeCommandList();
	m_BLASBuildCommandList->open();

	nvrhi::utils::BuildBottomLevelAccelStruct(m_BLASBuildCommandList, blas, blasDesc);

	m_BLASBuildCommandList->compactBottomLevelAccelStructs();

	m_BLASBuildCommandList->close();
	device->queueWaitForCommandList(nvrhi::CommandQueue::Compute, nvrhi::CommandQueue::Copy, m_SubmittedCopyInstance);
	auto computeSubmittedInstance = device->executeCommandList(m_BLASBuildCommandList, nvrhi::CommandQueue::Compute);

	m_BLASBuildQuery = device->createEventQuery();
	device->setEventQuery(m_BLASBuildQuery, nvrhi::CommandQueue::Compute, computeSubmittedInstance);

	m_LastBLASUpdate = renderer->GetFrameIndex();
}

void Model::UpdateBLAS(nvrhi::ICommandList* commandList)
{
	bool update;

	if (m_DirtyFlags.any(DirtyFlags::Visibility, DirtyFlags::Mesh))
		update = false;
	else {
		if (meshFlags.none(Mesh::Flags::Dynamic, Mesh::Flags::Skinned) && m_MeshTypes.none(Mesh::Type::LandLOD))
			return;

		// TODO: Add transform updates to non-skinned/non-dynamic meshes
		// Attempting it on other model types currenty causes device disconnection
		if (m_DirtyFlags.none(DirtyFlags::Vertex, DirtyFlags::Skin, DirtyFlags::Transform))
			return;

		update = true;
	}

	// If update is false the BLAS will be rebuilt (vertex moves = update, mesh hidden = rebuild)
	auto blasDesc = MakeBLASDesc(update);

	for (auto& mesh: meshes)
	{
		if (mesh->IsHidden())
			continue;

		blasDesc.addBottomLevelGeometry(mesh->geometryDesc);
	}

	nvrhi::utils::BuildBottomLevelAccelStruct(commandList, blas, blasDesc);

	m_LastBLASUpdate = Renderer::GetSingleton()->GetFrameIndex();
}

void Model::AppendMeshes(SceneGraph* sceneGraph, eastl::vector<eastl::unique_ptr<Mesh>>& a_meshes)
{
	// Copy Command
	auto copyCommandList = Renderer::GetSingleton()->GetCopyCommandList();
	copyCommandList->open();

	for (auto& mesh : a_meshes) {
		mesh->CreateBuffers(sceneGraph, copyCommandList);
		meshes.push_back(eastl::move(mesh));
	}

	copyCommandList->close();
	Renderer::GetSingleton()->GetDevice()->executeCommandList(copyCommandList, nvrhi::CommandQueue::Copy);

	UpdateMeshFlags();

	// Triggers a BLAS rebuild
	m_DirtyFlags.set(DirtyFlags::Mesh);
}

void Model::RemoveMeshes(const eastl::vector<Mesh*>& a_meshes)
{
	auto oldSize = meshes.size();

	// Remove any unique_ptr whose raw pointer is in toRemove
	meshes.erase(
		eastl::remove_if(meshes.begin(), meshes.end(),
			[&a_meshes](const auto& m)
			{
				return eastl::find(a_meshes.begin(), a_meshes.end(), m.get()) != a_meshes.end();
			}),
		meshes.end()
	);

	UpdateMeshFlags();

	// Triggers a BLAS rebuild
	if (meshes.size() != oldSize)	
		m_DirtyFlags.set(DirtyFlags::Mesh);
}