#include "Core/MeshManager.h"

#include "Renderer.h"

MeshManager::MeshManager()
	: 	m_MeshSlots(sizeof(uint32_t), Constants::NUM_MESHES_MAX, Constants::NUM_MESHES_MAX),
	m_GeometrySlots(sizeof(MeshData), Constants::NUM_MESHES_MAX, Constants::NUM_MESHES_MAX),
	m_TransformSlots(sizeof(float3x4), Constants::NUM_MESHES_MAX),
	m_PrevTransformSlots(sizeof(float3x4), Constants::NUM_MESHES_MAX),
	m_PropertiesSlots(sizeof(PropertiesData), Constants::NUM_MESHES_MAX, Constants::NUM_MESHES_MAX),
	m_GrassTransformSlots(sizeof(TransformData), Constants::NUM_GRASS_INSTANCES_MAX)
{
	CreateBuffers();
}

void MeshManager::CreateBuffers()
{
	auto device = Renderer::GetSingleton()->GetDevice();

	auto inputDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_MESHES_MAX * sizeof(float3x4))
		.setStructStride(sizeof(float3x4))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setCanHaveUAVs(false);

	m_CurrentBuffer = device->createBuffer(inputDesc.setDebugName("Current Transform Buffer"));
	m_PrevBuffer = device->createBuffer(inputDesc.setDebugName("Prev Transform Buffer"));

	auto outDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_MESHES_MAX * sizeof(TransformData))
		.setStructStride(sizeof(TransformData))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setCanHaveUAVs(true)
		.setIsAccelStructBuildInput(Renderer::GetSingleton()->IsVulkan())
		.setDebugName("Transform Buffer");

	m_Buffer = device->createBuffer(outDesc);

	auto meshDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_MESHES_MAX * sizeof(MeshData))
		.setStructStride(sizeof(MeshData))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setDebugName("Mesh Buffer");

	m_MeshBuffer = device->createBuffer(meshDesc);

	auto propsDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_MESHES_MAX * sizeof(PropertiesData))
		.setCanHaveRawViews(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setDebugName("Properties Buffer");

	m_PropertiesBuffer = device->createBuffer(propsDesc);

	auto grassDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_GRASS_INSTANCES_MAX * sizeof(TransformData))
		.setStructStride(sizeof(TransformData))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setCanHaveUAVs(false)
		.setIsAccelStructBuildInput(Renderer::GetSingleton()->IsVulkan())
		.setDebugName("Grass Transform Buffer");

	m_GrassTransformBuffer = device->createBuffer(grassDesc);
}

uint32_t MeshManager::AllocateMeshIndex()
{
	uint64_t offset = m_MeshSlots.Allocate();
	return m_MeshSlots.GetIndexFromOffset(offset);
}

void MeshManager::ReleaseMeshIndex(uint32_t index)
{
	m_MeshSlots.Release(index * sizeof(uint32_t));
}

uint32_t MeshManager::AllocateGeometryIndex()
{
	uint64_t offset = m_GeometrySlots.Allocate();
	return m_GeometrySlots.GetIndexFromOffset(offset);
}

void MeshManager::ReleaseGeometryIndex(uint32_t index)
{
	m_GeometrySlots.Release(index * sizeof(MeshData));
}

void MeshManager::WriteTransformData(uint32_t index, const float3x4& transform, const float3x4& prevTransform)
{
	m_TransformSlots.Write(index * sizeof(float3x4), &transform, sizeof(float3x4));
	m_PrevTransformSlots.Write(index * sizeof(float3x4), &prevTransform, sizeof(float3x4));
}

void MeshManager::WriteMeshData(uint32_t index, const MeshData& meshData)
{
	m_GeometrySlots.Write(index * sizeof(MeshData), &meshData, sizeof(MeshData));
}

void MeshManager::WritePropertiesData(uint32_t index, const PropertiesData& data)
{
	m_PropertiesSlots.Write(index * sizeof(PropertiesData), &data, sizeof(PropertiesData));
}

uint32_t MeshManager::AllocateGrassTransforms(uint32_t count)
{
	if (count == 0)
		return UINT32_MAX;

	std::scoped_lock lock(m_GrassTransformMutex);

	for (auto it = m_FreeGrassRanges.begin(); it != m_FreeGrassRanges.end(); ++it) {
		if (it->count >= count) {
			const uint32_t base = it->base;
			if (it->count == count) {
				m_FreeGrassRanges.erase(it);
			}
			else {
				it->base += count;
				it->count -= count;
			}
			return base;
		}
	}

	if (m_GrassTransformNext + count > Constants::NUM_GRASS_INSTANCES_MAX)
		return UINT32_MAX;

	const uint32_t base = m_GrassTransformNext;
	m_GrassTransformNext += count;
	return base;
}

void MeshManager::ReleaseGrassTransforms(uint32_t base, uint32_t count)
{
	if (count == 0 || base == UINT32_MAX)
		return;

	std::scoped_lock lock(m_GrassTransformMutex);

	// Insert sorted by base and coalesce adjacent ranges.
	GrassRange merged{ base, count };
	auto it = m_FreeGrassRanges.begin();
	while (it != m_FreeGrassRanges.end() && it->base < merged.base)
		++it;

	it = m_FreeGrassRanges.insert(it, merged);

	if (it != m_FreeGrassRanges.begin()) {
		auto prev = it - 1;
		if (prev->base + prev->count == it->base) {
			prev->count += it->count;
			it = m_FreeGrassRanges.erase(it);
			it = prev;
		}
	}

	auto next = it + 1;
	if (next != m_FreeGrassRanges.end() && it->base + it->count == next->base) {
		it->count += next->count;
		m_FreeGrassRanges.erase(next);
	}
}

void MeshManager::WriteGrassTransform(uint32_t index, const float3x4& transform, const float3x4& prevTransform)
{
	TransformData data{ transform, prevTransform };
	m_GrassTransformSlots.Write(index * sizeof(TransformData), &data, sizeof(TransformData));
}

void MeshManager::Flush(nvrhi::ICommandList* commandList)
{
	// Upload dirty mesh data
	{
		auto dirtyRanges = m_GeometrySlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_GeometrySlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges)
			commandList->writeBuffer(m_MeshBuffer, mirror + offset, size, offset);
	}

	// Upload dirty properties
	{
		auto dirtyRanges = m_PropertiesSlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_PropertiesSlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges)
			commandList->writeBuffer(m_PropertiesBuffer, mirror + offset, size, offset);
	}

	// Upload dirty transforms
	{
		auto dirtyRanges = m_TransformSlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_TransformSlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges)
			commandList->writeBuffer(m_CurrentBuffer, mirror + offset, size, offset);
	}

	{
		auto dirtyRanges = m_PrevTransformSlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_PrevTransformSlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges)
			commandList->writeBuffer(m_PrevBuffer, mirror + offset, size, offset);
	}

	{
		auto dirtyRanges = m_GrassTransformSlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_GrassTransformSlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges)
			commandList->writeBuffer(m_GrassTransformBuffer, mirror + offset, size, offset);
	}
}
