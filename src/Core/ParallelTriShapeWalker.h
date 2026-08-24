#pragma once

#include "Constants.h"
#include "Types/RE/RE.h"
#include "Utils/Adapter.h"
#include "Utils/Traversal.h"

#include <eastl/vector.h>
#include <eastl/unordered_map.h>
#include <eastl/utility.h>
#include <memory>

class BaseMesh;
class SceneGraph;

class ParallelTriShapeWalker
{
public:
	ParallelTriShapeWalker(
		SceneGraph* a_sceneGraph,
		eastl::vector<eastl::pair<RE::NiAVObject*, RE::TESObjectREFR*>>* a_forkedChildren,
		RE::NiAVObject* a_firstPersonRoot);

	// Routes a leaf into the per-worker buffer [workerIdx].
	void VisitLeaf(RE::BSTriShape* bsTriShape, RE::TESObjectREFR* refr, size_t workerIdx);

	// Walks a single subtree serially, routing leaves into per-worker buffer [workerIdx].
	void ProcessSubtree(RE::NiAVObject* object, RE::TESObjectREFR* parentRefr, size_t workerIdx);

	// Recursive top-down descent. Hit wide NiNodes will have their children appended to forkedChildren.
	void Walk(RE::NiAVObject* root);

private:
	SceneGraph* m_SceneGraph{ nullptr };
	eastl::vector<eastl::pair<RE::NiAVObject*, RE::TESObjectREFR*>>* m_ForkedChildren{ nullptr };
	RE::NiAVObject* m_FirstPersonRoot{ nullptr };
};
