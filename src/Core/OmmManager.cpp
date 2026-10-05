#include "Core/OmmManager.h"
#include "Core/Mesh/BaseMesh.h"
#include "Core/TextureManager.h"
#include "Renderer.h"
#include "Scene.h"
#include "Constants.h"
#include "Utils/Adapter.h"
#include "interop/VertexDesc.hlsli"
#include <omm-gpu-nvrhi.h>
#include <span>
#include <algorithm>

OmmManager::OmmManager()
{
	m_IsSupported = Renderer::GetSingleton()->SupportsFeature(nvrhi::Feature::RayTracingOpacityMicromap);
}

OmmManager::~OmmManager() = default;

void OmmManager::EnsureGpuBaker(nvrhi::ICommandList* commandList)
{
	if (!m_GpuBaker && m_IsSupported && commandList) {
		auto* device = Renderer::GetSingleton()->GetDevice();
		m_GpuBaker = std::make_unique<omm::GpuBakeNvrhi>(device, commandList, false);
	}
}

std::shared_ptr<OmmResource> OmmManager::TryGetOfflineResource(RE::BSTriShape* triShape, nvrhi::ICommandList* commandList)
{
	auto* extraData = Util::Adapter::GetBinaryExtraData(triShape, Constants::ExtraData::OMMData);
	if (!extraData || !extraData->value || extraData->size < sizeof(uint32_t) * 2) {
		return nullptr;
	}

	std::span<const uint8_t> payloadSpan(static_cast<const uint8_t*>(extraData->value), extraData->size);

	if (payloadSpan.size() < sizeof(OmmPayloadHeader)) {
		return nullptr;
	}

	OmmPayloadHeader header{};
	std::memcpy(&header, payloadSpan.data(), sizeof(OmmPayloadHeader));
	if (!header.IsValid()) {
		return nullptr;
	}

	const size_t expectedSize = sizeof(OmmPayloadHeader)
		+ header.indexByteSize
		+ header.descArrayByteSize
		+ header.descHistogramByteSize
		+ header.indexHistogramByteSize
		+ header.ommArrayByteSize;

	if (payloadSpan.size() < expectedSize) {
		return nullptr;
	}

	// Fast cache check by 64-bit content hash
	if (header.contentHash != 0) {
		std::scoped_lock lock(m_CacheMutex);
		auto it = m_Cache.find(header.contentHash);
		if (it != m_Cache.end()) {
			return it->second;
		}
	}

	const uint8_t* ptr = payloadSpan.data() + sizeof(OmmPayloadHeader);

	std::span<const uint16_t> triangleIndices;
	if (header.indexByteSize > 0) {
		triangleIndices = std::span<const uint16_t>(reinterpret_cast<const uint16_t*>(ptr), header.indexByteSize / sizeof(uint16_t));
		ptr += header.indexByteSize;
	}

	std::span<const OmmDescEntry> descArray;
	if (header.descArrayByteSize > 0) {
		descArray = std::span<const OmmDescEntry>(reinterpret_cast<const OmmDescEntry*>(ptr), header.descArrayCount);
		ptr += header.descArrayByteSize;
	}

	std::span<const OmmUsageEntry> descHistogram;
	if (header.descHistogramByteSize > 0) {
		descHistogram = std::span<const OmmUsageEntry>(reinterpret_cast<const OmmUsageEntry*>(ptr), header.descHistogramCount);
		ptr += header.descHistogramByteSize;
	}

	std::span<const OmmUsageEntry> indexHistogram;
	if (header.indexHistogramByteSize > 0) {
		indexHistogram = std::span<const OmmUsageEntry>(reinterpret_cast<const OmmUsageEntry*>(ptr), header.indexHistogramCount);
		ptr += header.indexHistogramByteSize;
	}

	std::span<const uint8_t> ommBitstream;
	if (header.ommArrayByteSize > 0) {
		ommBitstream = std::span<const uint8_t>(ptr, header.ommArrayByteSize);
	}

	if (triangleIndices.empty() || ommBitstream.empty() || descArray.empty() || descHistogram.empty()) {
		return nullptr;
	}

	auto* device = Renderer::GetSingleton()->GetDevice();
	if (!device) {
		return nullptr;
	}

	// 1. Allocate & Upload raw OMM Bitstream
	auto bitstreamDesc = nvrhi::BufferDesc()
		.setByteSize(ommBitstream.size_bytes())
		.setIsVolatile(false)
		.setInitialState(nvrhi::ResourceStates::OpacityMicromapBuildInput)
		.setKeepInitialState(true)
		.setDebugName("OMM_Bitstream");
	auto bitstreamBuffer = device->createBuffer(bitstreamDesc);
	if (!bitstreamBuffer) {
		logger::error("OmmManager: Failed to allocate bitstream buffer ({} bytes)", ommBitstream.size_bytes());
		return nullptr;
	}
	commandList->writeBuffer(bitstreamBuffer, ommBitstream.data(), ommBitstream.size_bytes());

	// 2. Allocate & Upload Per-OMM Descriptors
	auto descsDesc = nvrhi::BufferDesc()
		.setByteSize(descArray.size_bytes())
		.setIsVolatile(false)
		.setInitialState(nvrhi::ResourceStates::OpacityMicromapBuildInput)
		.setKeepInitialState(true)
		.setDebugName("OMM_PerOmmDescs");
	auto perOmmDescsBuffer = device->createBuffer(descsDesc);
	if (!perOmmDescsBuffer) {
		logger::error("OmmManager: Failed to allocate per-OMM descs buffer ({} bytes)", descArray.size_bytes());
		return nullptr;
	}
	commandList->writeBuffer(perOmmDescsBuffer, descArray.data(), descArray.size_bytes());

	// 3. Allocate & Upload Triangle Index Buffer
	auto indexDesc = nvrhi::BufferDesc()
		.setByteSize(triangleIndices.size_bytes())
		.setIsVolatile(false)
		.setInitialState(nvrhi::ResourceStates::AccelStructBuildInput)
		.setKeepInitialState(true)
		.setDebugName("OMM_IndexBuffer");
	auto indexBuffer = device->createBuffer(indexDesc);
	if (!indexBuffer) {
		logger::error("OmmManager: Failed to allocate index buffer ({} bytes)", triangleIndices.size_bytes());
		return nullptr;
	}
	commandList->writeBuffer(indexBuffer, triangleIndices.data(), triangleIndices.size_bytes());

	// 4. Build Opacity Micromap Array
	nvrhi::rt::OpacityMicromapDesc ommDesc;
	ommDesc.debugName = std::format("OMM_{:016X}", header.contentHash);
	ommDesc.flags = nvrhi::rt::OpacityMicromapBuildFlags::FastTrace;
	ommDesc.inputBuffer = bitstreamBuffer;
	ommDesc.inputBufferOffset = 0;
	ommDesc.perOmmDescs = perOmmDescsBuffer;
	ommDesc.perOmmDescsOffset = 0;

	for (const auto& u : descHistogram) {
		nvrhi::rt::OpacityMicromapUsageCount count;
		count.count = u.count;
		count.subdivisionLevel = u.subdivisionLevel;
		count.format = static_cast<nvrhi::rt::OpacityMicromapFormat>(u.format);
		ommDesc.counts.push_back(count);
	}

	auto opacityMicromap = device->createOpacityMicromap(ommDesc);
	if (!opacityMicromap) {
		logger::error("OmmManager: createOpacityMicromap failed for hash 0x{:016X}", header.contentHash);
		return nullptr;
	}

	commandList->buildOpacityMicromap(opacityMicromap, ommDesc);

	// 5. Build geometry attachment histogram
	std::vector<nvrhi::rt::OpacityMicromapUsageCount> indexHisto;
	indexHisto.reserve(indexHistogram.size());
	for (const auto& u : indexHistogram) {
		nvrhi::rt::OpacityMicromapUsageCount count;
		count.count = u.count;
		count.subdivisionLevel = u.subdivisionLevel;
		count.format = static_cast<nvrhi::rt::OpacityMicromapFormat>(u.format);
		indexHisto.push_back(count);
	}

	auto res = std::make_shared<OmmResource>();
	res->bitstreamBuffer = bitstreamBuffer;
	res->perOmmDescsBuffer = perOmmDescsBuffer;
	res->indexBuffer = indexBuffer;
	res->opacityMicromap = opacityMicromap;
	res->indexHistogram = std::move(indexHisto);
	res->contentHash = header.contentHash;

	if (header.contentHash != 0) {
		std::scoped_lock lock(m_CacheMutex);
		m_Cache[header.contentHash] = res;
	}

	return res;
}

