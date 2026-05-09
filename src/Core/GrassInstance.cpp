#include "core/GrassInstance.h"

void GrassInstance::UpdateTransform()
{
	half3 position;
	std::memcpy(&position, &m_Data.x, sizeof(half3));

	//half rotZ;
	//std::memcpy(&rotZ, &m_Data->, sizeof(half));

	//half scale;
	//std::memcpy(&scale, &m_Data->heightScale, sizeof(half));

	auto instanceTransform = RE::NiTransform();
	//instanceTransform.rotate = RE::NiMatrix3(0.0f, 0.0f, rotZ * (180.0f / std::numbers::pi_v<float>));
	instanceTransform.translate = RE::NiPoint3(position.x, position.y, position.z);
	instanceTransform.scale = 1.0f;

	auto worldTransform = m_Node->local * instanceTransform;

	/*logger::info("Position: {}", float3(position));
	logger::info("Local: {}", Util::Math::Float3(m_Node->local.translate));
	logger::info("World: {}", Util::Math::Float3(worldTransform.translate));*/

	if (memcmp(&m_NiTransform, &worldTransform, sizeof(RE::NiTransform)) != 0)
		m_DirtyFlags |= DirtyFlags::Transform;

	m_DirtyFlags |= model->GetDirtyFlags().get();

	// Update transform for BLAS instance
	XMStoreFloat3x4(&m_Transform, Util::Math::GetXMFromNiTransform(worldTransform));
	XMStoreFloat3x4(&m_PrevTransform, Util::Math::GetXMFromNiTransform(m_NiTransform));

	m_NiTransform = worldTransform;
}