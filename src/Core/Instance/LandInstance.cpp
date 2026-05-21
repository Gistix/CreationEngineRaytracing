#include "core/Instance/LandInstance.h"

bool LandInstance::IsHidden() const
{
	if (m_Node->GetFlags().none(RE::NiAVObject::Flag::kAccumulated))
		return true;

	return m_State.any(State::Detached) || m_Node->GetFlags().all(RE::NiAVObject::Flag::kHidden);
}