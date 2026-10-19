#include "VRAMTracker.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Core/Mesh/BaseMesh.h"
#include "Core/BLASManager.h"
#include "Core/BLASCluster.h"
#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "Pass/Raytracing/Common/LightTLAS.h"
#include "Types/TopLevelAS.h"

#include <dxgi1_4.h>
#include <format>
#include <unordered_set>
#include <magic_enum/magic_enum.hpp>

namespace VRAMTracker
{
	static void CollectDXGIMemory(eastl::vector<eastl::pair<eastl::string, uint64_t>>& entries)
	{
		auto* renderer = Renderer::GetSingleton();
		if (!renderer)
			return;

		IDXGIAdapter3* pAdapter3 = nullptr;

		if (auto* d3d12Device = renderer->GetNativeD3D12Device()) {
			LUID luid = d3d12Device->GetAdapterLuid();
			IDXGIFactory4* pFactory = nullptr;
			if (SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&pFactory)))) {
				IDXGIAdapter* pAdapter = nullptr;
				if (SUCCEEDED(pFactory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&pAdapter)))) {
					pAdapter->QueryInterface(IID_PPV_ARGS(&pAdapter3));
					pAdapter->Release();
				}
				pFactory->Release();
			}
		}

		if (!pAdapter3) {
			if (auto* d3d11Device = renderer->GetNativeD3D11Device()) {
				IDXGIDevice* pDxgiDevice = nullptr;
				if (SUCCEEDED(d3d11Device->QueryInterface(IID_PPV_ARGS(&pDxgiDevice)))) {
					IDXGIAdapter* pAdapter = nullptr;
					if (SUCCEEDED(pDxgiDevice->GetAdapter(&pAdapter))) {
						pAdapter->QueryInterface(IID_PPV_ARGS(&pAdapter3));
						pAdapter->Release();
					}
					pDxgiDevice->Release();
				}
			}
		}

		if (pAdapter3) {
			DXGI_QUERY_VIDEO_MEMORY_INFO localInfo{};
			if (SUCCEEDED(pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localInfo))) {
				entries.emplace_back("Local Video Memory Budget", localInfo.Budget);
				entries.emplace_back("Local Video Memory Current Usage", localInfo.CurrentUsage);
				entries.emplace_back("Local Video Memory Available For Reservation", localInfo.AvailableForReservation);
				entries.emplace_back("Local Video Memory Current Reservation", localInfo.CurrentReservation);
			}

			DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalInfo{};
			if (SUCCEEDED(pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalInfo))) {
				entries.emplace_back("Non-Local Memory Budget", nonLocalInfo.Budget);
				entries.emplace_back("Non-Local Memory Current Usage", nonLocalInfo.CurrentUsage);
				entries.emplace_back("Non-Local Memory Available For Reservation", nonLocalInfo.AvailableForReservation);
				entries.emplace_back("Non-Local Memory Current Reservation", nonLocalInfo.CurrentReservation);
			}

			pAdapter3->Release();
		}
	}

	static uint64_t CollectRenderTargets(eastl::vector<eastl::pair<eastl::string, uint64_t>>& entries)
	{
		auto* renderer = Renderer::GetSingleton();
		if (!renderer)
			return 0;

		auto* device = renderer->GetDevice();
		if (!device)
			return 0;

		uint64_t totalBytes = 0;
		auto& rtm = renderer->RenderTargetManager();

		for (size_t i = 0; i < static_cast<size_t>(RenderTarget::Total); ++i) {
			const auto& rt = rtm.m_Textures[i];
			if (rt.handle) {
				const auto name = magic_enum::enum_name(static_cast<RenderTarget>(i));
				uint64_t byteSize = device->getTextureMemoryRequirements(rt.handle).size;
				entries.emplace_back(name.data(), byteSize);
				totalBytes += byteSize;
			}
		}

		// Engine-allocated shared textures (wrapped, do not consume additional VRAM)
		if (auto* depth = renderer->GetDepthTexture()) {
			uint64_t sz = device->getTextureMemoryRequirements(depth).size;
			entries.emplace_back("[Engine Shared] Depth (Non-additive)", sz);
		}
		if (auto* mv = renderer->GetMotionVectorTexture()) {
			uint64_t sz = device->getTextureMemoryRequirements(mv).size;
			entries.emplace_back("[Engine Shared] MotionVectors (Non-additive)", sz);
		}
		if (auto* wd = renderer->GetWaterDisplacementTexture()) {
			uint64_t sz = device->getTextureMemoryRequirements(wd).size;
			entries.emplace_back("[Engine Shared] WaterDisplacement (Non-additive)", sz);
		}

		if (renderer->m_GBufferOutput) {
			auto* gb = renderer->m_GBufferOutput.get();
			auto addTex = [&](const char* label, nvrhi::ITexture* tex) {
				if (tex) {
					uint64_t sz = device->getTextureMemoryRequirements(tex).size;
					entries.emplace_back(label, sz);
					totalBytes += sz;
				}
			};
			addTex("GBuffer Output Depth", gb->depth);
			addTex("GBuffer Output MotionVectors", gb->motionVectors);
			addTex("GBuffer Output Albedo", gb->albedo);
			addTex("GBuffer Output NormalRoughness", gb->normalRoughness);
			addTex("GBuffer Output EmissiveMetallic", gb->emissiveMetallic);
		}

		if (renderer->m_StablePlanes) {
			auto* sp = renderer->m_StablePlanes.get();
			if (sp->header) {
				uint64_t sz = device->getTextureMemoryRequirements(sp->header).size;
				entries.emplace_back("Stable Planes Header", sz);
				totalBytes += sz;
			}
			if (sp->buffer) {
				uint64_t sz = sp->buffer->getDesc().byteSize;
				entries.emplace_back("Stable Planes Buffer", sz);
				totalBytes += sz;
			}
			if (sp->stableRadiance) {
				uint64_t sz = device->getTextureMemoryRequirements(sp->stableRadiance).size;
				entries.emplace_back("Stable Planes Radiance", sz);
				totalBytes += sz;
			}
		}

		if (renderer->m_ReSTIRGIResources) {
			auto* rgi = renderer->m_ReSTIRGIResources.get();
			auto addBuf = [&](const char* label, nvrhi::IBuffer* buf) {
				if (buf) {
					uint64_t sz = buf->getDesc().byteSize;
					entries.emplace_back(label, sz);
					totalBytes += sz;
				}
			};
			auto addTex = [&](const char* label, nvrhi::ITexture* tex) {
				if (tex) {
					uint64_t sz = device->getTextureMemoryRequirements(tex).size;
					entries.emplace_back(label, sz);
					totalBytes += sz;
				}
			};
			addBuf("ReSTIR GI Reservoirs", rgi->reservoirBuffer);
			addBuf("ReSTIR GI Neighbor Offsets", rgi->neighborOffsetBuffer);
			addBuf("ReSTIR GI Surface Data", rgi->surfaceDataBuffer);
			addTex("ReSTIR GI Secondary PositionNormal", rgi->secondaryGBufferPositionNormal);
			addTex("ReSTIR GI Secondary Radiance", rgi->secondaryGBufferRadiance);
			addTex("ReSTIR GI Secondary DiffuseAlbedo", rgi->secondaryGBufferDiffuseAlbedo);
			addTex("ReSTIR GI Secondary SpecularF0Roughness", rgi->secondaryGBufferSpecularF0Roughness);
			addTex("ReSTIR GI Prev Linear Depth", rgi->prevGBufferDepth);
			addTex("ReSTIR GI Prev Normals", rgi->prevGBufferNormals);
		}

		entries.emplace_back("Total Render Targets & Buffers", totalBytes);
		return totalBytes;
	}

	static uint64_t CollectOwnedMeshBuffers(eastl::vector<eastl::pair<eastl::string, uint64_t>>& entries)
	{
		auto* scene = Scene::GetSingleton();
		if (!scene)
			return 0;

		auto* sceneGraph = scene->GetSceneGraph();
		if (!sceneGraph)
			return 0;

		uint64_t totalBytes = 0;

		BaseMesh::OwnedBufferStats meshStats{};
		for (const auto& [triShape, mesh] : sceneGraph->GetDirectMeshes()) {
			if (mesh) {
				mesh->CollectOwnedBuffers(meshStats);
			}
		}

		if (meshStats.skinnedMeshCount > 0) {
			std::string label = std::format("Skinned Live Vertex ({} meshes)", meshStats.skinnedMeshCount);
			entries.emplace_back(label.c_str(), meshStats.skinnedLiveVertexBytes);
			totalBytes += meshStats.skinnedLiveVertexBytes;

			entries.emplace_back("Skinned Prev Position", meshStats.skinnedPrevPositionBytes);
			totalBytes += meshStats.skinnedPrevPositionBytes;
		}

		if (meshStats.dynamicMeshCount > 0) {
			std::string label = std::format("Dynamic Live ({} meshes)", meshStats.dynamicMeshCount);
			entries.emplace_back(label.c_str(), meshStats.dynamicLiveBytes);
			totalBytes += meshStats.dynamicLiveBytes;

			entries.emplace_back("Dynamic Original Rest/Morph", meshStats.dynamicOriginalBytes);
			totalBytes += meshStats.dynamicOriginalBytes;
		}

		if (meshStats.landLODMeshCount > 0) {
			std::string label = std::format("Land LOD Live Vertex ({} meshes)", meshStats.landLODMeshCount);
			entries.emplace_back(label.c_str(), meshStats.landLODLiveVertexBytes);
			totalBytes += meshStats.landLODLiveVertexBytes;
		}

		auto& meshManager = sceneGraph->GetMeshManager();
		if (meshManager) {
			auto addBuf = [&](const char* label, nvrhi::IBuffer* buf) {
				if (buf) {
					uint64_t sz = buf->getDesc().byteSize;
					entries.emplace_back(label, sz);
					totalBytes += sz;
				}
			};
			addBuf("MeshManager Metadata Buffer", meshManager->GetMeshBuffer());
			addBuf("MeshManager Properties Buffer", meshManager->GetPropertiesBuffer());
			addBuf("MeshManager Composite Transform Buffer", meshManager->GetTransformBuffer());
			addBuf("MeshManager Current Transform Buffer", meshManager->GetCurrentTransformBuffer());
			addBuf("MeshManager Prev Transform Buffer", meshManager->GetPrevTransformBuffer());
			addBuf("MeshManager Instance Transform Buffer", meshManager->GetInstanceTransformBuffer());
		}

		auto addRingBuf = [&](const char* label, nvrhi::IBuffer* buf) {
			if (buf) {
				uint64_t sz = buf->getDesc().byteSize * Constants::MAX_FRAMES_IN_FLIGHT;
				entries.emplace_back(label, sz);
				totalBytes += sz;
			}
		};
		addRingBuf("Light Data RingBuffer", sceneGraph->GetLightBuffer());
		addRingBuf("Mesh Slot Remap RingBuffer", sceneGraph->GetMeshSlotRemapBuffer());
		addRingBuf("Instance Data RingBuffer", sceneGraph->GetInstanceBuffer());
		addRingBuf("Instance Bound RingBuffer", sceneGraph->GetInstanceBoundBuffer());
		addRingBuf("Instance Light List RingBuffer", sceneGraph->GetInstanceLightList());
		addRingBuf("Instance Light Counter RingBuffer", sceneGraph->GetInstanceLightCounter());

		if (auto* matBuf = sceneGraph->GetMaterialBuffer()) {
			uint64_t sz = matBuf->getDesc().byteSize;
			entries.emplace_back("Material Table Buffer", sz);
			totalBytes += sz;
		}

		entries.emplace_back("Total Owned Mesh & System Buffers", totalBytes);
		return totalBytes;
	}

	static uint64_t CollectBLAS(eastl::vector<eastl::pair<eastl::string, uint64_t>>& entries)
	{
		auto* scene = Scene::GetSingleton();
		if (!scene)
			return 0;

		auto* sceneGraph = scene->GetSceneGraph();
		if (!sceneGraph)
			return 0;

		uint64_t totalBytes = 0;

		auto& blasManager = sceneGraph->GetBLASManager();
		if (blasManager) {
			uint32_t sharedCount = 0;
			uint64_t sharedBytes = 0;
			uint64_t transformBytes = 0;
			blasManager->GetBLASMemoryStats(sharedCount, sharedBytes, transformBytes);

			std::string sharedLabel = std::format("Shared BLAS Cache ({} structures)", sharedCount);
			entries.emplace_back(sharedLabel.c_str(), sharedBytes);
			totalBytes += sharedBytes;

			if (transformBytes > 0) {
				entries.emplace_back("Shared Relative Transform Buffer", transformBytes);
				totalBytes += transformBytes;
			}
		}

		uint32_t dedicatedCount = 0;
		uint64_t dedicatedBytes = 0;
		std::unordered_set<const BLASResource*> seenDedicated;

		auto inspectCluster = [&](BLASCluster* cluster) {
			if (!cluster)
				return;
			const auto& res = cluster->GetBLASResource();
			if (res && !res->m_IsShared && res->m_BLAS && seenDedicated.insert(res.get()).second) {
				dedicatedCount++;
				dedicatedBytes += res->GetAllocatedMemory();
			}
		};

		for (const auto& [refr, cluster] : sceneGraph->GetOwnerClusters()) {
			inspectCluster(cluster.get());
		}
		for (const auto& [triShape, cluster] : sceneGraph->GetOrphanClusters()) {
			inspectCluster(cluster.get());
		}
		for (const auto& [segment, cluster] : sceneGraph->GetSubIndexSegmentClusters()) {
			inspectCluster(cluster.get());
		}

		if (dedicatedCount > 0) {
			std::string dedicatedLabel = std::format("Dedicated Cluster BLAS ({} structures)", dedicatedCount);
			entries.emplace_back(dedicatedLabel.c_str(), dedicatedBytes);
			totalBytes += dedicatedBytes;
		}

		entries.emplace_back("Total BLAS Memory", totalBytes);
		return totalBytes;
	}

	static uint64_t CollectTLAS(eastl::vector<eastl::pair<eastl::string, uint64_t>>& entries)
	{
		auto* renderer = Renderer::GetSingleton();
		if (!renderer)
			return 0;

		auto* renderGraph = renderer->GetRenderGraph();
		if (!renderGraph)
			return 0;

		uint64_t totalBytes = 0;

		if (auto* sceneTLAS = renderGraph->GetPass<Pass::SceneTLAS>()) {
			auto& tlas = sceneTLAS->GetTopLevelAS();
			uint32_t instanceCount = tlas.GetCurrentInstanceCount();
			uint64_t bufferSize = tlas.GetTotalBufferSize();

			std::string label = std::format("Scene TLAS ({} active instances, {} frames in flight)", instanceCount, Constants::MAX_FRAMES_IN_FLIGHT);
			entries.emplace_back(label.c_str(), bufferSize);
			totalBytes += bufferSize;
		}

		if (auto* lightTLAS = renderGraph->GetPass<Pass::LightTLAS>()) {
			uint32_t lightCount = lightTLAS->GetTopLevelInstanceCount();
			uint64_t bufferSize = lightTLAS->GetTopLevelBufferSize();

			std::string label = std::format("Light TLAS ({} light instances)", lightCount);
			entries.emplace_back(label.c_str(), bufferSize);
			totalBytes += bufferSize;
		}

		entries.emplace_back("Total TLAS Memory", totalBytes);
		return totalBytes;
	}

	static uint64_t CollectTextures(eastl::vector<eastl::pair<eastl::string, uint64_t>>& entries)
	{
		auto* scene = Scene::GetSingleton();
		if (!scene)
			return 0;

		auto* sceneGraph = scene->GetSceneGraph();
		if (!sceneGraph)
			return 0;

		auto& textureManager = sceneGraph->GetTextureManager();
		if (!textureManager)
			return 0;

		std::scoped_lock lock(textureManager->m_TexturesMutex);
		uint32_t count = static_cast<uint32_t>(textureManager->m_Textures.size());
		uint64_t totalBytes = textureManager->GetFakeDoubledVRAMUsage();

		std::string label = std::format("Shared Game Textures ({} textures, non-additive)", count);
		entries.emplace_back(label.c_str(), totalBytes);

		return totalBytes;
	}

	static uint64_t CollectPassResources(eastl::vector<eastl::pair<eastl::string, uint64_t>>& entries)
	{
		auto* renderer = Renderer::GetSingleton();
		if (!renderer)
			return 0;

		auto* renderGraph = renderer->GetRenderGraph();
		if (!renderGraph)
			return 0;

		uint64_t totalBytes = 0;

		for (const auto& node : renderGraph->GetNodes()) {
			if (node.m_RenderPass) {
				uint64_t mem = node.m_RenderPass->GetAllocatedMemory();
				if (mem > 0) {
					entries.emplace_back(node.m_Name, mem);
					totalBytes += mem;
				}
			}
		}

		if (totalBytes > 0) {
			entries.emplace_back("Total Pass Resources", totalBytes);
		}

		return totalBytes;
	}

	void Collect(VRAMExportData& outData)
	{
		outData.clear();

		// 1. VRAM (DXGI)
		{
			eastl::vector<eastl::pair<eastl::string, uint64_t>> dxgiEntries;
			CollectDXGIMemory(dxgiEntries);
			if (!dxgiEntries.empty()) {
				outData.emplace_back("VRAM (DXGI)", eastl::move(dxgiEntries));
			}
		}

		uint64_t grandTotal = 0;

		// 2. Render Targets
		{
			eastl::vector<eastl::pair<eastl::string, uint64_t>> rtEntries;
			grandTotal += CollectRenderTargets(rtEntries);
			outData.emplace_back("Render Targets", eastl::move(rtEntries));
		}

		// 3. Owned Mesh Buffers
		{
			eastl::vector<eastl::pair<eastl::string, uint64_t>> meshEntries;
			grandTotal += CollectOwnedMeshBuffers(meshEntries);
			outData.emplace_back("Owned Mesh Buffers", eastl::move(meshEntries));
		}

		// 4. BLAS
		{
			eastl::vector<eastl::pair<eastl::string, uint64_t>> blasEntries;
			grandTotal += CollectBLAS(blasEntries);
			outData.emplace_back("BLAS", eastl::move(blasEntries));
		}

		// 5. TLAS
		{
			eastl::vector<eastl::pair<eastl::string, uint64_t>> tlasEntries;
			grandTotal += CollectTLAS(tlasEntries);
			outData.emplace_back("TLAS", eastl::move(tlasEntries));
		}

		// 6. Pass Resources (SHaRC, NRD, etc.)
		{
			eastl::vector<eastl::pair<eastl::string, uint64_t>> passEntries;
			uint64_t passTotal = CollectPassResources(passEntries);
			if (!passEntries.empty()) {
				grandTotal += passTotal;
				outData.emplace_back("Pass Resources", eastl::move(passEntries));
			}
		}

		// 7. Textures (Wrapped from game engine - non-additive)
		{
			eastl::vector<eastl::pair<eastl::string, uint64_t>> texEntries;
			CollectTextures(texEntries);
			outData.emplace_back("Textures", eastl::move(texEntries));
		}

		// 8. Summary
		{
			eastl::vector<eastl::pair<eastl::string, uint64_t>> summaryEntries;
			summaryEntries.emplace_back("Total Dedicated Raytracing VRAM", grandTotal);
			outData.emplace_back("Summary", eastl::move(summaryEntries));
		}

		logger::info("[VRAM Report] Exporting VRAM usage (Dedicated: {:.2f} MiB):", grandTotal / (1024.0 * 1024.0));
		for (const auto& [category, items] : outData) {
			logger::info("  [{}]", category.c_str());
			for (const auto& [name, value] : items) {
				if (value >= 1024 * 1024) {
					logger::info("    - {}: {:.2f} MiB ({} bytes)", name.c_str(), value / (1024.0 * 1024.0), value);
				} else if (value >= 1024) {
					logger::info("    - {}: {:.2f} KiB ({} bytes)", name.c_str(), value / 1024.0, value);
				} else {
					logger::info("    - {}: {} bytes", name.c_str(), value);
				}
			}
		}
	}
}
