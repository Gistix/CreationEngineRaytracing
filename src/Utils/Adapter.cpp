#include "Adapter.h"
#include "Constants.h"
#include "Scene.h"

#if defined(FALLOUT4)
#include "Types/RE/FO4/ShadowSceneNode.h"
#include "Types/RE/FO4/NiSwitchNode.h"
#endif

#include "Core/Mesh/DynamicMesh.h"
#include "Types/RE/TriShapeDX12.h"
#include "Util.h"

namespace Util
{
	namespace Adapter
	{
		template <class T>
		T readAt(const void* a_object, std::size_t a_offset)
		{
			return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(a_object) + a_offset);
		}

		GeometryRuntimeData GetGeometryRuntimeData(RE::BSGeometry* a_geometry)
		{
			GeometryRuntimeData runtimeData{};
#if defined(SKYRIM)
			auto& data = a_geometry->GetGeometryRuntimeData();
			runtimeData.alphaProperty = data.alphaProperty.get();
			runtimeData.shaderProperty = data.shaderProperty.get();
			runtimeData.skinInstance = data.skinInstance.get();
			runtimeData.rendererData = data.rendererData;
			runtimeData.vertexDesc = data.vertexDesc;
#elif (FALLOUT4)
			runtimeData.alphaProperty = reinterpret_cast<RE::NiAlphaProperty*>(a_geometry->properties[0].get());
			runtimeData.shaderProperty = reinterpret_cast<RE::BSShaderProperty*>(a_geometry->properties[1].get());
			runtimeData.skinInstance = a_geometry->skinInstance.get();
			runtimeData.rendererData = reinterpret_cast<RE::BSGraphics::TriShape*>(a_geometry->rendererData);
			runtimeData.vertexDesc = a_geometry->vertexDesc;
#endif		

			return runtimeData;
		}

		bool IsSkinned(const GeometryRuntimeData& geometryData)
		{
#if defined(SKYRIM)
			auto skinInstance = static_cast<RE::NiSkinInstance*>(geometryData.skinInstance);
			return !geometryData.rendererData && skinInstance && skinInstance->skinPartition && skinInstance->skinPartition->numPartitions > 0;
#elif defined(FALLOUT4)
			return geometryData.skinInstance != nullptr;
#endif
		}

		TrishapeRuntimeData GetTrishapeRuntimeData(RE::BSTriShape* a_triShape)
		{
			TrishapeRuntimeData data{};
#if defined(SKYRIM)
			auto runtime = a_triShape->GetTrishapeRuntimeData();
			data.vertexCount = runtime.vertexCount;
			data.triangleCount = runtime.triangleCount;
#elif defined(FALLOUT4)
			data.vertexCount = a_triShape->numVertices;
			data.triangleCount = a_triShape->numTriangles;
#endif
			return data;
		}

		RE::BSGraphics::TriShape* GetRendererData(const GeometryRuntimeData& geometryData)
		{
#if defined(SKYRIM)
			if (IsSkinned(geometryData)) {
				auto skinInstance = static_cast<RE::NiSkinInstance*>(geometryData.skinInstance);
				return skinInstance->skinPartition->partitions[0].buffData;
			}
			return static_cast<RE::BSGraphics::TriShape*>(geometryData.rendererData);
#elif defined(FALLOUT4)
			return static_cast<RE::BSGraphics::TriShape*>(geometryData.rendererData);
#endif
		}

		bool GetAlphaBlending(RE::NiAlphaProperty* a_property)
		{
			if (!a_property) return false;
#if defined(SKYRIM)
			return a_property->GetAlphaBlending();
#elif defined(FALLOUT4)
			return (a_property->flags.flags & 1) != 0;
#endif
		}

		uint32_t GetDynamicDataSize(RE::BSDynamicTriShape* a_dynamicTriShape)
		{
#if defined(SKYRIM)
			return a_dynamicTriShape->GetDynamicTrishapeRuntimeData().dataSize;
#elif defined(FALLOUT4)
			return a_dynamicTriShape->dynamicDataSize;
#endif
		}

