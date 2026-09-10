#include "Core/Mesh/GrassMesh.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Util.h"
#include "Types/RE/RE.h"

GrassMesh::GrassMesh(RE::BSTriShape* a_sourceShape, [[maybe_unused]] nvrhi::ICommandList* a_commandList)
{
	m_Name = MakeDebugName(a_sourceShape);
	m_BSTriShape = a_sourceShape;
	m_Type = Type::Default;
	m_WorldBound = a_sourceShape->worldBound;
	m_Transform = Constants::kIdentityTransform;
	m_PrevTransform = Constants::kIdentityTransform;
	m_NeedsPrevInit = false;
}

void GrassMesh::Update([[maybe_unused]] nvrhi::ICommandList* a_commandList)
{
	if (!m_BSTriShape)
		return;

	// The group children own the BLASes; the parent only tracks the shape's bound.
	m_WorldBound = m_BSTriShape->worldBound;
}
