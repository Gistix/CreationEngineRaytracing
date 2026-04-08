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

	auto* SceneGraph = Scene::GetSingleton()->GetSceneGraph();


	for (size_t i = 0; i < RE::BIPED_OBJECT::kTotal; i++)
	{
		auto& object = biped->objects[i];

		const auto& curObject = BipObjectReference(object);
		auto& prevObject = m_Objects[i];


		if (curObject != prevObject) {
			if (curObject.IsValid())
				SceneGraph->ActorEquip(m_Actor, curObject, m_FirstPerson);

			// The reason why we don't unequip here and use the event instead is because 'partClone' needs to be valid
			// Since for non-weapons we traverse the scene graph for each of its BSGeometry to remove their 'Mesh' from the 'Model'

			prevObject = curObject;
		}
	}
}