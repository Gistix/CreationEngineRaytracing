#include "LODBlockReference.h"
#include "Util.h"

void LODBlockReference::UpdateVisibility(RE::BSMultiBoundNode* object)
{
	auto hidden = Util::Game::IsHidden(object);

	if (m_Hidden == hidden)
		return;

	for (auto* instance: instances)
	{
		instance->SetLODHidden(hidden);
	}

	m_Hidden = hidden;
}