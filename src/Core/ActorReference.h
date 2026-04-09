#pragma once

#include <PCH.h>
#include "Core/Instance.h"

struct BipObjectReference
{
	BipObjectReference() = default;

	BipObjectReference(RE::BIPOBJECT& object)
	{
		item = object.item;
		addon = object.addon;
		part = object.part;
		partClone = object.partClone.get();

		// Store form type for later comparison, since item pointer may become invalid
		formType = object.item ? object.item->GetFormType() : RE::FormType::None;
	}

	bool operator==(const BipObjectReference& other) const
	{
		return item == other.item &&
			addon == other.addon &&
			part == other.part &&
			partClone == other.partClone;
	}

	bool operator!=(const BipObjectReference& other) const
	{
		return !(*this == other);
	}

	bool IsValid() const
	{
		return item != nullptr && partClone != nullptr;
	}

	RE::TESForm* item;
	RE::TESObjectARMA* addon;
	RE::TESModel* part;
	RE::NiAVObject* partClone;

	RE::FormType formType;
};

struct ActorReference
{
	ActorReference(RE::Actor* actor, bool firstPerson, eastl::array<eastl::vector<Mesh*>, RE::BIPED_OBJECTS::kTotal> meshes) 
		: m_Actor(actor), m_FirstPerson(firstPerson), m_Meshes(meshes)
	{
		auto* biped = m_Actor->GetBiped(false).get();

		if (!biped)
			return;

		for (size_t i = 0; i < RE::BIPED_OBJECT::kTotal; i++)
		{
			auto& object = biped->objects[i];

			m_Objects[i] = { object };
		}

		m_Biped = true;
	}

	void Update();

	RE::Actor* m_Actor;

	bool m_Biped;

	bool m_FirstPerson;

	BipObjectReference m_Objects[RE::BIPED_OBJECTS::kTotal];

	eastl::array<eastl::vector<Mesh*>, RE::BIPED_OBJECTS::kTotal> m_Meshes;
};