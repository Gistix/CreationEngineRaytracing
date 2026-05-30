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