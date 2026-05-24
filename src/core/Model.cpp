#include "Core/Model.h"
#include "Scene.h"
#include "Renderer.h"

#include "Pass/Raytracing/Common/Skinning.h"

Model::Model(eastl::string name, RE::NiAVObject* node, RE::TESForm* form, eastl::vector<eastl::unique_ptr<Mesh>>& meshes) :
	m_Name(name), m_Meshes(eastl::move(meshes))
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

	for (auto& mesh : m_Meshes) {
		meshFlags.set(mesh->flags.get());
		m_MeshTypes.set(mesh->m_Type);
		shaderTypes |= mesh->material->shaderType;
		features |= static_cast<int>(mesh->material->feature);
		shaderFlags.set(mesh->material->shaderFlags.get());
	}
}

nvrhi::rt::AccelStructDesc Model::MakeBLASDesc(bool rebuild)
{
	auto blasDesc = nvrhi::rt::AccelStructDesc()
		.setIsTopLevel(false)
		.setDebugName(std::format("{} - BLAS", m_Name.c_str()));

	// We want things frequent updates/rebuilts to be fast, without neglecting those who are more static
	if (meshFlags.any(Mesh::Flags::Dynamic, Mesh::Flags::Skinned) || m_MeshTypes.any(Mesh::Type::LandLOD))
		blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastBuild;
	else
		blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;

	blasDesc.buildFlags |= (rebuild ? nvrhi::rt::AccelStructBuildFlags::AllowUpdate : nvrhi::rt::AccelStructBuildFlags::PerformUpdate);

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

	if (!m_BuffersUploaded) {
		if (device->pollEventQuery(m_BufferUploadQuery)) {
			m_BuffersUploaded = true;
			m_BufferUploadCommandList = nullptr;
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

	auto skinningPass = renderer->GetRenderGraph()->GetRootNode()->GetPass<Pass::Skinning>();

	auto externalEmittance = GetExternalEmittance();

	for (auto& mesh : m_Meshes) {
		auto dirtyFlags = mesh->Update(object, isPlayer, meshFlags.get());

		bool vertexUpdate = (dirtyFlags & DirtyFlags::Vertex) != DirtyFlags::None;
		bool skinUpdate = (dirtyFlags & DirtyFlags::Skin) != DirtyFlags::None;

		if (skinningPass && (vertexUpdate || skinUpdate)) {
			skinningPass->QueueUpdate(dirtyFlags, mesh.get());
		}

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

	for (auto& mesh : m_Meshes) {
		if (mesh->IsHidden())
			continue;

		meshData[index] = mesh->GetData();
		index++;
	}

	m_DataParams.hidden = (m_DataParams.firstMeshID == index);

	m_LastDataUpload = frameIndex;
	return m_DataParams;
}

bool Model::IsReady() const
{
	return m_BuffersUploaded;
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
	}
	else {
		// Must have a valid update flag
		if (m_DirtyFlags.none(DirtyFlags::Vertex, DirtyFlags::Skin, DirtyFlags::Transform))
			return;

		rebuild = false;
	}

	// If rebuild is true the BLAS will be build/rebuilt (mesh list changed = rebuild, vertex moved = update)
	// TODO: We should probably rebuild periodically to avoid performance degradation (we had issues in the past with TLAS updates, so nowadays we always rebuild TLAS)
	auto blasDesc = MakeBLASDesc(rebuild);

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

void Model::AppendMeshes(SceneGraph* sceneGraph, nvrhi::ICommandList* commandList, eastl::vector<eastl::unique_ptr<Mesh>>& a_meshes)
{
	for (auto& mesh : a_meshes) {
		mesh->CreateBuffers(sceneGraph, commandList);
		m_Meshes.push_back(eastl::move(mesh));
	}

	UpdateMeshFlags();

	// Signals a BLAS rebuild
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

	// Signals a BLAS rebuild
	if (m_Meshes.size() != oldSize)
		m_DirtyFlags.set(DirtyFlags::Mesh);
};