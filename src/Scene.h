#pragma once

#include "core/Mesh.h"
#include "core/Model.h"
#include "SceneGraph.h"
#include "Types/RendererParams.h"

struct Scene
{
	eastl::unique_ptr<SceneGraph> m_SceneGraph;

	std::recursive_mutex shareTextureMutex;
	bool shareTexture = false;

	struct LightSettings
	{
		bool LodDimmer = false;
	};

	struct Settings
	{
		bool Enabled = true;
		bool PathTracing = true;
		LightSettings LightSettings;
	} settings;

	Scene()
	{
		m_SceneGraph = eastl::make_unique<SceneGraph>();
	}

	static Scene* GetSingleton()
	{
		static Scene singleton;
		return &singleton;
	}

	SceneGraph* GetSceneGraph() const;

	bool Initialize(RendererParams rendererParams);

	void Update(nvrhi::ICommandList* commandList);

	void AttachModel(RE::TESForm* form);

	void AttachLand(RE::TESForm* form, RE::NiAVObject* root);

	void AddLight(RE::BSLight* light);

	void RemoveLight(const RE::NiPointer<RE::BSLight>& a_light);
};