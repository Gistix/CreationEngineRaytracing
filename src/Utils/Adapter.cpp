#include "Adapter.h"

namespace Util
{
	namespace Adapter::CLib
	{
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
	}
}