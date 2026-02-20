#pragma once

#include "core/Model.h"
#include "core/Instance.h"

struct SceneGraph
{
	eastl::unordered_map<RE::BSDismemberSkinInstance*, eastl::vector<Mesh*>> dismemberReferences;

	// Model Path, Model data ptr
	eastl::unordered_map<eastl::string, eastl::unique_ptr<Model>> models;

	// Root node ptr, Instance data
	eastl::vector<eastl::unique_ptr<Instance>> instances;

	eastl::unordered_map<RE::NiAVObject*, Instance*> instanceNodes;
	eastl::unordered_map<RE::FormID, eastl::vector<Instance*>> instancesFormIDs;

	eastl::deque<eastl::string> msnConvertionQueue;

	void CreateModel(RE::TESForm* form, const char* model, RE::NiAVObject* root);
	void CreateActorModel(RE::Actor* actor, const char* name, RE::NiAVObject* root);
	void CreateLandModel(RE::TESObjectLAND* land);

private:
	void CreateModelInternal(RE::TESForm* form, const char* path, RE::NiAVObject* node);
	void AddInstance(RE::FormID formID, RE::NiAVObject* node, eastl::string path);
};