		void* LockDynamicData(RE::BSDynamicTriShape* a_dynamicTriShape)
		{
#if defined(SKYRIM)
			a_dynamicTriShape->GetDynamicTrishapeRuntimeData().lock.Lock();
			return a_dynamicTriShape->GetDynamicTrishapeRuntimeData().dynamicData;
#elif defined(FALLOUT4)
			using Fn = void* (*)(RE::BSDynamicTriShape*);
			static REL::Relocation<Fn> func{ REL::ID(2270749) };
			return func(a_dynamicTriShape);
		#endif
		}

		void UnlockDynamicData(RE::BSDynamicTriShape* a_dynamicTriShape)
		{
#if defined(SKYRIM)
			a_dynamicTriShape->GetDynamicTrishapeRuntimeData().lock.Unlock();
#elif defined(FALLOUT4)
			using Fn = void (*)(RE::BSDynamicTriShape*);
			static REL::Relocation<Fn> func{ REL::ID(2270751) };
			func(a_dynamicTriShape);
		#endif
		}

		void UpdateDynamicData(DynamicMesh* dynamicMesh, RE::BSDynamicTriShape* bsDynamicTriShape)
		{
#if defined(SKYRIM)
			auto& runtimeData = bsDynamicTriShape->GetDynamicTrishapeRuntimeData();
			dynamicMesh->UpdateDynamicData(runtimeData.dynamicData, runtimeData.dataSize);
#elif defined(FALLOUT4)
			void* data = LockDynamicData(bsDynamicTriShape);
			dynamicMesh->UpdateDynamicData(data, bsDynamicTriShape->dynamicDataSize);
			UnlockDynamicData(bsDynamicTriShape);
		#endif
		}

		void GetAlwaysRenderChildren(RE::NiNode* shadowSceneNode, eastl::vector<RE::NiAVObject*>& outChildren)
		{
#if defined(SKYRIM)
			auto ssn = reinterpret_cast<RE::ShadowSceneNode*>(shadowSceneNode);
			if (auto portalGraph = ssn->GetRuntimeData().portalGraph) {
				for (auto& child : portalGraph->alwaysRenderChildren) {
					if (child->parent)
						continue;
					outChildren.push_back(child.get());
				}
			}
#elif defined(FALLOUT4)
			if (auto portalGraph = GetPortalGraph(shadowSceneNode)) {
				for (auto& child : portalGraph->alwayRenderChildren) {
					if (child && child->parent)
						continue;
					if (child)
						outChildren.push_back(child.get());
				}
			}
#endif
		}

		bool IsValidTriShape(RE::BSGeometry* a_geometry)
		{
#if defined(SKYRIM)
			auto type = a_geometry->GetType();
			return 
				type == RE::BSGeometry::Type::kTriShape || 
				type == RE::BSGeometry::Type::kDynamicTriShape || 
				type == RE::BSGeometry::Type::kSubIndexTriShape;
#elif defined(FALLOUT4)
			auto type = static_cast<RE::BSGeometryType>(a_geometry->type);
			return 
				type == RE::BSGeometryType::kTriShape || 
				type == RE::BSGeometryType::kDynamicTriShape || 
				type == RE::BSGeometryType::kSubIndexTriShape || 
				type == RE::BSGeometryType::kMeshLODTriShape;
#endif
		}

		const char* GetName(RE::TESForm* a_form)
		{
			if (!a_form) return nullptr;
#if defined(SKYRIM)
			return a_form->GetName();
#elif defined(FALLOUT4)
			return a_form->GetFormEditorID();
#endif		
		}

		RE::BIPOBJECT* GetBipedObjects(RE::BipedAnim* a_bipedAnim)
		{
#if defined(SKYRIM)
			return &a_bipedAnim->objects[0];
#elif defined(FALLOUT4)
			return &a_bipedAnim->object[0];
#endif		
		}

		RE::BSGeometry* AsGeometry(RE::NiAVObject* a_object) {
#if defined(SKYRIM)
			return a_object->AsGeometry();
#elif defined(FALLOUT4)
			return a_object->IsGeometry();
#endif
		}

		RE::BSTriShape* AsTriShape(RE::NiAVObject* a_object)
		{
#if defined(SKYRIM)
			return a_object->AsTriShape();
#elif defined(FALLOUT4)
			return a_object->IsTriShape();
#endif
		}

