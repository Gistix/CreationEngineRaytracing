#include "Core/InstanceManager.h"
#include "Scene.h"
#include "SceneGraph.h"

void InstanceManager::EraseModel(Instance* instance)
{
    if (auto* model = instance->model) {
        auto refCount = model->Release();
        instance->model = nullptr;

        if (refCount <= 0) {
            Scene::GetSingleton()->GetSceneGraph()->ReleaseModel(model);
        }
    }
}