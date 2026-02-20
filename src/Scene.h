#pragma once

#include "core/Mesh.h"
#include "core/Model.h"

struct Scene
{

	static Scene* GetSingleton()
	{
		static Scene singleton;
		return &singleton;
	}

	void AttachModel(RE::TESForm* form);

	void AttachLand(RE::TESForm* form, RE::NiAVObject* root);
};