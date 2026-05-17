#include "ObjectLODBlockReference.h"
#include "Util.h"
#include "Types/RE/RE.h"

void ObjectLODBlockReference::UpdateVisibility()
{
	if (detached)
		return;

	if (!block->attached) {
		logger::info("Unattached object block");
		return;
	}

	if (!block->chunk) {
		logger::info("Chunk is nullptr");
		return;
	}

	bool hidden = Util::Game::IsHidden(block->chunk);

	if (m_Hidden == hidden)
		return;

	for (auto* instance : instances)
	{
		instance->SetLODHidden(hidden);
	}

	m_Hidden = hidden;
}