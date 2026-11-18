#include "Types/TreeAnim.h"
#include "Util.h"
#include "Types/RE/RE.h"

bool TreeAnimState::Update([[maybe_unused]] RE::BSShaderProperty* shaderProperty, float4& outParams)
{
#if defined(SKYRIM)
	const auto treeAnim = Util::Adapter::GetTreeAnimParams(shaderProperty);

	// TreeParams.w * TreeParams.y, TreeParams.z, WindTimers.x
	outParams = float4(treeAnim.frequency * treeAnim.windMagnitude, treeAnim.amplitude, treeAnim.timer, 0.0f);
#else
	outParams = float4(0.0f, 0.0f, 0.0f, 0.0f);
#endif

	// The game stops the leaf timer while the node is culled, so frozen params mean no GPU work.
	const bool changed = outParams.x != m_LastParams.x || outParams.y != m_LastParams.y || outParams.z != m_LastParams.z;
	if (!changed && !m_NeedsSettle)
		return false;

	m_NeedsSettle = changed;
	m_LastParams = outParams;
	return true;
}
