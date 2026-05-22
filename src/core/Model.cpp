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
		shaderTypes |= mesh->material->shaderType;
		features |= static_cast<int>(mesh->material->feature);
		shaderFlags.set(mesh->material->shaderFlags.get());
	}
}

nvrhi::rt::AccelStructDesc Model::MakeBLASDesc(bool update)
{
	auto blasDesc = nvrhi::rt::AccelStructDesc()
		.setIsTopLevel(false)
		.setDebugName(std::format("{} - BLAS", m_Name.c_str()));

	// We want things frequent updates/rebuilts to be fast, without neglecting those who are more static
	if (meshFlags.any(Mesh::Flags::Dynamic, Mesh::Flags::Skinned) || m_MeshTypes.any(Mesh::Type::LandLOD))
		blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastBuild;
	else
		blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;

	blasDesc.buildFlags |= (update ? nvrhi::rt::AccelStructBuildFlags::PerformUpdate : nvrhi::rt::AccelStructBuildFlags::AllowUpdate);

	return blasDesc;
}

void Model::CreateBuffers(SceneGraph* sceneGraph)
{
	auto* renderer = Renderer::GetSingleton();
	auto device = renderer->GetDevice();

	m_BufferUploadCommandList = renderer->GetCopyCommandList();
	m_BufferUploadCommandList->open();

	for (auto& mesh : meshes)
		mesh->CreateBuffers(sceneGraph, m_BufferUploadCommandList);

	m_BufferUploadCommandList->close();

	{
		std::scoped_lock lock(renderer->GetExecutionMutex());

		m_SubmittedCopyInstance = device->executeCommandList(m_BufferUploadCommandList, nvrhi::CommandQueue::Copy);

		m_BufferUploadQuery = device->createEventQuery();
		device->setEventQuery(m_BufferUploadQuery, nvrhi::CommandQueue::Copy, m_SubmittedCopyInstance);
	}
}

void Model::UpdateFlags()
{
	auto* device = Renderer::GetSingleton()->GetDevice();

	if (!(m_Flags & Flags::BuffersUploaded)) {
		if (device->pollEventQuery(m_BufferUploadQuery)) {
			m_Flags |= Flags::BuffersUploaded;

			m_BufferUploadCommandList = nullptr;
		}
	}

	if (!(m_Flags & Flags::BLASBuilt)) {
		if (device->pollEventQuery(m_BLASBuildQuery)) {
			m_Flags |= Flags::BLASBuilt;

			m_BLASBuildCommandList = nullptr;
		}
	}
}

void Model::Update(RE::NiAVObject* object, bool isPlayer, nvrhi::ICommandList* commandList)
{
	const auto frameIndex = Renderer::GetSingleton()->GetFrameIndex();

	if (m_LastUpdate == frameIndex)
		return;

	UpdateFlags();

	auto skinningPass = Renderer::GetSingleton()->GetRenderGraph()->GetRootNode()->GetPass<Pass::Skinning>();

	auto externalEmittance = GetExternalEmittance();

	for (auto& mesh : meshes) {
		auto dirtyFlags = mesh->Update(object, isPlayer, meshFlags.get());

		bool vertexUpdate = (dirtyFlags & DirtyFlags::Vertex) != DirtyFlags::None;
		bool skinUpdate = (dirtyFlags & DirtyFlags::Skin) != DirtyFlags::None;

		if (skinningPass && (vertexUpdate || skinUpdate)) {
			skinningPass->QueueUpdate(dirtyFlags, mesh.get());
		}

		mesh->UpdateData(commandList, externalEmittance);

		m_DirtyFlags |= dirtyFlags;
	}

	m_LastUpdate = frameIndex;
}

void Model::SetData(MeshData* meshData, uint32_t& index)
{
	for (auto& mesh : meshes) {
		if (mesh->IsHidden())
			continue;

		meshData[index] = mesh->GetData();
		index++;
	}
}

void Model::BuildBLAS()
{
	if (blas)
		return;

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

	{
		std::scoped_lock lock(renderer->GetExecutionMutex());

		m_BLASBuildCommandList->close();
		device->queueWaitForCommandList(nvrhi::CommandQueue::Compute, nvrhi::CommandQueue::Copy, m_SubmittedCopyInstance);
		auto submittedComputeInstance = device->executeCommandList(m_BLASBuildCommandList, nvrhi::CommandQueue::Compute);

		m_BLASBuildQuery = device->createEventQuery();
		device->setEventQuery(m_BLASBuildQuery, nvrhi::CommandQueue::Compute, submittedComputeInstance);
	}

	m_LastBLASUpdate = renderer->GetFrameIndex();
}

bool Model::IsReady() const
{
	if (!(m_Flags & Model::Flags::BuffersUploaded))
		return false;

	if (!(m_Flags & Model::Flags::BLASBuilt))
		return false;

	return true;
}

void Model::UpdateBLAS(nvrhi::ICommandList* commandList)
{
	auto frameIndex = Renderer::GetSingleton()->GetFrameIndex();

	if (frameIndex == m_LastBLASUpdate)
		return;

	bool update;

	if (m_DirtyFlags.any(DirtyFlags::Visibility, DirtyFlags::Mesh))
		update = false;
	else {
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

	// Consume all flags after BLAS is updated/rebuilt
	m_DirtyFlags.reset(DirtyFlags::Visibility, DirtyFlags::Mesh);
	m_DirtyFlags.reset(DirtyFlags::Vertex, DirtyFlags::Skin, DirtyFlags::Transform);

	m_LastBLASUpdate = frameIndex;
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

	auto* renderer = Renderer::GetSingleton();
	{
		std::scoped_lock lock(renderer->GetExecutionMutex());
		renderer->GetDevice()->executeCommandList(copyCommandList, nvrhi::CommandQueue::Copy);
	}

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

DEFINE_ENUM_FLAG_OPERATORS(Model::Flags::Flag);