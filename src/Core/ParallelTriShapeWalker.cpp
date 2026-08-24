#include "Core/ParallelTriShapeWalker.h"
#include "SceneGraph.h"
#include "Core/Mesh/BaseMesh.h"
#include "Utils/Traversal.h"

ParallelTriShapeWalker::ParallelTriShapeWalker(
	SceneGraph* a_sceneGraph,
	eastl::vector<eastl::pair<RE::NiAVObject*, RE::TESObjectREFR*>>* a_forkedChildren,
	RE::NiAVObject* a_firstPersonRoot)
	: m_SceneGraph(a_sceneGraph),
	  m_ForkedChildren(a_forkedChildren),
	  m_FirstPersonRoot(a_firstPersonRoot)
{
}

void ParallelTriShapeWalker::VisitLeaf(RE::BSTriShape* bsTriShape, RE::TESObjectREFR* refr, size_t workerIdx)
{
	if (!bsTriShape) {
		logger::critical("[PhaseA-DBG] VisitLeaf[{0}] was passed a NULL bsTriShape (refr={1:p})", workerIdx, static_cast<const void*>(refr));
		return;
	}

	auto it = m_SceneGraph->m_Meshes.find(bsTriShape);
	if (it != m_SceneGraph->m_Meshes.end()) {
		auto mesh = it->second.get();
		m_SceneGraph->m_PerWorkerUpdateList[workerIdx].push_back({ mesh, refr });
		m_SceneGraph->m_PerWorkerCurrentVisible[workerIdx].push_back(mesh);
	} else {
		m_SceneGraph->m_PerWorkerCreateList[workerIdx].push_back({ bsTriShape, refr });
	}
}

void ParallelTriShapeWalker::ProcessSubtree(RE::NiAVObject* object, RE::TESObjectREFR* parentRefr, size_t workerIdx)
{
	Util::Traversal::SceneGraphTriShapes<false>(
		object,
		[this, workerIdx](RE::BSTriShape* bsTriShape, RE::TESObjectREFR* refr) {
			if (!bsTriShape) {
				logger::critical("[PhaseA-DBG] ProcessSubtree[{0} visitor] received NULL bsTriShape from AsTriShape; refr={1:p}",
					workerIdx, static_cast<const void*>(refr));
				return;
			}
			VisitLeaf(bsTriShape, refr, workerIdx);
		},
		parentRefr,
		m_FirstPersonRoot
	);
}

void ParallelTriShapeWalker::Walk(RE::NiAVObject* root)
{
	Util::Traversal::SceneGraphTriShapes<true>(
		root,
		[this](RE::BSTriShape* bsTriShape, RE::TESObjectREFR* refr) {
			// Leaves reached during the serial descent route to slot 0 (no concurrent writers;
			// the worker tasks haven't been dispatched yet).
			VisitLeaf(bsTriShape, refr, 0);
		},
		nullptr, // parentRefr
		m_FirstPersonRoot,
		[this](RE::NiNode* node, RE::TESObjectREFR* refr, const eastl::vector<RE::NiAVObject*>& portalChildren) {
			// Wide fan-out: record children for post-descent chunked parallel processing instead
			// of recursing. This is the deliberate granularity decision: forking 1:1 per child
			// (1,254 tasks in a typical frame) swamps the pool with mutex overhead; chunking here
			// keeps total pool tasks at exactly numWorkers, matching the existing Phase B pattern.
			auto& children = Util::Adapter::GetChildren(node);
			for (auto& child : children) {
				if (child) {
					m_ForkedChildren->push_back({ child.get(), refr });
				}
			}
			for (auto* child : portalChildren) {
				if (child) {
					m_ForkedChildren->push_back({ child, refr });
				}
			}
		}
	);
}
