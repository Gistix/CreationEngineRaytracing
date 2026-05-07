#include "LODBlockReference.h"
#include "Util.h"
#include "Types/RE/RE.h"

void LODBlockReference::UpdateVisibility()
{
	if (detached)
		return;

	RE::BSMultiBoundNode* object = nullptr;
	bool hidden = false;

	if (auto terrainBlockPtr = get_if<RE::BGSTerrainBlock*>(&block)) {
		auto* terrainBlock = *terrainBlockPtr;

		if (!terrainBlock->attached) {
			logger::info("Unattached terrain block");
			return;
		}

		object = terrainBlock->chunk;

	} else 	if (auto objectBlockPtr = get_if<RE::BGSObjectBlock*>(&block)) {
		auto* objectBlock = *objectBlockPtr;

		if (!objectBlock->attached) {
			logger::info("Unattached object block");
			return;
		}

		object = objectBlock->chunk;
	}

	if (!object) {
		logger::info("Chunk is nullptr");
		return;
	}

	hidden |= Util::Game::IsHidden(object);

	if (m_Hidden == hidden)
		return;

	for (auto* instance: instances)
	{
		instance->SetLODHidden(hidden);
	}

	m_Hidden = hidden;
}