std::shared_ptr<OmmResource> OmmManager::GetOrCreate(RE::BSTriShape* triShape, BaseMesh* mesh, nvrhi::ICommandList* commandList)
{
	const auto& settings = Scene::GetSingleton()->m_Settings.RaytracingSettings;
	if (!m_IsSupported || !settings.EnableOMM || !triShape || !commandList) {
		return nullptr;
	}

	// 1. Try offline baked binary extra data ("CERT::OMM") first
	if (auto offlineRes = TryGetOfflineResource(triShape, commandList)) {
		return offlineRes;
	}

	// 2. Check if runtime OMM fallback is enabled
	if (!settings.EnableRuntimeOMMFallback || !mesh) {
		return nullptr;
	}

	if (mesh->GetType() == BaseMesh::Type::Instanced) {
		return nullptr;
	}

	// 3. Determine alpha test threshold or blending cutoff
	float alphaCutoff = 0.5f;
	auto runtimeData = Util::Adapter::GetGeometryRuntimeData(triShape);
	auto alphaProperty = runtimeData.alphaProperty;
	if (alphaProperty) {
#if defined(SKYRIM)
		if (alphaProperty->GetAlphaTesting()) {
			alphaCutoff = alphaProperty->alphaThreshold > 0 ? (static_cast<float>(alphaProperty->alphaThreshold) / 255.0f) : 0.5f;
		} else if (alphaProperty->GetAlphaBlending()) {
			alphaCutoff = 1.0f / 255.0f;
		} else {
			return nullptr;
		}
#elif defined(FALLOUT4)
		const auto flags = alphaProperty->flags.flags;
		if (flags & static_cast<uint16_t>(RE::NiAlphaPropertyFlags::kAlphaTest)) {
			alphaCutoff = static_cast<float>(static_cast<uint8_t>(alphaProperty->alphaTestRef)) / 255.0f;
			if (alphaCutoff <= 0.0f) alphaCutoff = 0.5f;
		} else if (flags & static_cast<uint16_t>(RE::NiAlphaPropertyFlags::kAlphaBlend)) {
			alphaCutoff = 1.0f / 255.0f;
		} else {
			return nullptr;
		}
#endif
	} else {
		return nullptr;
	}

	// 4. Compute 64-bit cache key
	uint64_t runtimeKey = ComputeRuntimeKey(triShape, mesh, alphaCutoff);
	if (runtimeKey == 0) {
		return nullptr;
	}

	// 5. Check cache
	{
		std::scoped_lock lock(m_CacheMutex);
		auto it = m_Cache.find(runtimeKey);
		if (it != m_Cache.end()) {
			return it->second;
		}
	}

	// 6. If already in flight, return nullptr for frame 0 (Any-Hit will handle it)
	{
		std::scoped_lock lock(m_PendingMutex);
		if (m_InFlightKeys.contains(runtimeKey)) {
			return nullptr;
		}
	}

	// 7. Schedule runtime GPU bake
	if (ScheduleRuntimeGpuBake(triShape, mesh, commandList, runtimeKey, alphaCutoff)) {
		std::scoped_lock lock(m_PendingMutex);
		m_InFlightKeys.insert(runtimeKey);
	}

	return nullptr;
}

