#pragma once

namespace Util
{
	namespace Adapter::CLib
	{
#if defined(SKYRIM)
#	define NiRTTI(name) RE::NiRTTI_##name
#elif defined(FALLOUT4)
#	define NiRTTI(name) RE::Ni_RTTI::name
#endif

		RE::BSGeometry* AsGeometry(RE::NiAVObject* a_object);
		RE::NiNode* AsNode(RE::NiAVObject* a_object);
		RE::BSFadeNode* AsFadeNode(RE::NiAVObject* a_object);
		RE::NiTObjectArray<RE::NiPointer<RE::NiAVObject>>& GetChildren(RE::NiNode* a_node);

		uint8_t* GetVertexData(RE::BSGraphics::TriShape* rendererData);
		uint16_t* GetIndexData(RE::BSGraphics::TriShape* rendererData);

#if defined(SKYRIM)
		RE::NiSkinInstance* GetSkinInstance(RE::BSGeometry* geometry);
#elif defined(FALLOUT4)
		RE::BSSkin::Instance* GetSkinInstance(RE::BSGeometry* geometry);
#endif
	}
}