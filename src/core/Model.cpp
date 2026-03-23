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

	if (meshFlags.all(Mesh::Flags::Water))
	{
		if (form->GetFormType() == RE::FormType::Water) {
			auto* waterForm = form->As<RE::TESWaterForm>();

			m_WaterTexScroll[0] = reinterpret_cast<float4*>(&waterForm->texScroll[0]);
			m_WaterTexScroll[1] = reinterpret_cast<float4*>(&waterForm->texScroll[1]);
			m_WaterTexScroll[2] = reinterpret_cast<float4*>(&waterForm->texScroll[2]);

			m_HasWaterTexScroll = true;
		}
	}

	if (meshFlags.none(Mesh::Flags::Landscape, Mesh::Flags::Water))
	{
		auto* refr = form->AsReference();

		if (auto* extra = refr->extraList.GetByType<RE::ExtraEmittanceSource>()) {
			auto* tesRegion = extra->source->As<RE::TESRegion>();
			m_EmittanceColor = reinterpret_cast<float3*>(&tesRegion->emittanceColor);
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

void Model::Update()
{
	const auto frameIndex = Renderer::GetSingleton()->GetFrameIndex();

	if (m_LastUpdate == frameIndex)
		return;

	auto skinningPass = Renderer::GetSingleton()->GetRenderGraph()->GetRootNode()->GetPass<Pass::Skinning>();

	for (auto& mesh : meshes) {
		auto dirtyFlags = mesh->Update();

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

	float4 waterTexScroll[3] = {
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f }
	};

	if (m_HasWaterTexScroll) {
		waterTexScroll[0] = -(*m_WaterTexScroll[0]);
		waterTexScroll[1] = -(*m_WaterTexScroll[1]);
		waterTexScroll[2] = -(*m_WaterTexScroll[2]);
	}

	for (auto& mesh : meshes) {
		if (mesh->IsHidden())
			continue;

		meshData[index] = mesh->GetData(externalEmittance, waterTexScroll);
		index++;
	}
}

void Model::BuildBLAS(nvrhi::ICommandList* commandList)
{
	auto blasDesc = MakeBLASDesc(false);

	// Initial build with all shapes, visible or not, so the scratch buffer can be sized to fit all geometry
	for (auto& mesh: meshes) {
		blasDesc.addBottomLevelGeometry(mesh->geometryDesc);
	}

	auto* renderer = Renderer::GetSingleton();

	blas = renderer->GetDevice()->createAccelStruct(blasDesc);

	nvrhi::utils::BuildBottomLevelAccelStruct(commandList, blas, blasDesc);

	m_LastBLASUpdate = renderer->GetFrameIndex();
}

void Model::UpdateBLAS(nvrhi::ICommandList* commandList)
{
	bool update;

	if (m_DirtyFlags.all(DirtyFlags::Visibility))
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