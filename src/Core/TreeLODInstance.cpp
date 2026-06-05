#include "core/TreeLODInstance.h"

void TreeLODInstance::UpdateTransform()
{
	half3 position;
	std::memcpy(&position, &m_Data->x, sizeof(half3));

	half rotZ;
	std::memcpy(&rotZ, &m_Data->rotZ, sizeof(half));

	half scale;
	std::memcpy(&scale, &m_Data->scale, sizeof(half));

	auto instanceTransform = RE::NiTransform();
	instanceTransform.rotate = RE::NiMatrix3(0.0f, 0.0f, rotZ * (180.0f / std::numbers::pi_v<float>));
	instanceTransform.translate = RE::NiPoint3(position.x, position.y, position.z);
	instanceTransform.scale = scale;

	auto worldTransform = m_Node->local * instanceTransform;

	// Update previous transform
	m_PrevTransform = m_Transform;

	float3x4 transform;
	XMStoreFloat3x4(&transform, Util::Math::GetXMFromNiTransform(worldTransform));

	if (Util::Math::MatrixNearEqual(transform, m_Transform))
		return;

	m_DirtyFlags |= DirtyFlags::Transform;

	// Update transform
	m_Transform = transform;
}