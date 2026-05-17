#include "core/GrassInstance.h"

void GrassInstance::UpdateTransform()
{
	float3 scaleMask = float3::One;
	if (model->GetShaderFlags().none(RE::BSShaderProperty::EShaderPropertyFlag::kUniformScale)) {
		scaleMask.x = 0;
		scaleMask.y = 0;
	}

	//float3 position = m_Data.position * (m_Data.heightScale * scaleMask + float3(1, 1, 1));

	//auto rotation = RE::NiMatrix3(m_Data.rot1, m_Data.rot2, m_Data.rot3);
	//auto transformedPosition = rotation * RE::NiPoint3(position.x, position.y, position.z);

	auto instanceTransform = RE::NiTransform();
	instanceTransform.rotate = RE::NiMatrix3(m_Data.rot1, m_Data.rot2, m_Data.rot3);
	//instanceTransform.translate = RE::NiPoint3(m_Data.position) + transformedPosition;
	instanceTransform.translate = m_Data.position;
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