		RE::NiNode* AsNode(RE::NiAVObject* a_object) {
#if defined(SKYRIM)
			return a_object->AsNode();
#elif defined(FALLOUT4)
			return a_object->IsNode();
#endif		
		}

		RE::BSFadeNode* AsFadeNode(RE::NiAVObject* a_object) {
#if defined(SKYRIM)
			return a_object->AsFadeNode();
#elif defined(FALLOUT4)
			return a_object->IsFadeNode();
#endif		
		}

		RE::BSSubIndexTriShape* AsSubIndexTriShape(RE::BSGeometry* a_geometry)
		{
#if defined(SKYRIM)
			return a_geometry->AsSubIndexTriShape();
#elif defined(FALLOUT4)
			return a_geometry->IsSubIndexTriShape();
#endif	
		}

		RE::BSDynamicTriShape* AsDynamicTriShape(RE::BSTriShape* a_geometry)
		{
#if defined(SKYRIM)
			return a_geometry->AsDynamicTriShape();
#elif defined(FALLOUT4)
			return a_geometry->IsDynamicTriShape();
#endif
		}

		RE::TESObjectREFR* GetOwner(RE::NiAVObject* a_object)
		{
#if defined(SKYRIM)
			return REL::RelocateMember<RE::TESObjectREFR*>(a_object, 0x0F8, 0x110);
#elif defined(FALLOUT4)
			return a_object ? reinterpret_cast<RE::TESObjectREFR*>(a_object->userData) : nullptr;
#endif	
		}

		RE::NiNode* GetFirstPerson3D(RE::PlayerCharacter* a_player)
		{
			if (!a_player)
				return nullptr;

#if defined(SKYRIM)
			return a_player->GetInfoRuntimeData().firstPerson3D.get();
#elif defined(FALLOUT4)
			return a_player->firstPerson3D.get();
#endif	
		}

		RE::NiPoint3 GetZeroNiPoint3()
		{
#if defined(SKYRIM)
			return RE::NiPoint3::Zero();
#elif defined(FALLOUT4)
			return { 0.0f, 0.0f, 0.0f };
#endif
		}

		RE::NiTexture* GetDefaultTextureProjNoiseMap()
		{
#if defined(SKYRIM)
			return RE::BSGraphics::State::GetSingleton()->defaultTextureProjNoiseMap.get();
#elif defined(FALLOUT4)
			return GetGraphicsState().defaultTextureWhiteNoiseMap.get();
#endif
		}

		RE::NiPoint3 GetCameraEyePosition()
		{
#if defined(SKYRIM)
			auto& runtimeData = RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData();
			return runtimeData.posAdjust.getEye();
#elif defined(FALLOUT4)
			static REL::Relocation<RE::BSGraphics::State*> singleton{ RE::ID::BSGraphics::State::Singleton };
			const auto* state = reinterpret_cast<const std::uint8_t*>(singleton.address());
			return *reinterpret_cast<const RE::NiPoint3*>(state + 0x37C); // CameraStateData::currentPosAdjust
#endif
		}

		bool IsInFirstPerson(RE::PlayerCharacter* a_player, RE::PlayerCamera* a_camera)
		{
#if defined(SKYRIM)
			return !a_player->GetPlayerRuntimeData().playerFlags.isInThirdPersonMode && !a_camera->IsInFreeCameraMode();
#elif defined(FALLOUT4)
			if (!a_player || !a_camera)
				return false;

			auto currentState = a_camera->GetCameraCurrentState();
			return currentState && currentState->id == RE::CameraStates::kFirstPerson;
#endif
		}

