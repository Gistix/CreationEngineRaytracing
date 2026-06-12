#include "TerrainLODBlockReference.h"
#include "Util.h"
#include "Types/RE/RE.h"

void TerrainLODBlockReference::UpdateIntersection()
{
#if defined(SKYRIM)
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
#else
	prevIntersecting = intersecting;
	intersecting = false;
#endif
}

void TerrainLODBlockReference::UpdateVisibility()
{
	if (m_Attached != block->attached) {
		SetAttached(block->attached);
	}

	if (!m_Attached) {
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