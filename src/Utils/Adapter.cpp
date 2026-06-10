#include "Adapter.h"

namespace Util
{
	namespace Adapter::CLib
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

		RE::BIPOBJECT* GetBipedObjects(RE::BipedAnim* a_bipedAnim)
		{
#if defined(SKYRIM)
			return &a_bipedAnim->objects[0];
#elif (FALLOUT4)
			return &a_bipedAnim->object[0];
#endif		
		}

		RE::BSGeometry* AsGeometry(RE::NiAVObject* a_object) {
#if defined(SKYRIM)
			return a_object->AsGeometry();
#elif (FALLOUT4)
			return a_object->IsGeometry();
#endif
		}

		RE::NiNode* AsNode(RE::NiAVObject* a_object) {
#if defined(SKYRIM)
			return a_object->AsNode();
#elif (FALLOUT4)
			return a_object->IsNode();
#endif		
		}

		RE::BSFadeNode* AsFadeNode(RE::NiAVObject* a_object) {
#if defined(SKYRIM)
			return a_object->AsFadeNode();
#elif (FALLOUT4)
			return a_object->IsFadeNode();
#endif		
		}

		RE::BSSubIndexTriShape* AsSubIndexTriShape(RE::BSGeometry* a_geometry)
		{
#if defined(SKYRIM)
			return a_geometry->AsSubIndexTriShape();
#elif (FALLOUT4)
			return a_geometry->IsSubIndexTriShape();
#endif	
		}

		RE::NiTObjectArray<RE::NiPointer<RE::NiAVObject>>& GetChildren(RE::NiNode* a_node) {
#if defined(SKYRIM)
			return a_node->GetChildren();
#elif (FALLOUT4)
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
	}
}