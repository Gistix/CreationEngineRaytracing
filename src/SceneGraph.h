#pragma once

#include "core/Model.h"
#include "core/Instance.h"
#include "FrameData.hlsli"

struct SceneGraph
{
	eastl::unordered_map<RE::BSDismemberSkinInstance*, eastl::vector<Shape*>> dismemberReferences;

	// Model Path, Model data ptr
	eastl::unordered_map<eastl::string, eastl::unique_ptr<Model>> models;

	// Root node ptr, Instance data
	eastl::vector<eastl::unique_ptr<Instance>> instances;

	eastl::unordered_map<RE::NiAVObject*, Instance*> nodeInstances;
	eastl::unordered_map<RE::FormID, eastl::vector<Instance*>> formIDInstances;

	static SceneGraph* GetSingleton()
	{
		static SceneGraph singleton;
		return &singleton;
	}

	void CreateModel(RE::TESForm* form, const char* model, RE::NiAVObject* root);
	void CreateActorModel(RE::Actor* actor, const char* name, RE::NiAVObject* root);
	void CreateLandModel(RE::TESObjectLAND* land);

private:
	void CreateModelInternal(RE::TESForm* form, const char* path, RE::NiAVObject* pRoot);
};