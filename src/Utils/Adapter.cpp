#include "Adapter.h"

namespace Util
{
	namespace Adapter
	{
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

		const char* GetName(RE::TESForm* a_form)
		{
#if defined(SKYRIM)
			return a_form->GetName();
#elif defined(FALLOUT4)
			return a_form->GetFullName();
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

		RE::TESObjectREFR* GetOwner(RE::NiAVObject* a_object)
		{
#if defined(SKYRIM)
			return REL::RelocateMember<RE::TESObjectREFR*>(a_object, 0x0F8, 0x110);
#elif defined(FALLOUT4)
			return;
#endif	
		}
		
		RE::NiTObjectArray<RE::NiPointer<RE::NiAVObject>>& GetChildren(RE::NiNode* a_node) {
#if defined(SKYRIM)
			return a_node->GetChildren();
#elif defined(FALLOUT4)
			return a_node->children;
#endif		
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

#if defined(SKYRIM)
		RE::NiSkinInstance* GetSkinInstance(RE::BSGeometry* geometry)
		{
			return geometry->GetGeometryRuntimeData().skinInstance.get();
		}
#elif defined(FALLOUT4)
		RE::BSSkin::Instance* GetSkinInstance(RE::BSGeometry* geometry)
		{
			return geometry->skinInstance.get();
		}
#endif

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
			data.specular = { 0.0f, 0.0f, 0.0f };
			data.radius = rd.radius;
			data.fade = rd.fade;
			data.fadeZone = 0.0f;
#elif defined(FALLOUT4)
			data.ambient.r = a_light->amb.r;
			data.ambient.g = a_light->amb.g;
			data.ambient.b = a_light->amb.b;
			data.diffuse.r = a_light->diff.r;
			data.diffuse.g = a_light->diff.g;
			data.diffuse.b = a_light->diff.b;
			data.specular.r = a_light->spec.r;
			data.specular.g = a_light->spec.g;
			data.specular.b = a_light->spec.b;
			data.radius = { 1.0f, 1.0f, 1.0f };
			data.fade = a_light->dimmer;
			data.fadeZone = 0.0f;
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
			auto* raw = reinterpret_cast<const float*>(rd);
			data.spotOuterAngle = raw[3];
			data.spotInnerAngle = raw[4];
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
			static REL::Relocation<RE::BSShaderManager::State*> singleton{ REL::ID(1287208) };
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
	}
}