bool OmmManager::ScheduleRuntimeGpuBake(RE::BSTriShape* triShape, BaseMesh* mesh, nvrhi::ICommandList* commandList, uint64_t runtimeKey, float alphaCutoff)
{
	if (!mesh || mesh->GetGeometryEntries().empty()) {
		return false;
	}

	EnsureGpuBaker(commandList);
	if (!m_GpuBaker) {
		return false;
	}

	// Diffuse texture resolution
	nvrhi::TextureHandle diffuseTexture = nullptr;
	if (mesh->GetMaterial()) {
		diffuseTexture = mesh->GetMaterial()->GetDiffuseTexture();
	}

	if (!diffuseTexture) {
		auto& textureManager = Scene::GetSingleton()->GetSceneGraph()->GetTextureManager();
		auto shaderProperty = Util::Adapter::GetGeometryRuntimeData(triShape).shaderProperty;
		if (shaderProperty && static_cast<RE::BSShaderMaterial::Type>(shaderProperty->GetMaterialType()) == RE::BSShaderMaterial::Type::kLighting && shaderProperty->material) {
			auto lightingMaterial = reinterpret_cast<RE::BSLightingShaderMaterialBase*>(shaderProperty->material);
			if (lightingMaterial->diffuseTexture) {
				auto* rendererTexture = Util::Adapter::GetRendererTexture(lightingMaterial->diffuseTexture.get());
				if (rendererTexture) {
					diffuseTexture = textureManager->GetTextureHandle(rendererTexture);
				}
			}
		}
	}

	if (!diffuseTexture) {
		return false;
	}

	// Geometry info
	const auto& geom = mesh->GetGeometryEntries()[0].desc.geometryData.triangles;
	if (!geom.indexBuffer || !geom.vertexBuffer || geom.indexCount == 0 || geom.vertexCount == 0) {
		return false;
	}

	VertexDesc vertexDesc(mesh->GetVertexDescRaw());
	if (!vertexDesc.HasFlag(VertexFlags::UV)) {
		return false;
	}

	const uint32_t uvOffset = vertexDesc.GetAttributeOffset(VertexAttribute::Texcoord0);
	const uint16_t vertexStride = vertexDesc.GetVertexSize();

	omm::GpuBakeNvrhi::Input input;
	input.operation = omm::GpuBakeNvrhi::Operation::SetupAndBake;
	input.alphaTexture = diffuseTexture;
	input.alphaTextureChannel = 3;
	input.alphaCutoff = alphaCutoff;
	input.alphaCutoffGreater = omm::OpacityState::Opaque;
	input.alphaCutoffLessEqual = omm::OpacityState::Transparent;
	input.bilinearFilter = true;
	input.enableLevelLineIntersection = true;
	input.sampleMode = nvrhi::SamplerAddressMode::Wrap;
	input.texCoordFormat = nvrhi::Format::R16_FLOAT;
	input.texCoordBuffer = geom.vertexBuffer;
	input.texCoordBufferOffsetInBytes = static_cast<uint32_t>(geom.vertexOffset + uvOffset);
	input.texCoordStrideInBytes = vertexStride;
	input.indexBuffer = geom.indexBuffer;
	input.indexBufferOffsetInBytes = static_cast<uint32_t>(geom.indexOffset);
	input.indexOffset = 0;
	input.numIndices = geom.indexCount;
	input.maxSubdivisionLevel = std::clamp(Constants::RuntimeOMMSubdivision, 1u, 12u);
	input.format = static_cast<nvrhi::rt::OpacityMicromapFormat>(Constants::RuntimeOMMFormat);
	input.dynamicSubdivisionScale = 0.5f;
	input.minimalMemoryMode = false;
	input.enableStats = false;
	input.enableSpecialIndices = true;
	input.force32BitIndices = false;
	input.enableTexCoordDeduplication = true;
	input.computeOnly = true;

	omm::GpuBakeNvrhi::PreDispatchInfo preDispatchInfo{};
	m_GpuBaker->GetPreDispatchInfo(input, preDispatchInfo);

	auto* device = Renderer::GetSingleton()->GetDevice();

	PendingOmmBakeTask task;
	task.runtimeKey = runtimeKey;
	task.targetMesh = mesh;
	task.submitFence = Renderer::GetSingleton()->GetLastSubmittedFence();

	// Allocate GPU output buffers
	task.ommArrayBuffer = device->createBuffer(nvrhi::BufferDesc()
		.setByteSize(std::max<size_t>(preDispatchInfo.ommArrayBufferSize, 4u))
		.setCanHaveUAVs(true)
		.setCanHaveRawViews(true)
		.setIsAccelStructBuildInput(true)
		.setInitialState(nvrhi::ResourceStates::Common)
		.setDebugName("OMM_Runtime_ArrayBuffer"));

	task.ommDescBuffer = device->createBuffer(nvrhi::BufferDesc()
		.setByteSize(std::max<size_t>(preDispatchInfo.ommDescBufferSize, 4u))
		.setCanHaveUAVs(true)
		.setCanHaveRawViews(true)
		.setIsAccelStructBuildInput(true)
		.setInitialState(nvrhi::ResourceStates::Common)
		.setDebugName("OMM_Runtime_DescBuffer"));

	task.ommIndexBuffer = device->createBuffer(nvrhi::BufferDesc()
		.setByteSize(std::max<size_t>(preDispatchInfo.ommIndexBufferSize, 4u))
		.setCanHaveUAVs(true)
		.setCanHaveRawViews(true)
		.setIsAccelStructBuildInput(true)
		.setInitialState(nvrhi::ResourceStates::Common)
		.setDebugName("OMM_Runtime_IndexBuffer"));

	task.ommDescArrayHistogramBuffer = device->createBuffer(nvrhi::BufferDesc()
		.setByteSize(std::max<size_t>(preDispatchInfo.ommDescArrayHistogramSize, 4u))
		.setCanHaveUAVs(true)
		.setCanHaveRawViews(true)
		.setInitialState(nvrhi::ResourceStates::Common)
		.setDebugName("OMM_Runtime_DescArrayHistogramBuffer"));

	task.ommIndexHistogramBuffer = device->createBuffer(nvrhi::BufferDesc()
		.setByteSize(std::max<size_t>(preDispatchInfo.ommIndexHistogramSize, 4u))
		.setCanHaveUAVs(true)
		.setCanHaveRawViews(true)
		.setInitialState(nvrhi::ResourceStates::Common)
		.setDebugName("OMM_Runtime_IndexHistogramBuffer"));

	task.ommPostDispatchInfoBuffer = device->createBuffer(nvrhi::BufferDesc()
		.setByteSize(std::max<size_t>(preDispatchInfo.ommPostDispatchInfoBufferSize, 4u))
		.setCanHaveUAVs(true)
		.setCanHaveRawViews(true)
		.setInitialState(nvrhi::ResourceStates::Common)
		.setDebugName("OMM_Runtime_PostDispatchInfoBuffer"));

	// Allocate CPU readback staging buffers
	task.descArrayHistogramReadback = device->createBuffer(nvrhi::BufferDesc()
		.setByteSize(task.ommDescArrayHistogramBuffer->getDesc().byteSize)
		.setCpuAccess(nvrhi::CpuAccessMode::Read)
		.setInitialState(nvrhi::ResourceStates::CopyDest)
		.setKeepInitialState(true)
		.setDebugName("OMM_Runtime_DescHistoReadback"));

	task.indexHistogramReadback = device->createBuffer(nvrhi::BufferDesc()
		.setByteSize(task.ommIndexHistogramBuffer->getDesc().byteSize)
		.setCpuAccess(nvrhi::CpuAccessMode::Read)
		.setInitialState(nvrhi::ResourceStates::CopyDest)
		.setKeepInitialState(true)
		.setDebugName("OMM_Runtime_IndexHistoReadback"));

	task.postDispatchInfoReadback = device->createBuffer(nvrhi::BufferDesc()
		.setByteSize(task.ommPostDispatchInfoBuffer->getDesc().byteSize)
		.setCpuAccess(nvrhi::CpuAccessMode::Read)
		.setInitialState(nvrhi::ResourceStates::CopyDest)
		.setKeepInitialState(true)
		.setDebugName("OMM_Runtime_PostInfoReadback"));

	if (!task.ommArrayBuffer || !task.ommDescBuffer || !task.ommIndexBuffer ||
		!task.ommDescArrayHistogramBuffer || !task.ommIndexHistogramBuffer || !task.ommPostDispatchInfoBuffer ||
		!task.descArrayHistogramReadback || !task.indexHistogramReadback || !task.postDispatchInfoReadback) {
		logger::error("OmmManager: Failed to allocate GPU buffers for runtime bake");
		return false;
	}

	commandList->beginTrackingBufferState(task.ommArrayBuffer, nvrhi::ResourceStates::Common);
	commandList->beginTrackingBufferState(task.ommDescBuffer, nvrhi::ResourceStates::Common);
	commandList->beginTrackingBufferState(task.ommIndexBuffer, nvrhi::ResourceStates::Common);
	commandList->beginTrackingBufferState(task.ommDescArrayHistogramBuffer, nvrhi::ResourceStates::Common);
	commandList->beginTrackingBufferState(task.ommIndexHistogramBuffer, nvrhi::ResourceStates::Common);
	commandList->beginTrackingBufferState(task.ommPostDispatchInfoBuffer, nvrhi::ResourceStates::Common);

	omm::GpuBakeNvrhi::Buffers outBuffers;
	outBuffers.ommArrayBuffer = task.ommArrayBuffer;
	outBuffers.ommDescBuffer = task.ommDescBuffer;
	outBuffers.ommIndexBuffer = task.ommIndexBuffer;
	outBuffers.ommDescArrayHistogramBuffer = task.ommDescArrayHistogramBuffer;
	outBuffers.ommIndexHistogramBuffer = task.ommIndexHistogramBuffer;
	outBuffers.ommPostDispatchInfoBuffer = task.ommPostDispatchInfoBuffer;

	m_GpuBaker->Dispatch(commandList, input, outBuffers);

	// Copy histograms to staging readback buffers
	commandList->copyBuffer(task.descArrayHistogramReadback, 0, task.ommDescArrayHistogramBuffer, 0, task.descArrayHistogramReadback->getDesc().byteSize);
	commandList->copyBuffer(task.indexHistogramReadback, 0, task.ommIndexHistogramBuffer, 0, task.indexHistogramReadback->getDesc().byteSize);
	commandList->copyBuffer(task.postDispatchInfoReadback, 0, task.ommPostDispatchInfoBuffer, 0, task.postDispatchInfoReadback->getDesc().byteSize);

	task.eventQuery = device->createEventQuery();
	device->setEventQuery(task.eventQuery, nvrhi::CommandQueue::Graphics);

	std::scoped_lock lock(m_PendingMutex);
	m_PendingTasks.push_back(std::move(task));
	return true;
}

