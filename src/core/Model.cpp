#include "Core/Model.h"
#include "Scene.h"
#include "Renderer.h"

#include "Pass/Raytracing/Common/Skinning.h"

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

	auto* refr = form->AsReference();

	if (refr && refr->extraList.HasType(RE::ExtraDataType::kEmittanceSource)) {
		if (auto* extra = refr->extraList.GetByType<RE::ExtraEmittanceSource>()) {
			if (auto* tesRegion = extra->source->As<RE::TESRegion>()) {
				m_EmittanceColor = reinterpret_cast<float3*>(&tesRegion->emittanceColor);
			}
		}
	}
}

nvrhi::rt::AccelStructDesc Model::MakeBLASDesc(bool update)
{
	auto blasDesc = nvrhi::rt::AccelStructDesc()
		.setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
		.setIsTopLevel(false)
		.setDebugName(std::format("{} - BLAS", m_Name.c_str()));

	if (meshFlags.any(Mesh::Flags::Dynamic, Mesh::Flags::Skinned))
		blasDesc.buildFlags |= (update ? nvrhi::rt::AccelStructBuildFlags::PerformUpdate : nvrhi::rt::AccelStructBuildFlags::AllowUpdate);
	else
		blasDesc.buildFlags |= nvrhi::rt::AccelStructBuildFlags::AllowCompaction;

	return blasDesc;
}

void Model::CreateBuffers(SceneGraph* sceneGraph, nvrhi::ICommandList* commandList)
{
	for (auto& mesh : meshes) {
		mesh->CreateBuffers(sceneGraph, commandList);
	}
}

