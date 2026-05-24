#include "Core/Reference/ActorReference.h"

#include "Scene.h"
#include "SceneGraph.h"

void ActorReference::Update(nvrhi::ICommandList* commandList)
{
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	auto* faceNode = m_Actor->GetFaceNodeSkinned();

	if (faceNode != m_FaceNode) {
		if (m_FaceNode)
			sceneGraph->ActorUnequip(m_Instance, m_FaceMeshes, m_FirstPerson);

		if (faceNode)
			sceneGraph->ActorEquip(commandList, m_Instance, m_Actor, faceNode, m_FaceMeshes, m_FirstPerson);

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
						sceneGraph->ActorUnequip(m_Instance, m_ObjectMeshes[i], m_FirstPerson);
						m_ObjectMeshes[i].clear();
					}

					// Add current valid object
					if (curObject.IsValid())
						sceneGraph->ActorEquip(commandList, m_Instance, curObject.item, curObject.partClone, m_ObjectMeshes[i], m_FirstPerson);

					prevObject = curObject;
				}
			}
		}
	}

	// Updade animated objects
	{
		eastl::unordered_map<RE::TESObjectANIO*, RE::NiAVObject*> addQueue;
		eastl::unordered_set<RE::TESObjectANIO*> removeQueue;

		{
			std::scoped_lock lock(m_AnimatedObjectQueueMutex);
			addQueue = eastl::move(m_AnimatedObjectAddQueue);
			removeQueue = eastl::move(m_AnimatedObjectRemoveQueue);
		}

		for (auto& [animObject, object] : addQueue)
		{
			auto [it, emplaced] = m_AnimatedObjectMeshes.try_emplace(animObject);
			if (!emplaced)
				continue;

			sceneGraph->ActorEquip(commandList, m_Instance, animObject, object, it->second, m_FirstPerson);
		}

		for (auto& animObject : removeQueue) {
			auto it = m_AnimatedObjectMeshes.find(animObject);
			if (it == m_AnimatedObjectMeshes.end())
				continue;

			sceneGraph->ActorUnequip(m_Instance, it->second, m_FirstPerson);

			m_AnimatedObjectMeshes.erase(it);
		}
	}
}

void ActorReference::AttachAnimObject(RE::TESObjectANIO* animObject, RE::NiAVObject* object)
{
	std::scoped_lock lock(m_AnimatedObjectQueueMutex);
	m_AnimatedObjectAddQueue.emplace(animObject, object);
	m_AnimatedObjectRemoveQueue.erase(animObject);
}

void ActorReference::DetachAnimObject(RE::TESObjectANIO* animObject)
{
	std::scoped_lock lock(m_AnimatedObjectQueueMutex);
	m_AnimatedObjectRemoveQueue.emplace(animObject);
	m_AnimatedObjectAddQueue.erase(animObject);
}