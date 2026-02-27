#include "core/Instance.h"
#include "Util.h"
#include "Renderer.h"
#include "Scene.h"

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

void Instance::Update()
{
	if (memcmp(&m_NiTransform, &node->world, sizeof(RE::NiTransform)) != 0)
		m_DirtyFlags |= DirtyFlags::Transform;

	m_DirtyFlags |= model->GetDirtyFlags();

	if (m_DirtyFlags != DirtyFlags::None)
		logger::trace("Instance::Update - {}: {}", model->m_Name, Util::GetFlagsString<DirtyFlags>(static_cast<uint8_t>(m_DirtyFlags)));

	// Update transform for BLAS instance
	XMStoreFloat3x4(&m_Transform, Util::Math::GetXMFromNiTransform(node->world));
	m_NiTransform = node->world;

	// Instance has already been updated this frame
	if (SkipUpdate())
		return;
}