void OmmManager::ProcessPendingBakes(nvrhi::ICommandList* commandList, uint64_t completedFence)
{
	std::vector<PendingOmmBakeTask> readyTasks;
	{
		std::scoped_lock lock(m_PendingMutex);
		auto* device = Renderer::GetSingleton()->GetDevice();

		auto it = m_PendingTasks.begin();
		while (it != m_PendingTasks.end()) {
			bool isReady = false;
			if (it->eventQuery && device->pollEventQuery(it->eventQuery)) {
				isReady = true;
			} else if (completedFence > it->submitFence) {
				isReady = true;
			}

			if (isReady) {
				readyTasks.push_back(std::move(*it));
				m_InFlightKeys.erase(readyTasks.back().runtimeKey);
				it = m_PendingTasks.erase(it);
			} else {
				++it;
			}
		}
	}

	if (readyTasks.empty()) {
		return;
	}

	auto* device = Renderer::GetSingleton()->GetDevice();

	for (auto& task : readyTasks) {
		std::vector<nvrhi::rt::OpacityMicromapUsageCount> descUsages;
		void* pDescData = device->mapBuffer(task.descArrayHistogramReadback, nvrhi::CpuAccessMode::Read);
		if (pDescData) {
			omm::GpuBakeNvrhi::ReadUsageDescBuffer(pDescData, task.descArrayHistogramReadback->getDesc().byteSize, descUsages);
			device->unmapBuffer(task.descArrayHistogramReadback);
		}

		std::vector<nvrhi::rt::OpacityMicromapUsageCount> indexUsages;
		void* pIndexData = device->mapBuffer(task.indexHistogramReadback, nvrhi::CpuAccessMode::Read);
		if (pIndexData) {
			omm::GpuBakeNvrhi::ReadUsageDescBuffer(pIndexData, task.indexHistogramReadback->getDesc().byteSize, indexUsages);
			device->unmapBuffer(task.indexHistogramReadback);
		}

		if (descUsages.empty()) {
			logger::debug("OmmManager: Bake for key 0x{:016X} produced 0 descriptors (mesh may be fully opaque or empty).", task.runtimeKey);
			continue;
		}

		nvrhi::rt::OpacityMicromapDesc ommDesc;
		ommDesc.debugName = std::format("OMM_Runtime_{:016X}", task.runtimeKey);
		ommDesc.flags = nvrhi::rt::OpacityMicromapBuildFlags::FastTrace;
		ommDesc.inputBuffer = task.ommArrayBuffer;
		ommDesc.inputBufferOffset = 0;
		ommDesc.perOmmDescs = task.ommDescBuffer;
		ommDesc.perOmmDescsOffset = 0;
		ommDesc.counts = descUsages;

		auto opacityMicromap = device->createOpacityMicromap(ommDesc);
		if (!opacityMicromap) {
			logger::error("OmmManager: createOpacityMicromap failed for runtime key 0x{:016X}", task.runtimeKey);
			continue;
		}

		commandList->buildOpacityMicromap(opacityMicromap, ommDesc);

		auto res = std::make_shared<OmmResource>();
		res->bitstreamBuffer = task.ommArrayBuffer;
		res->perOmmDescsBuffer = task.ommDescBuffer;
		res->indexBuffer = task.ommIndexBuffer;
		res->opacityMicromap = opacityMicromap;
		res->indexHistogram = std::move(indexUsages);
		res->contentHash = task.runtimeKey;

		{
			std::scoped_lock lock(m_CacheMutex);
			m_Cache[task.runtimeKey] = res;
		}

		if (task.targetMesh) {
			task.targetMesh->AttachOpacityMicromap(res);
		}
	}
}

