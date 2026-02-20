#pragma once

#include "core/Model.h"
#include "core/Instance.h"

class SceneGraph
{
	eastl::unordered_map<RE::BSDismemberSkinInstance*, eastl::vector<Mesh*>> dismemberReferences;

	// Model Path, Model data ptr
	eastl::unordered_map<eastl::string, eastl::unique_ptr<Model>> m_Models;

	// Root node ptr, Instance data
	eastl::vector<eastl::unique_ptr<Instance>> m_Instances;

	eastl::unordered_map<RE::NiAVObject*, Instance*> m_InstanceNodes;
	eastl::unordered_map<RE::FormID, eastl::vector<Instance*>> m_InstancesFormIDs;

	eastl::deque<eastl::string> m_MSNConvertionQueue;

	void CreateModelInternal(RE::TESForm* form, const char* path, RE::NiAVObject* node);
	void AddInstance(RE::FormID formID, RE::NiAVObject* node, eastl::string path);

public:
	inline auto& GetModels() const { return m_Models; }
	inline auto& GetInstances() const { return m_Instances; }

	void CreateModel(RE::TESForm* form, const char* model, RE::NiAVObject* root);
	void CreateActorModel(RE::Actor* actor, const char* name, RE::NiAVObject* root);
	void CreateLandModel(RE::TESObjectLAND* land);
};