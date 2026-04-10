#include "Core/ActorReference.h"

#include "Scene.h"
#include "SceneGraph.h"

void ActorReference::Update()
{
	if (!m_Biped)
		return;

	auto* biped = m_Actor->GetBiped(false).get();

	if (!biped)
		return;

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	for (size_t i = 0; i < RE::BIPED_OBJECT::kTotal; i++)
	{
		auto& object = biped->objects[i];

		const auto& curObject = BipObjectReference(object);
		auto& prevObject = m_Objects[i];

		// Current and previous objects are different
		if (curObject != prevObject) {

			// Remove previous valid object
			if (prevObject.IsValid()) {
				sceneGraph->ActorUnequip(m_Actor, m_Meshes[i], m_FirstPerson);
				m_Meshes[i].clear();
			}

			// Add current valid object
			if (curObject.IsValid())
				sceneGraph->ActorEquip(m_Actor, curObject, m_Meshes[i], m_FirstPerson);

			prevObject = curObject;
		}
	}
}