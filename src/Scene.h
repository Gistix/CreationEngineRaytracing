#pragma once

#include "core/Mesh.h"
#include "core/Model.h"
#include "SceneGraph.h"

struct Scene
{
	eastl::unique_ptr<SceneGraph> m_SceneGraph;

	Scene();

	static Scene* GetSingleton()
	{
		static Scene singleton;
		return &singleton;
	}

	SceneGraph* GetSceneGraph() const;

	void AttachModel(RE::TESForm* form);

	void AttachLand(RE::TESForm* form, RE::NiAVObject* root);
};