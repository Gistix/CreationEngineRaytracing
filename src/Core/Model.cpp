#include "Core/Model.h"
#include "Scene.h"
#include "Renderer.h"

#include "Pass/Raytracing/Common/Skinning.h"

Model::Model(eastl::string name, Type type, RE::NiAVObject* node, RE::TESForm* form, eastl::vector<eastl::unique_ptr<Mesh>>& meshes) :
	m_Name(name), m_Type(type), m_Meshes(eastl::move(meshes))
{
	UpdateMeshFlags();

	// Initialize visibility state for BLAS creation
	for (auto& mesh : m_Meshes) {
		mesh->InitState(node, meshFlags.get());
	}

	// Actors cannot be instanced or copied, so we give them an unique name
	if (type == Type::Actor)
		m_Name.append(Model::KeySuffix(node).c_str());

	// Water and LOD models have no form
	if (form) {
		auto* refr = Util::Adapter::AsReference(form);

		if (refr) {
#if defined(SKYRIM)
			auto extraDataList = Util::Adapter::GetExtraDataList(refr);
			if (extraDataList && extraDataList->HasType(RE::ExtraDataType::kEmittanceSource)) {
				if (auto* extra = extraDataList->GetByType<RE::ExtraEmittanceSource>()) {
					if (auto* tesRegion = extra->source->As<RE::TESRegion>()) {
						m_EmittanceColor = reinterpret_cast<float3*>(&tesRegion->emittanceColor);
					}
				}
			}
#endif
		}
	}
}

eastl::unique_ptr<Model> Model::Clone(RE::NiAVObject* node, RE::FormID formID) const
{
	eastl::vector<eastl::unique_ptr<Mesh>> clonedMeshes;
	clonedMeshes.reserve(m_Meshes.size());

	for (const auto& mesh : m_Meshes) {
		auto clonedMesh = mesh->Clone(node, formID);
		if (clonedMesh)
			clonedMeshes.push_back(eastl::move(clonedMesh));
	}

	if (clonedMeshes.empty())
		return nullptr;

	eastl::string cloneName = m_Name + KeySuffix(node);

	auto clone = eastl::make_unique<Model>(cloneName, Type::Default, node, nullptr, clonedMeshes);

	clone->m_Type = m_Type;
	clone->m_EmittanceColor = m_EmittanceColor;

	return clone;
}

void Model::UpdateMeshFlags()
{
	meshFlags.reset();
	m_MeshTypes.reset();
	shaderFlags.reset();

	for (auto& mesh : m_Meshes) {
		meshFlags.set(mesh->flags.get());
		m_MeshTypes.set(mesh->m_Type);
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

	for (auto& mesh : m_Meshes)
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
	auto* renderer = Renderer::GetSingleton();
	const auto frameIndex = renderer->GetFrameIndex();

	if (m_LastUpdate == frameIndex)
		return;

	UpdateFlags();

	auto externalEmittance = GetExternalEmittance();

	for (auto& mesh : m_Meshes) {
		auto dirtyFlags = mesh->Update(object, isPlayer, meshFlags.get());

		if (!mesh->IsHidden())
			mesh->UpdateData(commandList, externalEmittance);

		m_DirtyFlags |= dirtyFlags;
	}

	m_LastUpdate = frameIndex;
}

DataParams Model::GetData(MeshData* meshData, uint32_t& index)
{	
	const auto frameIndex = Renderer::GetSingleton()->GetFrameIndex();

	m_DataParams.alreadyUpdated = (m_LastDataUpload == frameIndex);
	if (m_DataParams.alreadyUpdated)
		return m_DataParams;

	m_DataParams.firstMeshID = index;
	m_DataParams.numMeshes = 0;

	for (auto& mesh : m_Meshes) {
		if (mesh->IsHidden())
			continue;

		m_DataParams.numMeshes++;

		meshData[index] = mesh->GetData();
		index++;
	}

	m_DataParams.hidden = (m_DataParams.firstMeshID == index);

	m_LastDataUpload = frameIndex;
	return m_DataParams;
}

bool Model::IsReady() const
{
	if (!(m_Flags & Model::Flags::BuffersUploaded))
		return false;

	if (!(m_Flags & Model::Flags::BLASBuilt))
		return false;

	return true;
}

void Model::BuildBLAS()
{
	if (m_BLAS) {
		logger::critical("Model::BuildBLAS - BLAS already exists for model '{}', skipping BLAS build.", m_Name.c_str());
		return;
	}

	auto* renderer = Renderer::GetSingleton();
	auto device = renderer->GetDevice();

	// Compute Command - Waits for copy
	m_BLASBuildCommandList = renderer->GetComputeCommandList();
	m_BLASBuildCommandList->open();

	BuildUpdateBLAS(m_BLASBuildCommandList);

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

void Model::BuildUpdateBLAS(nvrhi::ICommandList* commandList)
{
	auto* renderer = Renderer::GetSingleton();
	auto* device = renderer->GetDevice();
	const auto frameIndex = renderer->GetFrameIndex();

	if (frameIndex == m_LastBLASUpdate)
		return;

	bool create = !m_BLAS;
	bool rebuild = true;

	if (create || m_DirtyFlags.any(DirtyFlags::Visibility, DirtyFlags::Mesh)) {
		rebuild = true;
		m_NumUpdatesSinceRebuild = 0;
	}
	else if (m_DirtyFlags.any(DirtyFlags::Vertex, DirtyFlags::Skin, DirtyFlags::Transform)) {
		if (m_NumUpdatesSinceRebuild >= Constants::MAX_BLAS_UPDATES_BEFORE_MAINTENANCE) {
			if (Scene::GetSingleton()->GetSceneGraph()->TryMaintenanceRebuild(frameIndex)) {
				rebuild = true;
				m_NumUpdatesSinceRebuild = 0;
			} else {
				rebuild = false;
			}
		} else {
			m_NumUpdatesSinceRebuild++;
			rebuild = false;
		}
	}
	else {
		return;
	}

	auto blasDesc = MakeBLASDesc(!rebuild);

	// Collect geometry descriptions
	for (auto& mesh : m_Meshes)
	{
		if (mesh->IsHidden())
			continue;

		blasDesc.addBottomLevelGeometry(mesh->geometryDesc);
	}

	if (!create && rebuild) {
		auto prebuildInfo = device->getAccelStructPreBuildInfo(blasDesc);
		create = prebuildInfo.resultMaxSizeInBytes > m_BLAS->getBufferSize();

		if (create)
			logger::debug("Model::BuildUpdateBLAS - Required BLAS size of {} greater than previous size of {}, creating a new buffer.", prebuildInfo.resultMaxSizeInBytes, m_BLAS->getBufferSize());
	}

	if (create)
		m_BLAS = device->createAccelStruct(blasDesc);

	nvrhi::utils::BuildBottomLevelAccelStruct(commandList, m_BLAS, blasDesc);

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
		m_Meshes.push_back(eastl::move(mesh));
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
	auto oldSize = m_Meshes.size();

	// Remove any unique_ptr whose raw pointer is in toRemove
	m_Meshes.erase(
		eastl::remove_if(m_Meshes.begin(), m_Meshes.end(),
			[&a_meshes](const auto& m)
			{
				return eastl::find(a_meshes.begin(), a_meshes.end(), m.get()) != a_meshes.end();
			}),
		m_Meshes.end()
	);

	UpdateMeshFlags();

	// Triggers a BLAS rebuild
	if (m_Meshes.size() != oldSize)	
		m_DirtyFlags.set(DirtyFlags::Mesh);
}

DEFINE_ENUM_FLAG_OPERATORS(Model::Flags::Flag);
