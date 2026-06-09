#include "Core/Reference/LODBlockReference.h"
#include "Scene.h"
#include "SceneGraph.h"

LODBlockReference::~LODBlockReference()
{
	Scene::GetSingleton()->GetSceneGraph()->ReleaseInstances(instances, true);
}

void LODBlockReference::SetAttached(bool attached)
{
	for (auto& instance : instances) {
		instance->SetDetached(!attached);
	}

	m_Attached = attached;
}

void LODBlockReference::AddInstance(Instance* instance)
{
	instance->SetDetached(!m_Attached);
	instances.push_back(instance);
}