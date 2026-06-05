#include "core/GrassInstance.h"

void GrassInstance::UpdateTransform()
{
	float3 scaleMask = float3::One;
	if (model->GetShaderFlags().none(RE::BSShaderProperty::EShaderPropertyFlag::kUniformScale)) {
		scaleMask.x = 0;
		scaleMask.y = 0;
	}

	const float3 axisScale = m_Data.heightScale * scaleMask + float3(1.0f, 1.0f, 1.0f);

	const float3 row0 = m_Data.rot1;
	const float3 row1(m_Data.rot2.y, m_Data.rot2.z, m_Data.rot3.x);
	const float3 row2(m_Data.rot3.z, m_Data.rot2.x, m_Data.rot3.y);
	const float3 translation = m_Data.position;

	// Update previous transform
	m_PrevTransform = m_Transform;

	float3x4 instanceTransform(
		row0.x * axisScale.x, row0.y * axisScale.y, row0.z * axisScale.z, translation.x,
		row1.x * axisScale.x, row1.y * axisScale.y, row1.z * axisScale.z, translation.y,
		row2.x * axisScale.x, row2.y * axisScale.y, row2.z * axisScale.z, translation.z);

	const auto localTransform = Util::Math::GetXMFromNiTransform(m_Node->local);
	const auto grassTransform = DirectX::XMLoadFloat3x4(&instanceTransform);

	float3x4 transform;
	DirectX::XMStoreFloat3x4(&transform, DirectX::XMMatrixMultiply(localTransform, grassTransform));

	if (Util::Math::MatrixNearEqual(transform, m_Transform))
		return;

	m_DirtyFlags |= DirtyFlags::Transform;

	// Update transform
	m_Transform = transform;
}
