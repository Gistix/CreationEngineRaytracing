#include "core/Instance.h"
#include "Util.h"
#include "Renderer.h"
#include "Scene.h"

void Instance::SetDetached(bool detached)
{
	m_State.set(detached, State::Detached);
}

bool Instance::IsDetached() const
{
	return m_State.all(State::Detached);
}
bool Instance::IsHidden() const
{
	return m_State.any(State::Detached, State::FirstPersonHidden, State::DistanceHidden) || node->GetFlags().all(RE::NiAVObject::Flag::kHidden);
}

bool Instance::SkipUpdate()
{
	auto* renderer = Renderer::GetSingleton();
	auto& settings = renderer->m_Settings;

	auto frameIndex = renderer->GetFrameIndex();

	if (settings.VariableUpdateRate)
	{
		const uint64_t delta = frameIndex - m_LastUpdate;

		float3 cameraPosition = Scene::GetSingleton()->GetCameraData()->Position;
		float3 instanceCenter = Util::Math::Float3(node->worldBound.center);

		const float distance = Util::Units::GameUnitsToMeters(float3::Distance(cameraPosition, instanceCenter));

		const uint64_t interval = Renderer::GetUpdateInterval(distance);

		if (delta < interval)
			return true;
	}

	m_LastUpdate = frameIndex;

	return false;
}

void Instance::Update(uint32_t tlasInstanceID)
{
	auto* player = RE::PlayerCharacter::GetSingleton();

	// TODO: Update logic for first person model support (both shares the same form id of the player)
	if (Util::IsPlayerFormID(formID)) {
		m_State.set(!player->Is3rdPersonVisible(), State::FirstPersonHidden);
	}

	if (model->GetMeshFlags().none(Mesh::Flags::Landscape, Mesh::Flags::Water) && !IsDetached())
	{
		auto distance = player->Get3D(false)->world.translate.GetDistance(node->world.translate);
		bool hide = distance > 40000;

		if (hide && m_State.none(State::DistanceHidden))
			logger::warn("Instance::Update - Instance of {} hidden due to its distance from player of: {}", model->m_Name, distance);

		m_State.set(hide, State::DistanceHidden);
	}

	if (IsHidden())
		return;

	// Instance has already been updated this frame
	if (SkipUpdate())
		return;

	if (memcmp(&m_NiTransform, &node->world, sizeof(RE::NiTransform)) != 0)
		m_DirtyFlags |= DirtyFlags::Transform;

	m_DirtyFlags |= model->GetDirtyFlags().get();

	if (m_DirtyFlags != DirtyFlags::None)
		logger::trace("Instance::Update - {}: {}", model->m_Name, Util::GetFlagsString<DirtyFlags>(static_cast<uint8_t>(m_DirtyFlags)));

	// Update transform for BLAS instance
	XMStoreFloat3x4(&m_Transform, Util::Math::GetXMFromNiTransform(node->world));
	XMStoreFloat3x4(&m_PrevTransform, Util::Math::GetXMFromNiTransform(node->previousWorld));

	m_NiTransform = node->world;

	m_TLASInstanceID = tlasInstanceID;
}