		RE::NiPoint3 GetFirstPersonNodePosition(RE::PlayerCamera* a_camera)
		{
#if defined(SKYRIM)
			using Fn = void (*)(RE::PlayerCamera*, RE::NiPoint3*);
			static REL::Relocation<Fn> func{ REL::RelocationID(49854, 50786) };

			RE::NiPoint3 result;
			func(a_camera, &result);
			return result;
#elif defined(FALLOUT4)
			(void)a_camera;
			if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->firstPersonEye)
				return player->firstPersonEye->world.translate;
			return GetZeroNiPoint3();
#endif	
		}
		
		RE::BSMultiBound* GetMultiBound(RE::BSMultiBoundNode* a_node)
		{
#if defined(SKYRIM)
			return a_node->GetRuntimeData().multiBound.get();
#elif defined(FALLOUT4)
			return a_node->multiBound.get();
#endif
		}

		RE::BSMultiBoundAABB* GetMultiBoundAABB(RE::BSMultiBound* a_multiBound)
		{
			if (!a_multiBound)
				return nullptr;
#if defined(SKYRIM)
			if (!a_multiBound->data)
				return nullptr;
			return netimmerse_cast<RE::BSMultiBoundAABB*>(a_multiBound->data.get());
#elif defined(FALLOUT4)
			if (!a_multiBound->shape)
				return nullptr;
			return netimmerse_cast<RE::BSMultiBoundAABB*>(a_multiBound->shape.get());
#endif
		}

		float GetNiBoundRadius(const RE::NiBound& a_bound)
		{
#if defined(SKYRIM)
			return a_bound.radius;
#elif defined(FALLOUT4)
			return a_bound.fRadius;
#endif
		}

		void SetNiBound(RE::NiBound& a_bound, const RE::NiPoint3& a_center, float a_radius)
		{
			a_bound.center = a_center;
#if defined(SKYRIM)
			a_bound.radius = a_radius;
#elif defined(FALLOUT4)
			a_bound.fRadius = a_radius;
#endif
		}
		
		RE::NiTObjectArray<RE::NiPointer<RE::NiAVObject>>& GetChildren(RE::NiNode* a_node) {
#if defined(SKYRIM)
			return a_node->GetChildren();
#elif defined(FALLOUT4)
			return a_node->children;
#endif		
		}

		RE::NiAVObject* GetChildAt(RE::NiNode* a_node, uint16_t a_index) {
			if (!a_node) return nullptr;
#if defined(SKYRIM)
			auto& children = a_node->GetChildren();
			if (a_index < children.size()) {
				return children[a_index].get();
			}
#elif defined(FALLOUT4)
			auto& children = a_node->children;
			if (a_index < children.size()) {
				return children.data()[a_index].get();
			}
#endif
			return nullptr;
		}

		uint8_t* GetVertexData(RE::BSGraphics::TriShape* rendererData)
		{
#if defined(SKYRIM)
			return rendererData->rawVertexData;
#elif defined(FALLOUT4)
			return reinterpret_cast<uint8_t*>(rendererData->vertexBuffer->data);
#endif
		}

		uint16_t* GetIndexData(RE::BSGraphics::TriShape* rendererData)
		{
#if defined(SKYRIM)
			return rendererData->rawIndexData;
#elif defined(FALLOUT4)
			return reinterpret_cast<uint16_t*>(rendererData->indexBuffer->data);
#endif
		}

		void DeallocateTriShapeData(RE::BSGraphics::TriShape* rendererData)
		{
#if defined(SKYRIM)
			auto* mm = RE::MemoryManager::GetSingleton();
			if (rendererData->rawVertexData)
				mm->Deallocate(rendererData->rawVertexData, false);
			if (rendererData->rawIndexData)
				mm->Deallocate(rendererData->rawIndexData, false);
#elif defined(FALLOUT4)
			(void)rendererData;
			// FO4 handles memory differently or doesn't have raw vertex/index data here
#endif
		}

		ID3D12Resource* GetVertexBufferDX12(RE::BSGraphics::TriShape* a_triShape)
		{
			if (!a_triShape)
				return nullptr;

#if defined(SKYRIM)
			return static_cast<RE::BSGraphics::TriShapeDX12*>(a_triShape)->vertexBufferDX12;
#elif defined(FALLOUT4)
			return Scene::GetSingleton()->GetSharedBuffer(a_triShape->vertexBuffer->buffer);
#endif
		}

		ID3D12Resource* GetIndexBufferDX12(RE::BSGraphics::TriShape* a_triShape)
		{
			if (!a_triShape)
				return nullptr;

#if defined(SKYRIM)
			return static_cast<RE::BSGraphics::TriShapeDX12*>(a_triShape)->indexBufferDX12;
#elif defined(FALLOUT4)
			return Scene::GetSingleton()->GetSharedBuffer(a_triShape->indexBuffer->buffer);
#endif
		}

		ID3D11Buffer* GetD3D11VertexBuffer(RE::BSGraphics::TriShape* a_triShape)
		{
			if (!a_triShape)
				return nullptr;

#if defined(SKYRIM)
			return reinterpret_cast<ID3D11Buffer*>(a_triShape->vertexBuffer);
#elif defined(FALLOUT4)
			return reinterpret_cast<ID3D11Buffer*>(a_triShape->vertexBuffer->buffer);
#endif
		}

		ID3D11Buffer* GetD3D11IndexBuffer(RE::BSGraphics::TriShape* a_triShape)
		{
			if (!a_triShape)
				return nullptr;

#if defined(SKYRIM)
			return reinterpret_cast<ID3D11Buffer*>(a_triShape->indexBuffer);
#elif defined(FALLOUT4)
			return reinterpret_cast<ID3D11Buffer*>(a_triShape->indexBuffer->buffer);
#endif
		}

		SkinData GetSkinData(RE::BSGeometry* geometry)
		{
			SkinData result = { false, 0, nullptr, 0 };
#if defined(SKYRIM)
			auto* skinInstance = geometry->GetGeometryRuntimeData().skinInstance.get();
			if (skinInstance) {
				result.hasSkin = true;
				auto* skinData = skinInstance->skinData.get();
				if (skinData) {
					result.numBones = skinData->bones;
					result.boneWorldTransforms = skinInstance->boneWorldTransforms;
				}
				result.frameID = skinInstance->frameID;
			}
#elif defined(FALLOUT4)
			auto* skinInstance = geometry->skinInstance.get();
			if (skinInstance) {
				result.hasSkin = true;
				result.numBones = skinInstance->bones.size();
				result.boneWorldTransforms = reinterpret_cast<const RE::NiTransform**>(reinterpret_cast<std::uintptr_t>(skinInstance->worldTransforms.data()));
				result.frameID = skinInstance->frameID;
			}
#endif
			return result;
		}

		const RE::NiTransform* GetSkinToBoneTransform(RE::BSGeometry* geometry, uint32_t a_boneIndex)
		{
			if (!geometry)
				return nullptr;

#if defined(SKYRIM)
			auto* skinInstance = geometry->GetGeometryRuntimeData().skinInstance.get();
			if (!skinInstance || !skinInstance->skinData || a_boneIndex >= skinInstance->skinData->bones)
				return nullptr;
			return &skinInstance->skinData->boneData[a_boneIndex].skinToBone;
#elif defined(FALLOUT4)
			auto* skinInstance = geometry->skinInstance.get();
			if (!skinInstance || !skinInstance->boneData || a_boneIndex >= skinInstance->boneData->transforms.size())
				return nullptr;
			return &skinInstance->boneData->transforms[a_boneIndex].transform;
#endif
		}

		ID3D11Texture2D* GetTextureResource(RE::NiTexture* a_texture)
		{
			auto* rendererTexture = GetRendererTexture(a_texture);
			return rendererTexture ? reinterpret_cast<ID3D11Texture2D*>(rendererTexture->texture) : nullptr;
		}

		RE::BSGraphics::Texture* GetRendererTexture(RE::NiTexture* a_texture)
		{
			if (!a_texture)
				return nullptr;

#if defined(SKYRIM)
			auto* sourceTexture = netimmerse_cast<RE::NiSourceTexture*>(a_texture);
			return sourceTexture ? sourceTexture->rendererTexture : nullptr;
#elif defined(FALLOUT4)
			return a_texture->rendererTexture;
#endif
		}

		RE::TESObjectREFR* AsReference(RE::TESForm* a_object)
		{
#if defined(SKYRIM)
			return a_object->AsReference();
#elif defined(FALLOUT4)
			return a_object->IsReference();
#endif		
		}

		RE::ExtraDataList* GetExtraDataList(RE::TESObjectREFR* a_refr)
		{
#if defined(SKYRIM)
			return &a_refr->extraList;
#elif defined(FALLOUT4)
			return a_refr->extraList.get();
#endif			
		}

		RE::TESBoundObject* GetBaseObject(RE::TESObjectREFR* a_refr)
		{
#if defined(SKYRIM)
			return a_refr->GetBaseObject();
#elif defined(FALLOUT4)
			return a_refr->GetObjectReference();
#endif
		}

		RE::TESForm* GetBipedObjectItem(const RE::BIPOBJECT& a_bipObject)
		{
#if defined(SKYRIM)
			return a_bipObject.item;
#elif defined(FALLOUT4)
			return a_bipObject.parent.object;
#endif
		}

		LightRuntimeData GetLightRuntimeData(RE::NiLight* a_light)
		{
			LightRuntimeData data{};
#if defined(SKYRIM)
			auto& rd = a_light->GetLightRuntimeData();
			data.ambient = rd.ambient;
			data.diffuse = rd.diffuse;
			data.radius = rd.radius.x;
			data.fade = rd.fade;
#elif defined(FALLOUT4)
			data.ambient = a_light->amb;
			data.diffuse = a_light->diff;
			data.radius = a_light->spec.r;
			data.fade = a_light->dimmer;
#endif
			return data;
		}

		PointLightRuntimeData GetPointLightRuntimeData(RE::NiLight* a_light)
		{
			PointLightRuntimeData data{};
#if defined(SKYRIM)
			auto* rd = &static_cast<RE::NiPointLight*>(a_light)->GetPointLightRuntimeData();
			data.constAttenuation = rd->constAttenuation;
			data.linearAttenuation = rd->linearAttenuation;
			data.quadraticAttenuation = rd->quadraticAttenuation;
#elif defined(FALLOUT4)
			auto* pointLight = static_cast<RE::NiPointLight*>(a_light);
			data.constAttenuation = pointLight->constantAttenuation;
			data.linearAttenuation = pointLight->linearAttenuation;
			data.quadraticAttenuation = pointLight->quadraticAttenuation;
			auto* spotLight = netimmerse_cast<RE::NiSpotLight*>(a_light);
			if (spotLight) {
				data.spotOuterAngle = spotLight->outerSpotAngle;
				data.spotInnerAngle = spotLight->innerSpotAngle;
			}
#endif
			return data;
		}

		RE::BSShaderManager::State& GetShaderManagerState()
		{
#if defined(SKYRIM)
			return RE::BSShaderManager::State::GetSingleton();
#elif defined(FALLOUT4)
			static REL::Relocation<RE::BSShaderManager::State*> singleton{ REL::VariantID(1287208, 2712479) };
			return *singleton;
#endif
		}

		bool IsExteriorCell(RE::TESObjectCELL* a_cell)
		{
#if defined(SKYRIM)
			return a_cell->IsExteriorCell();
#elif defined(FALLOUT4)
			return a_cell->IsExterior();
#endif
		}

		RE::EXTERIOR_DATA* GetCellExteriorData(RE::TESObjectCELL* a_cell)
		{
#if defined(SKYRIM)
			return a_cell->GetRuntimeData().cellData.exterior;
#elif defined(FALLOUT4)
			return a_cell->cellData.exterior;
#endif
		}

		ID3D11Texture2D* GetMainDepthStencilTexture()
		{
#if defined(SKYRIM)
			return RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].texture;
