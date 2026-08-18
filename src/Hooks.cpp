#include "Hooks.h"
#include "Renderer.h"
#include "Util.h"

namespace Hooks
{
	struct ID3D11Device_CreateBuffer
	{
		static HRESULT thunk(ID3D11Device* a_device, const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer)
		{
			if (!pDesc)
				return func(a_device, pDesc, pInitialData, ppBuffer);

			if (pDesc->ByteWidth == 0)
				return func(a_device, pDesc, pInitialData, ppBuffer);

			D3D11_BUFFER_DESC desc = *pDesc;

			if (desc.Usage == D3D11_USAGE_DEFAULT && desc.CPUAccessFlags == 0)
				desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;

			auto hr = func(a_device, &desc, pInitialData, ppBuffer);

			if (FAILED(hr)) {
				logger::error("ID3D11Device::CreateBuffer - Failed with HR: 0x{:08X}", static_cast<UINT>(hr));
				hr = func(a_device, pDesc, pInitialData, ppBuffer);
			}

			return hr;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11Device_CreateTexture2D
	{
		static HRESULT thunk(ID3D11Device* a_device, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D)
		{
			if (!pDesc)
				return func(a_device, pDesc, pInitialData, ppTexture2D);

			D3D11_TEXTURE2D_DESC desc = *pDesc;

			if (desc.Usage == D3D11_USAGE_DEFAULT && desc.CPUAccessFlags == 0 && !(desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE))
				desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;

			auto hr = func(a_device, &desc, pInitialData, ppTexture2D);

			if (FAILED(hr)) {
				logger::error("ID3D11Device::CreateTexture2D - Failed with HR: 0x{:08X}", static_cast<UINT>(hr));
				hr = func(a_device, pDesc, pInitialData, ppTexture2D);
			}

			return hr;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

#if defined(SKYRIM)
	template <typename = decltype([] {}) >
	struct MemoryManager_AllocateTriShape
	{
		static RE::BSGraphics::TriShapeDX12* thunk(RE::MemoryManager* a_memoryManager, [[ maybe_unused ]] size_t size, int32_t a_alignment, bool a_alignmentRequired)
		{
			auto* triShape = func(a_memoryManager, sizeof(RE::BSGraphics::TriShapeDX12), a_alignment, a_alignmentRequired);
			if (triShape)
				triShape->ownsDX12Buffers = true;
			return triShape;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGraphics_CreateTriShape
	{
		static RE::BSGraphics::TriShapeDX12* thunk(
			RE::BSGraphics::Renderer* a_renderer,
			RE::BSStream* a_bsStream,
			RE::BSGraphics::VertexDesc a_vertexDesc,
			uint16_t a_vertexCount, 
			uint32_t a_indexCount)
		{
			auto triShape = func(a_renderer, a_bsStream, a_vertexDesc, a_vertexCount, a_indexCount);

			// Share vertex buffer
			Util::CreateSharedBuffer(triShape->vertexBuffer, &triShape->vertexBufferDX12);
	
			// Share index buffer
			Util::CreateSharedBuffer(triShape->indexBuffer, &triShape->indexBufferDX12);

			return triShape;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGraphics_CreateTriShapeParticles
	{
		static RE::BSGraphics::TriShapeDX12* thunk(
			RE::BSGraphics::Renderer* a_renderer,
			uint8_t* vertexData, 
			uint32_t vertexDataSize,
			RE::BSGraphics::VertexDesc vertexDesc,
			uint16_t* indexData, 
			uint32_t numIndices)
		{
			auto triShape = func(a_renderer, vertexData, vertexDataSize, vertexDesc, indexData, numIndices);

			// Share vertex buffer
			Util::CreateSharedBuffer(triShape->vertexBuffer, &triShape->vertexBufferDX12);

			// Share index buffer
			Util::CreateSharedBuffer(triShape->indexBuffer, &triShape->indexBufferDX12);

			return triShape;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGraphics_CreateTriShapeVertex
	{
		struct IndexRenderData
		{
			ID3D11Buffer* indexBuffer;
			int32_t refCount;
		};

		static RE::BSGraphics::TriShapeDX12* thunk(
			RE::BSGraphics::Renderer* a_renderer,
			uint8_t* a_vertexData,
			uint32_t a_vertexDataSize,
			RE::BSGraphics::VertexDesc a_vertexDesc,
			IndexRenderData* a_indexRenderData)
		{
			auto triShape = func(a_renderer, a_vertexData, a_vertexDataSize, a_vertexDesc, a_indexRenderData);

			// Share vertex buffer
			Util::CreateSharedBuffer(triShape->vertexBuffer, &triShape->vertexBufferDX12);

			// The original code does 'triShape->indexBuffer = a_indexRenderData->indexBuffer' and calls 'AddRef'
			// We have no way of copying the original indexBufferDX12 here, so we just share it again
			Util::CreateSharedBuffer(triShape->indexBuffer, &triShape->indexBufferDX12);

			return triShape;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSGraphics_CreateTriShapeIndex
	{
		struct VertexRenderData
		{
			ID3D11Buffer* vertexBuffer;				// 00
			ID3D11Buffer* indexBuffer;				// 08
			RE::BSGraphics::VertexDesc vertexDesc;  // 10
		};

		static RE::BSGraphics::TriShapeDX12* thunk(
			RE::BSGraphics::Renderer* a_renderer,
			VertexRenderData* a_vertexRenderData,
			RE::BSGraphics::VertexDesc vertexDesc,
			uint16_t* a_indexData,
			uint32_t a_numIndices)
		{
			auto triShape = func(a_renderer, a_vertexRenderData, vertexDesc, a_indexData, a_numIndices);

			// Share vertex buffer
			// The original function utilizes 'BSGraphics::CopyTriShapeVertices' to copy from 'VertexRenderData' into 'RE::BSGraphics::TriShape'
			// TODO: Find all sites where 'VertexRenderData' is created and extend it with DX12 buffers as well
			Util::CreateSharedBuffer(triShape->vertexBuffer, &triShape->vertexBufferDX12);

			// Share index buffer
			Util::CreateSharedBuffer(triShape->indexBuffer, &triShape->indexBufferDX12);

			return triShape;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Releases dst vertexBuffer, copies src vertexBuffer pointer and calls 'AddRef'
	// Frees dst rawVertexData, then allocates memory for src rawVertexData
	struct BSGraphics_CopyTriShapeVertices
	{
		static int32_t thunk(
			RE::BSGraphics::Renderer* a_renderer,
			BSGraphics_CreateTriShapeIndex::VertexRenderData* a_dstTriShape,
			BSGraphics_CreateTriShapeIndex::VertexRenderData* a_srcTriShape)
		{
			return func(a_renderer, a_dstTriShape, a_srcTriShape);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Re-shares the D3D11 vertex buffer of a TriShapeDX12 to D3D12 after the engine replaced it.
	void ReShareVertexBuffer(RE::BSGraphics::TriShape* triShape)
	{
		if (!triShape)
			return;

		auto triShapeDX12 = reinterpret_cast<RE::BSGraphics::TriShapeDX12*>(triShape);

		if (triShapeDX12->vertexBufferDX12) {
			triShapeDX12->vertexBufferDX12->Release();
			triShapeDX12->vertexBufferDX12 = nullptr;
		}

		Util::CreateSharedBuffer(triShapeDX12->vertexBuffer, &triShapeDX12->vertexBufferDX12);
	}

	// Blends vertex data between two non-skinned BSTriShapes (body morphs).
	// Calls CopyTriShapeVertices internally, which replaces src's vertexBuffer.
	// We must re-share the new D3D11 vertex buffer to D3D12 after the blend.
	struct BSTriShape_ApplyBodyMorph
	{
		static bool thunk(RE::BSTriShape* src, RE::BSTriShape* tgt, double weight)
		{
			auto result = func(src, tgt, weight);

			if (src) {
				auto geomData = Util::Adapter::GetGeometryRuntimeData(src);
				if (geomData.rendererData)
					ReShareVertexBuffer(geomData.rendererData);
			}

			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Blends vertex data between two skinned BSTriShapes (body morphs) by replacing the
	// vertex buffer of every skin partition with the same new D3D11 buffer.
	// We must re-share the new D3D11 vertex buffer to D3D12 after the blend, so meshes
	// created afterwards wrap the morphed data.
	struct BSTriShape_ApplyBodyMorphSkinned
	{
		// a_partitionMask (r9d): bitmask of skin partitions to blend; the engine always passes 0xFFFFFFFF.
		static bool thunk(RE::BSTriShape* src, RE::BSTriShape* tgt, float weight, uint32_t partitionMask)
		{
			auto result = func(src, tgt, weight, partitionMask);

			if (src) {
				auto geomData = Util::Adapter::GetGeometryRuntimeData(src);
				if (auto* skinInstance = geomData.skinInstance) {
					const auto& skinPartition = skinInstance->skinPartition;
					if (skinPartition && skinPartition->numPartitions > 0) {
						// The renderer wraps only the first partition's buffer; all partitions
						// share the same new D3D11 buffer after the blend.
						ReShareVertexBuffer(skinPartition->partitions[0].buffData);
					}
				}
			}

			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	template <std::derived_from<RE::BSTriShape> T>
	struct BSTriShape_Dtor
	{
		static void thunk(T* a_bsTriShape, uint8_t a_release)
		{
			Scene::GetSingleton()->GetSceneGraph()->OnDestroy(a_bsTriShape);

			func(a_bsTriShape, a_release);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	// Copies new float3* dynamic data to the dynamic trishape's float4* dynamic data
	struct BSDynamicTriShape_UpdateDynamicData
	{
		static void thunk(RE::BSDynamicTriShape* a_bsDynamicTriShape, float3* a_dynamicData, bool a_recalculateBounds)
		{
			func(a_bsDynamicTriShape, a_dynamicData, a_recalculateBounds);

			// Dynamic data is ready as float4* after the original function ran
			Scene::GetSingleton()->GetSceneGraph()->UpdateDynamicData(a_bsDynamicTriShape);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TriShape_Dtor
	{
		static void thunk([[ maybe_unused ]] void* a1, RE::BSGraphics::TriShape* a_triShape)
		{
			if (a_triShape && _InterlockedExchangeAdd(&a_triShape->refCount, 0xFFFFFFFF) == 1)
			{
				auto indexBuffer = reinterpret_cast<ID3D11Buffer*>(a_triShape->indexBuffer);
				if (indexBuffer)
					indexBuffer->Release();

				auto vertexBuffer = reinterpret_cast<ID3D11Buffer*>(a_triShape->vertexBuffer);
				if (vertexBuffer)
					vertexBuffer->Release();

				Util::Adapter::DeallocateTriShapeData(a_triShape);

				if (static_cast<RE::BSGraphics::TriShapeDX12*>(a_triShape)->ownsDX12Buffers) {
					auto* triShapeDX12 = static_cast<RE::BSGraphics::TriShapeDX12*>(a_triShape);

					if (triShapeDX12->indexBufferDX12)
						triShapeDX12->indexBufferDX12->Release();

					if (triShapeDX12->vertexBufferDX12)
						triShapeDX12->vertexBufferDX12->Release();
				}

				auto* mm = RE::MemoryManager::GetSingleton();
				mm->Deallocate(a_triShape, false);
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSTextureSet_SetTexture
	{
		static void thunk(RE::BSTextureSet* a_bsTextureSet, RE::BSTextureSet::Texture a_texture, RE::NiSourceTexturePtr& a_srcTexture)
		{
			func(a_bsTextureSet, a_texture, a_srcTexture);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	void NiSourceTexture_Destructor::thunk(RE::NiSourceTexture* oThis)
	{
		if (oThis && oThis->rendererTexture) {
			auto scene = Scene::GetSingleton();
			auto sceneGraph = scene->GetSceneGraph();
			sceneGraph->ReleaseTexture(oThis->rendererTexture);
		}

		func(oThis);
	}

	template <std::derived_from<RE::NiCullingProcess> T>
	struct NiCullingProcess_AppendVirtual
	{
		static void thunk(T* cullingProcess, RE::BSGeometry* geometry, uint32_t a_arg2)
		{
			if (geometry) {
				if (Scene::GetSingleton()->ApplyPathTracingCull() && Util::Culling::ShouldCull(geometry))
					return;
			}

			func(cullingProcess, geometry, a_arg2);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void BSBatchRenderer_RenderPassImmediately::thunk(RE::BSRenderPass* pass, uint32_t technique, bool alphaTest, uint32_t renderFlags)
	{
		if (!pass->shader) {
			func(pass, technique, alphaTest, renderFlags);
			return;
		}

		const auto shaderType = pass->shader->shaderType.get();
		auto* scene = Scene::GetSingleton();

		const bool pathTracingActive = scene->IsPathTracingActive();

		// Water rendering toggle during path tracing
		if (pathTracingActive && shaderType == RE::BSShader::Type::Water)
			return;

		auto* shaderProperty = pass->shaderProperty;
		if (!shaderProperty || !shaderProperty->material) {
			func(pass, technique, alphaTest, renderFlags);
			return;
		}

		if (pathTracingActive) {
			// Cull non-effect refractive geometry during path tracing
			if (shaderType != RE::BSShader::Type::Effect && shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kRefraction))
				return;

			auto feature = shaderProperty->material->GetFeature();

			// Cull eyes since they are transparent and draw after PT is composited
			switch (feature) {
			case RE::BSShaderMaterial::Feature::kEnvironmentMap: // Some mods use this flag for eyes,
			case RE::BSShaderMaterial::Feature::kEye:
				return;
			}
		}

		// Path tracing occlusion-based culling
		if (scene->ApplyPathTracingCull() && pass->shader && pass->geometry)
		{
			switch (shaderType) {
			case RE::BSShader::Type::Sky:
			case RE::BSShader::Type::Water:
			case RE::BSShader::Type::Effect:
			case RE::BSShader::Type::Particle:
				break;
			default:
				// Utility, Lighting, DistantTree, BloodSplatter, etc.
				return;
			}
		}

		func(pass, technique, alphaTest, renderFlags);
	}

	void* DrawWorld_BuildSceneLists::thunk()
	{
		if (Scene::GetSingleton()->ApplyFullPathTracingCull())
			return nullptr;

		return func();
	}

	struct BGSTerrainBlock_Load
	{
		static RE::BGSTerrainBlock* thunk(RE::BGSTerrainBlock* a_block, RE::BGSTerrainManager* a_terrainManager, RE::BSStream* a_stream, int32_t a4, int32_t a5)
		{
			auto result = func(a_block, a_terrainManager, a_stream, a4, a5);

			if (result && result->node && result->land) {
				auto lodLevel = static_cast<std::int32_t>(result->node->GetLODLevel());
				auto key = RE::BSFixedString(Constants::ExtraData::LandLOD);
				auto extra = RE::NiIntegersExtraData::Create(key, { lodLevel });
				result->land->AddExtraData(extra);
			}

			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSTerrainBlock_Dtor
	{
		static void thunk(RE::BGSTerrainBlock* a_block)
		{
			func(a_block);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSObjectBlock_Load
	{
		static RE::BGSObjectBlock* thunk(RE::BGSObjectBlock* a_block, RE::BGSTerrainNode* a_terrainNode, RE::BSStream* a_stream)
		{
			auto result = func(a_block, a_terrainNode, a_stream);
			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSObjectBlock_Dtor
	{
		static void thunk(RE::BGSObjectBlock* a_block)
		{
			return func(a_block);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSDistantTreeBlock_AttachSE
	{
		static void thunk(RE::BGSDistantTreeBlock* a_block, float a2)
		{
			const bool wasAttached = a_block->attached;

			func(a_block, a2);

			const bool valid = a_block->node && a_block->attached && !wasAttached && !a_block->node->mapTerrain;
			if (a_block->doneLoading && valid) {
				if (!a_block->treeGroups.empty()) {
				}
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSDistantTreeBlock_AttachAE
	{
		static void thunk(RE::BGSDistantTreeBlock* a_block)
		{
			const bool wasAttached = a_block->attached;

			func(a_block);

			const bool valid = a_block->node && a_block->attached && !wasAttached && !a_block->node->mapTerrain;
			if (a_block->doneLoading && valid) {
				if (!a_block->treeGroups.empty()) {

				}
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSDistantTreeBlock_DtorSE
	{
		static void thunk(RE::BGSDistantTreeBlock* a_block)
		{
			func(a_block);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BGSDistantTreeBlock_DtorAE
	{
		static_assert(sizeof(RE::BGSTerrainNode::Layer<RE::BGSDistantTreeBlock>) == 0x30);

		static void thunk(RE::BSResource::IEntryDB* a_entryDB, RE::BGSTerrainNode::Layer<RE::BGSDistantTreeBlock>* a2, int a3, void* a4)
		{
			RE::BGSDistantTreeBlock* block = nullptr;

			if (a2)
				block = a2->block;

			func(a_entryDB, a2, a3, a4);

			if (a2 && block) {
				if (block != a2->block) {
				}
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};
	
	// 1401B6AF0 SE/140203810 AE
	struct GrassManager_CreateInstances
	{
		static uint32_t thunk(RE::BGSGrassManager* a_grassManager, RE::CreateGrassParams* a_createGrassParams)
		{
			auto instances = func(a_grassManager, a_createGrassParams);

			if (instances > 0) {
			}

			return instances;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

#elif defined(FALLOUT4)
	struct BSD3DResourceCreator_AddBufferToRelease
	{
		static int thunk(void* a_this, RE::BSGraphics::Buffer* a_buffer)
		{
			if (a_buffer)
				Scene::GetSingleton()->TryReleaseBuffer(a_buffer->buffer);

			return func(a_this, a_buffer);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSD3DResourceCreator_CreateBufferRequested
	{
		enum class BufferType : uint32_t
		{
			Vertex = 0,
			Index = 1,
			Constant = 2,
			Byte = 3,
			Structured = 4
		};

		struct Request
		{
			RE::BSGraphics::Buffer* buffer;
			BufferType type;
			uint32_t size;
			bool isDynamic;
		};

		static void thunk(void* a_this, Request* a_request)
		{
			func(a_this, a_request);

			if (!a_request) {
				logger::info("BSD3DResourceCreator::CreateBufferRequested - Request is nullptr");
				return;
			}

			if (a_request->type != BufferType::Vertex && a_request->type != BufferType::Index)
				return;

			if (a_request->isDynamic)
				return;

			if (!a_request->buffer) {
				logger::info("BSD3DResourceCreator::CreateBufferRequested - Buffer is nullptr");
				return;
			}

			Scene::GetSingleton()->TryShareBuffer(a_request->buffer->buffer);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
#endif

	void InstallEarly()
	{

	}

	void Install()
	{
#if defined(SKYRIM)
		stl::write_vfunc<0x0, NiSourceTexture_Destructor>(RE::VTABLE_NiSourceTexture[0]);
		const auto createTriShapeA = REL::RelocationID(75473, 77259);
		const auto createTriShapeB = REL::RelocationID(75474, 77260);
		const auto createTriShapeC = REL::RelocationID(75475, 77261);
		const auto createTriShapeD = REL::RelocationID(75476, 77262);

		stl::write_thunk_call<MemoryManager_AllocateTriShape<>>(createTriShapeA.address() + 0x9f);
		stl::write_thunk_call<MemoryManager_AllocateTriShape<>>(createTriShapeB.address() + 0x87);
		stl::write_thunk_call<MemoryManager_AllocateTriShape<>>(createTriShapeC.address() + 0x82);
		stl::write_thunk_call<MemoryManager_AllocateTriShape<>>(createTriShapeD.address() + REL::Relocate(0x85, 0x87));

		stl::detour_thunk<BSGraphics_CreateTriShape>(createTriShapeA);
		stl::detour_thunk<BSGraphics_CreateTriShapeParticles>(createTriShapeB);
		stl::detour_thunk<BSGraphics_CreateTriShapeVertex>(createTriShapeC); // Landscape
		stl::detour_thunk<BSGraphics_CreateTriShapeIndex>(createTriShapeD);  // NiSkinPartition::Partition::buffData

		// This function is inlined in some places on AE
		//stl::detour_thunk<BSGraphics_CopyTriShapeVertices>(REL::RelocationID(74735, 76477));

		stl::detour_thunk<BSTriShape_ApplyBodyMorph>(REL::RelocationID(74720, 76460));
		stl::detour_thunk<BSTriShape_ApplyBodyMorphSkinned>(REL::RelocationID(74721, 76462));

		stl::write_vfunc<0x0, BSTriShape_Dtor<RE::BSTriShape>>(RE::VTABLE_BSTriShape[0]);
		stl::write_vfunc<0x0, BSTriShape_Dtor<RE::BSDynamicTriShape>>(RE::VTABLE_BSDynamicTriShape[0]);
		stl::write_vfunc<0x0, BSTriShape_Dtor<RE::BSSubIndexTriShape>>(RE::VTABLE_BSSubIndexTriShape[0]);
		stl::write_vfunc<0x0, BSTriShape_Dtor<RE::BSMultiStreamInstanceTriShape>>(RE::VTABLE_BSMultiStreamInstanceTriShape[0]);

		// Use a hook to update dynamic data, else we risk trying accessing dynamic data while the engine has already released it
		stl::detour_thunk<BSDynamicTriShape_UpdateDynamicData>(REL::RelocationID(69570, 70954));
		
		stl::detour_thunk<TriShape_Dtor>(REL::RelocationID(75480, 77267));

		stl::detour_thunk<BSTextureSet_SetTexture>(REL::RelocationID(20907, 0));
		
		// Terrain LOD
		stl::detour_thunk<BGSTerrainBlock_Load>(REL::RelocationID(30932, 31735));
		//stl::detour_thunk<BGSTerrainBlock_Dtor>(REL::RelocationID(30933, 31736));

		// Object LOD
		//stl::detour_thunk<BGSObjectBlock_Load>(REL::RelocationID(30737, 31575));

		// Two completely different functions for SE and AE, however the end hook address for both is NiMemFree
		//stl::write_thunk_call<BGSObjectBlock_Dtor>(REL::RelocationID(30730, 31634).address() + REL::Relocate(0x6D, 0x11A));

		// Tree LOD
		/*if (REL::Module::IsSE()) {
			stl::detour_thunk<BGSDistantTreeBlock_AttachSE>(REL::RelocationID(30832, 0));
			stl::detour_thunk<BGSDistantTreeBlock_DtorSE>(REL::RelocationID(30821, 0));
		}
		else {
			stl::detour_thunk<BGSDistantTreeBlock_AttachAE>(REL::RelocationID(0, 31653));
			stl::detour_thunk<BGSDistantTreeBlock_DtorAE>(REL::RelocationID(0, 31717));
		}*/
		
		// Landscape
		//stl::detour_thunk<TESObjectLAND_Attach3D>(REL::RelocationID(18334, 18750));
		//stl::detour_thunk<TESObjectLAND_Detach3D>(REL::RelocationID(18335, 18751));

		// Water
		//stl::detour_thunk<TESWaterSystem_AddWater>(REL::RelocationID(31388, 32179));
		//stl::detour_thunk<TESWaterSystem_RemoveWater>(REL::RelocationID(31391, 32182));

		// Grass
		//stl::detour_thunk<GrassManager_CreateInstances>(REL::RelocationID(15212, 15381));

		/*stl::detour_thunk<NiCullingProcess_AppendVirtual>(REL::RelocationID(26533, 27130));
		stl::detour_thunk<BSCullingProcess_AppendVirtual>(REL::RelocationID(74807, 76556));*/

		stl::write_vfunc<0x18, NiCullingProcess_AppendVirtual<RE::NiCullingProcess>>(RE::VTABLE_NiCullingProcess[0]);
		stl::write_vfunc<0x18, NiCullingProcess_AppendVirtual<RE::BSFadeNodeCuller>>(RE::VTABLE_BSFadeNodeCuller[0]);
		stl::write_vfunc<0x18, NiCullingProcess_AppendVirtual<RE::BSCullingProcess>>(RE::VTABLE_BSCullingProcess[0]);

		stl::detour_thunk<BSBatchRenderer_RenderPassImmediately>(REL::RelocationID(100854, 107644));

		stl::detour_thunk<DrawWorld_BuildSceneLists>(REL::RelocationID(35630, 36643));

		auto* scene = Scene::GetSingleton();
		scene->g_FlowMapSize = reinterpret_cast<int32_t*>(REL::RelocationID(527644, 414596).address());
		scene->g_DisplacementCellTexCoordOffset = reinterpret_cast<float4*>(REL::RelocationID(528184, 415129).address());
		scene->g_DisplacementMeshFlowCellOffset = reinterpret_cast<RE::NiPoint2*>(REL::RelocationID(528164, 415109).address());
		scene->g_DisplacementMeshPos = reinterpret_cast<RE::NiPoint2*>(REL::RelocationID(516235, 402400).address());

		REL::Relocation<float*> g_Time{ REL::RelocationID(513213, 390953) };
		scene->g_Time = g_Time.get();

		scene->g_TreeLODAtlasTex = reinterpret_cast<RE::NiPointer<RE::NiSourceTexture>*>(REL::RelocationID(528222, 415172).address());

		REL::Relocation<bool*> g_BypassSubIndexVisibility{ REL::RelocationID(524687, 411302) };
		scene->g_BypassSubIndexVisibility = g_BypassSubIndexVisibility.get();

#elif defined(FALLOUT4)
		stl::detour_thunk<BSD3DResourceCreator_CreateBufferRequested>(REL::ID(2277441));
		stl::detour_thunk<BSD3DResourceCreator_AddBufferToRelease>(REL::ID(2277426));
#endif
		logger::info("[Raytracing] Installed hooks");
	}

	void InstallD3D11(ID3D11Device* a_device)
	{
		stl::detour_vfunc<3, ID3D11Device_CreateBuffer>(a_device);
		stl::detour_vfunc<5, ID3D11Device_CreateTexture2D>(a_device);
	}
}
