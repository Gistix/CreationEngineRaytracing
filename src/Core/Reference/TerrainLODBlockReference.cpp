#include "TerrainLODBlockReference.h"
#include "Util.h"
#include "Types/RE/RE.h"

void TerrainLODBlockReference::UpdateIntersection()
{
	if (block->node->GetLODLevel() != 4)
		return;

	const float4 loadedRange = *reinterpret_cast<float4*>(&RE::BSShaderManager::State::GetSingleton().loadedRange);
	const float2 loadedPosition = { loadedRange.x , loadedRange.y };
	const float2 loadedExtents = { loadedRange.z , loadedRange.w };

	auto multibound = block->chunk->GetRuntimeData().multiBound.get();
	if (!multibound || !multibound->data)
		return;

	static REL::Relocation<const RE::NiRTTI*> multiboundAABBRTTI{ NiRTTI(BSMultiBoundAABB) };
	if (multibound->data->GetRTTI() != multiboundAABBRTTI.get())
		return;

	auto* multiboundAABB = reinterpret_cast<RE::BSMultiBoundAABB*>(multibound->data.get());

	const float2 lodPosition = { multiboundAABB->center.x, multiboundAABB->center.y };
	const float2 lodSize = { multiboundAABB->size.x, multiboundAABB->size.y };

	prevIntersecting = intersecting;

	intersecting = Util::Math::Intersects(loadedPosition, loadedExtents * 2.0f, lodPosition, lodSize);
}

void TerrainLODBlockReference::UpdateVisibility()
{
	if (m_Detached) {
		if (block->attached)
			logger::info("TerrainLODBlockReference::UpdateVisibility - Detached object reference has attached block");

		return;
	}

	if (!block->attached) {
		logger::info("TerrainLODBlockReference::UpdateVisibility - Attached object reference has detached block");
		return;
	}

	if (!block->chunk) {
		logger::info("TerrainLODBlockReference::UpdateVisibility - Chunk is nullptr");
		return;
	}

	bool hidden = Util::Game::IsHidden(block->chunk);

	if (!hidden)
		UpdateIntersection();

	if (m_Hidden == hidden)
		return;

	for (auto* instance: instances)
	{
		instance->SetLODHidden(hidden);
	}

	m_Hidden = hidden;
}