void Model::Update(RE::NiAVObject* object, bool isPlayer)
{
	const auto frameIndex = Renderer::GetSingleton()->GetFrameIndex();

	if (m_LastUpdate == frameIndex)
		return;

	auto skinningPass = Renderer::GetSingleton()->GetRenderGraph()->GetRootNode()->GetPass<Pass::Skinning>();

	for (auto& mesh : meshes) {
		auto dirtyFlags = mesh->Update(object, isPlayer);

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

void Model::BuildBLAS(nvrhi::ICommandList* commandList)
{
	auto* renderer = Renderer::GetSingleton();

	auto supportedFeatures = renderer->GetSupportedFeatures();
	bool dmmSupport = supportedFeatures & SupportedFeatures::DisplacementMicroMeshes;

	if (dmmSupport && meshFlags.all(Mesh::Flags::Displacement)) {
		BuildDMMBLAS(commandList);
	} else {
		BuildStandardBLAS(commandList);
	}

	m_LastBLASUpdate = renderer->GetFrameIndex();
}

void Model::BuildStandardBLAS(nvrhi::ICommandList* commandList)
{
	auto blasDesc = MakeBLASDesc(false);

	for (auto& mesh : meshes) {
		if (mesh->IsHidden())
			continue;

		blasDesc.addBottomLevelGeometry(mesh->geometryDesc);
	}

	blas = Renderer::GetSingleton()->GetDevice()->createAccelStruct(blasDesc);

	nvrhi::utils::BuildBottomLevelAccelStruct(commandList, blas, blasDesc);
}

void Model::BuildDMMBLAS(nvrhi::ICommandList* commandList)
{
	ID3D12GraphicsCommandList4* nativeCommandList = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);

	auto* displacementMM = Scene::GetSingleton()->m_DisplacementMM.get();

	static auto vertexNormalOffset = offsetof(Vertex, Normal);

	eastl::vector<NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX> geoms;
	geoms.reserve(meshes.size());

	for (auto& mesh : meshes) {
		if (mesh->IsHidden())
			continue;

		NVAPI_D3D12_RAYTRACING_GEOMETRY_DMM_TRIANGLES_DESC triangleDesc{};

		ID3D12Resource* nativeTriangleBuffer = mesh->buffers.triangleBuffer->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
		ID3D12Resource* nativeVertexBuffer = mesh->buffers.vertexBuffer->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
		
		triangleDesc.triangles = {
			0,
			DXGI_FORMAT_R16_UINT,
			DXGI_FORMAT_R32G32B32_FLOAT,
			mesh->triangleCount * 3,
			mesh->vertexCount,
			nativeTriangleBuffer->GetGPUVirtualAddress(),
			{
				nativeVertexBuffer->GetGPUVirtualAddress(),
				sizeof(Vertex)
			}
		};

		if (mesh->flags.all(Mesh::Flags::Displacement)) {
			displacementMM->ProcessMesh(commandList, mesh.get());

			ID3D12Resource* nativeDMMBuffer = mesh->dmm.buffer->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);

			NVAPI_D3D12_RAYTRACING_GEOMETRY_DMM_ATTACHMENT_DESC dmmDesc{};

			dmmDesc.vertexDisplacementVectorBuffer = {
				nativeVertexBuffer->GetGPUVirtualAddress() + vertexNormalOffset,
				sizeof(Vertex)
			};
			dmmDesc.vertexDisplacementVectorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

			dmmDesc.displacementMicromapArray = nativeDMMBuffer->GetGPUVirtualAddress();

			dmmDesc.numDMMUsageCounts = 1;
			dmmDesc.pDMMUsageCounts = &mesh->dmm.usageCount;

			triangleDesc.dmmAttachment = dmmDesc;
		}

		NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX geometryDesc{};
		geometryDesc.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_DMM_TRIANGLES_EX;
		geometryDesc.flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		geometryDesc.dmmTriangles = triangleDesc;

		geoms.push_back(geometryDesc);
	}

	if (geoms.empty())
		return;

	NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX inputDescEx{};
	inputDescEx.type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	inputDescEx.flags = NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE_EX;
	inputDescEx.descsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputDescEx.geometryDescStrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX);
	inputDescEx.numDescs = geoms.size();
	inputDescEx.pGeometryDescs = geoms.data();

	nvrhi::d3d12::AccelStruct* as = checked_cast<AccelStruct*>(blas);

	NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC_EX asDesc = {};
	asDesc.destAccelerationStructureData = blas->GetGPUVirtualAddress();
	asDesc.inputs = inputDescEx;
	asDesc.scratchAccelerationStructureData = m_D3D12ScratchBuffer->GetGPUVirtualAddress();

	NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS asExParams = {};
	asExParams.numPostbuildInfoDescs = 0;
	asExParams.pPostbuildInfoDescs = nullptr;
	asExParams.pDesc = &asDesc;
	asExParams.version = NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS_VER;

	NvAPI_Status nvapiStatus = NvAPI_D3D12_BuildRaytracingAccelerationStructureEx(nativeCommandList, &asExParams);
}

void Model::UpdateBLAS(nvrhi::ICommandList* commandList)
{
	bool update;

	if (m_DirtyFlags.any(DirtyFlags::Visibility, DirtyFlags::Mesh))
		update = false;
	else {
		if (meshFlags.none(Mesh::Flags::Dynamic, Mesh::Flags::Skinned))
			return;

		if (m_DirtyFlags.none(DirtyFlags::Vertex, DirtyFlags::Skin))
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

	// Triggers a BLAS update
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

	// Triggers a BLAS update
	if ( meshes.size() != oldSize)	
		m_DirtyFlags.set(DirtyFlags::Mesh);
}

void Model::RemoveGeometry(RE::BSGeometry* geometry)
{
	// Find the first mesh whose bsGeometryPtr matches
	auto it = eastl::find_if(meshes.begin(), meshes.end(), [&](auto& mesh) {
		return mesh->bsGeometryPtr.get() == geometry;
	});

	if (it == meshes.end())
		return;

	// Erase single element
	meshes.erase(it);

	// Triggers a BLAS update
	m_DirtyFlags.set(DirtyFlags::Mesh);
}