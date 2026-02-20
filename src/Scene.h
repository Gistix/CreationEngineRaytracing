#pragma once

#include "core/Mesh.h"
#include "core/Model.h"
#include "SceneGraph.h"

struct Scene
{
	eastl::unique_ptr<SceneGraph> m_SceneGraph;

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

	void Update(nvrhi::ICommandList* commandList);

	void AttachModel(RE::TESForm* form);

	void AttachLand(RE::TESForm* form, RE::NiAVObject* root);
};