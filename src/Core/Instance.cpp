#include "core/Instance.h"
#include "Util.h"
#include "Renderer.h"

bool Instance::SkipUpdate()
{
	auto* renderer = Renderer::GetSingleton();
	auto& settings = renderer->settings;

	if (!settings.VariableUpdateRate)
		return false;

	auto frameIndex = renderer->GetFrameIndex();

	const uint64_t delta = frameIndex - m_LastUpdate;

	float3 cameraPosition = renderer->GetCameraData()->Position;
	float3 instanceCenter = Util::Float3(node->worldBound.center);

	const float distance = Util::Units::GameUnitsToMeters(float3::Distance(cameraPosition, instanceCenter));

	const uint64_t interval = Renderer::GetUpdateInterval(distance);

	if (delta < interval)
		return true;

	m_LastUpdate = frameIndex;

	return false;
}

void Instance::Update()
{
	// Update transform for BLAS instance
	XMStoreFloat3x4(&transform, Util::GetXMFromNiTransform(node->world));

	// Instance has already been updated this frame
	if (SkipUpdate())
		return;
}