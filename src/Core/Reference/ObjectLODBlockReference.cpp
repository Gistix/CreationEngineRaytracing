#include "ObjectLODBlockReference.h"
#include "Util.h"
#include "Types/RE/RE.h"

void ObjectLODBlockReference::UpdateVisibility()
{
	if (m_Detached) {
		if (block->attached)
			logger::info("ObjectLODBlockReference::UpdateVisibility - Detached object reference has attached block");

		return;
	}

	if (!block->attached) {
		logger::info("ObjectLODBlockReference::UpdateVisibility - Attached object reference has detached block");
		return;
	}

	if (!block->chunk) {
		logger::info("ObjectLODBlockReference::UpdateVisibility - Chunk is nullptr");
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