#elif defined(FALLOUT4)
			return reinterpret_cast<ID3D11Texture2D*>(RE::BSGraphics::GetRendererData()->depthStencilTargets[0].texture);
#endif
		}

		float2 GetDynamicResolutionRatios()
		{
#if defined(SKYRIM)
			auto& stateRuntime = RE::BSGraphics::State::GetSingleton()->GetRuntimeData();
			return { stateRuntime.dynamicResolutionWidthRatio, stateRuntime.dynamicResolutionHeightRatio };
#elif defined(FALLOUT4)
			static REL::Relocation<RE::BSGraphics::RenderTargetManager*> singleton{ RE::ID::BSGraphics::RenderTargetManager::Singleton };
			return { singleton->dynamicWidthRatio, singleton->dynamicHeightRatio };
#endif
		}

		const RE::BSGraphics::ViewData GetCameraEyeViewData()
		{
#if defined(SKYRIM)
			return RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData().cameraData.getEye();
#elif defined(FALLOUT4)
			static REL::Relocation<RE::BSGraphics::State*> singleton{ RE::ID::BSGraphics::State::Singleton };
			return singleton->cameraState.camViewData;
#endif
		}

		CameraRuntimeData GetCameraRuntimeData()
		{
			CameraRuntimeData data{};
#if defined(SKYRIM)
			auto& runtimeData = RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData();
			auto cameraData = runtimeData.cameraData.getEye();

			data.viewMat = cameraData.viewMat;
			data.projMat = cameraData.projMat;
			data.viewProjMatrixUnjittered = cameraData.viewProjMatrixUnjittered;
			data.previousViewProjMatrixUnjittered = cameraData.previousViewProjMatrixUnjittered;
			data.posAdjust = Util::Math::Float3(runtimeData.posAdjust.getEye());
			data.previousPosAdjust = Util::Math::Float3(runtimeData.previousPosAdjust.getEye());
#elif defined(FALLOUT4)
			auto& state = Util::Adapter::GetGraphicsState();
			const auto* mainCam = RE::Main::WorldRootCamera();

			RE::BSGraphics::CameraStateData* cameraData = nullptr;
			for (auto& cache : state.cameraDataCache)
			{
				if (mainCam == cache.referenceCamera && cache.useJitter) {
					cameraData = &cache;
					break;
				}
			}

			auto& camViewData = cameraData->camViewData;
			data.viewMat = reinterpret_cast<const float4x4&>(camViewData.viewMat);
			data.projMat = reinterpret_cast<const float4x4&>(camViewData.projMat);
			data.viewProjMatrixUnjittered = reinterpret_cast<const float4x4&>(camViewData.viewProjUnjittered);
			data.previousViewProjMatrixUnjittered = reinterpret_cast<const float4x4&>(camViewData.previousViewProjUnjittered);
			data.posAdjust = Util::Math::Float3(cameraData->posAdjust);
			data.previousPosAdjust = Util::Math::Float3(cameraData->previousPosAdjust);
#endif
			return data;
		}

		RE::SceneGraph* GetWorldRootNode()
		{
#if defined(SKYRIM)
			return RE::Main::GetSingleton()->WorldRootNode();
#elif defined(FALLOUT4)
			static REL::Relocation<RE::NiPointer<RE::SceneGraph>*> worldRootPtr{ RE::ID::Main::WorldRootNode };
			return worldRootPtr->get();
#endif
		}

		bool IsNiAVObjectHidden(const RE::NiAVObject* a_object)
		{
#if defined(SKYRIM)
			return a_object->GetFlags().all(RE::NiAVObject::Flag::kHidden);
#elif defined(FALLOUT4)
			return (a_object->GetFlags() & 1) != 0;
#endif
		}

		bool IsMultiBoundNodeAllFail(const RE::BSMultiBoundNode* a_node)
		{
#if defined(SKYRIM)
			return a_node->GetRuntimeData().cullingMode == RE::BSCullingProcess::BSCPCullingType::kAllFail;
#elif defined(FALLOUT4)
			return a_node->cullingMode.all(RE::BSCullingProcess::CullingType::kAllFail);
#endif
		}

		RE::BSGraphics::State& GetGraphicsState()
		{
#if defined(SKYRIM)
			return *RE::BSGraphics::State::GetSingleton();
#elif defined(FALLOUT4)
			// FO4 returns State by value, which is expensive to call repeatedly and makes it non-referenceable.
			// Re-create the singleton logic to get the pointer instead.
			static REL::Relocation<RE::BSGraphics::State*> singleton{ RE::ID::BSGraphics::State::Singleton };
			return *singleton;
#endif
		}

		uint32_t GetGraphicsFrameCount()
		{
#if defined(SKYRIM)
			return GetGraphicsState().frameCount;
#elif defined(FALLOUT4)
			return GetGraphicsState().currentFrame;
#endif
		}

		CESEAdapter::REX::EnumSet<MenuState> GetMenuState()
		{
			CESEAdapter::REX::EnumSet<MenuState> state;
			const auto ui = RE::UI::GetSingleton();
#if defined(SKYRIM)
			state.set(ui->IsMenuOpen(RE::MainMenu::MENU_NAME), MenuState::MainMenu);
			state.set(ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME), MenuState::LoadingMenu);
			state.set(ui->IsMenuOpen(RE::MapMenu::MENU_NAME), MenuState::MapMenu);
