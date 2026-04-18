#include "Core/ActorReference.h"

#include "Scene.h"
#include "SceneGraph.h"

void ActorReference::Update()
{
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	auto* faceNode = m_Actor->GetFaceNodeSkinned();

	if (faceNode != m_FaceNode) {
		if (m_FaceNode)
			sceneGraph->ActorUnequip(m_Actor, m_FaceMeshes, m_FirstPerson);

		if (faceNode)
			sceneGraph->ActorEquip(m_Actor, m_Actor, faceNode, m_FaceMeshes, m_FirstPerson);

		m_FaceNode = faceNode;
	}

	if (m_Biped) {
		if (auto* biped = m_Actor->GetBiped(m_FirstPerson).get()) {
			for (size_t i = 0; i < RE::BIPED_OBJECT::kTotal; i++)
			{
				auto& object = biped->objects[i];

				const auto& curObject = BipObjectReference(object);
				auto& prevObject = m_Objects[i];

				// Current and previous objects are different
				if (curObject != prevObject) {

					// Remove previous valid object
					if (prevObject.IsValid()) {
						sceneGraph->ActorUnequip(m_Actor, m_ObjectMeshes[i], m_FirstPerson);
						m_ObjectMeshes[i].clear();
					}

					// Add current valid object
					if (curObject.IsValid())
						sceneGraph->ActorEquip(m_Actor, curObject.item, curObject.partClone, m_ObjectMeshes[i], m_FirstPerson);

					prevObject = curObject;
				}
			}
		}
	}
}

void ActorReference::AttachAnimObject(RE::TESObjectANIO* animObject, RE::NiAVObject* object)
{
	if (m_AnimatedObjectMeshes.find(animObject) != m_AnimatedObjectMeshes.end())
		return;

	auto [it, emplaced] = m_AnimatedObjectMeshes.try_emplace(animObject);

	if (!emplaced)
		return;

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
	sceneGraph->ActorEquip(m_Actor, animObject, object, it->second, m_FirstPerson);
}

void ActorReference::DetachAnimObject(RE::TESObjectANIO* animObject)
{
	auto it = m_AnimatedObjectMeshes.find(animObject);

	if (it == m_AnimatedObjectMeshes.end())
		return;

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
	sceneGraph->ActorUnequip(m_Actor, it->second, m_FirstPerson);

	m_AnimatedObjectMeshes.erase(it);
}