void OmmManager::CancelPendingBake(BaseMesh* mesh)
{
	std::scoped_lock lock(m_PendingMutex);
	for (auto& task : m_PendingTasks) {
		if (task.targetMesh == mesh) {
			task.targetMesh = nullptr;
		}
	}
}

uint64_t OmmManager::ComputeRuntimeKey(RE::BSTriShape* triShape, BaseMesh* mesh, float alphaCutoff)
{
	uint64_t hash = 14695981039346656037ULL;
	auto hashBytes = [&hash](const void* data, size_t size) {
		const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i) {
			hash ^= static_cast<uint64_t>(ptr[i]);
			hash *= 1099511628211ULL;
		}
	};

	if (mesh) {
		uint64_t vdesc = mesh->GetVertexDescRaw();
		hashBytes(&vdesc, sizeof(vdesc));

		const auto& entries = mesh->GetGeometryEntries();
		if (!entries.empty()) {
			const auto& geom = entries[0].desc.geometryData.triangles;
			hashBytes(&geom.indexCount, sizeof(geom.indexCount));
			hashBytes(&geom.vertexCount, sizeof(geom.vertexCount));
			void* ib = geom.indexBuffer;
			void* vb = geom.vertexBuffer;
			hashBytes(&ib, sizeof(ib));
			hashBytes(&vb, sizeof(vb));
		}
	}

	hashBytes(&triShape, sizeof(triShape));
	hashBytes(&alphaCutoff, sizeof(alphaCutoff));

	hashBytes(&Constants::RuntimeOMMSubdivision, sizeof(Constants::RuntimeOMMSubdivision));
	hashBytes(&Constants::RuntimeOMMFormat, sizeof(Constants::RuntimeOMMFormat));

	return hash;
}