#elif defined(FALLOUT4)
			state.set(ui->GetMenuOpen(RE::MainMenu::MENU_NAME), MenuState::MainMenu);
			state.set(ui->GetMenuOpen(RE::LoadingMenu::MENU_NAME), MenuState::LoadingMenu);
			state.set(ui->GetMenuOpen(RE::PipboyMenu::MENU_NAME), MenuState::MapMenu);
#endif
			return state;
		}

		RE::NiSwitchNode* AsSwitchNode(RE::NiNode* node)
		{
#if defined(SKYRIM)
			return node->AsSwitchNode();
#elif defined(FALLOUT4)
			if (node->GetRTTI() == Constants::rtti::NiSwitchNode.get())
				return static_cast<RE::NiSwitchNode*>(node);
			return nullptr;
#endif
		}
		RE::BSPortalGraph* GetPortalGraph(RE::NiNode* node)
		{
			auto ssn = reinterpret_cast<RE::ShadowSceneNode*>(node);
#if defined(SKYRIM)
			return ssn->GetRuntimeData().portalGraph;
#elif defined(FALLOUT4)
			return ssn->portalGraph;
#endif
		}

		float4 GetShaderManagerLoadedRange()
		{
#if defined(SKYRIM)
			return *reinterpret_cast<const float4*>(&RE::BSShaderManager::State::GetSingleton().loadedRange);
#elif defined(FALLOUT4)
			const auto& state = GetShaderManagerState();
			return *reinterpret_cast<const float4*>(reinterpret_cast<const std::uint8_t*>(&state) + 0x44); // State::loadedRange
#endif
		}

		RE::ShadowSceneNode* GetShadowSceneNode(uint32_t index)
		{
#if defined(SKYRIM)
			return RE::BSShaderManager::State::GetSingleton().shadowSceneNode[index];
#elif defined(FALLOUT4)
			const auto& state = GetShaderManagerState();
			return *reinterpret_cast<RE::ShadowSceneNode* const*>(reinterpret_cast<const std::uint8_t*>(&state) + index * sizeof(void*));
#endif
		}

		RE::NiIntegersExtraData* GetIntegersExtraData(RE::BSTriShape* a_triShape, const char* a_name)
		{
#if defined(SKYRIM)
			return a_triShape->GetExtraData<RE::NiIntegersExtraData>(a_name);
#elif defined(FALLOUT4)
			return reinterpret_cast<RE::NiIntegersExtraData*>(a_triShape->GetExtraData(a_name));
#endif
		}

		RE::TESObjectREFR* GetUserData(RE::NiAVObject* object)
		{
#if defined(SKYRIM)
			return object->GetUserData();
#elif defined(FALLOUT4)
			return reinterpret_cast<RE::TESObjectREFR*>(object->userData);
#endif
		}

		bool IsSpotLight(const RE::TESObjectLIGH* a_light)
		{
#if defined(SKYRIM)
			return a_light->data.flags.any(RE::TES_LIGHT_FLAGS::kSpotlight, RE::TES_LIGHT_FLAGS::kSpotShadow);
#elif defined(FALLOUT4)
			constexpr auto spotMask = static_cast<std::uint32_t>(RE::TES_LIGHT_FLAGS::kSpotlight) |
			                          static_cast<std::uint32_t>(RE::TES_LIGHT_FLAGS::kSpotShadow);
			return (a_light->data.flags & spotMask) != 0;
#endif
		}

		RE::BSMultiBoundNode* AsMultiBoundNode(RE::NiNode* a_node)
		{
#if defined(SKYRIM)
			return a_node->AsMultiBoundNode();
#elif defined(FALLOUT4)
			return a_node->IsMultiBoundNode();
#endif
		}
	}
}
