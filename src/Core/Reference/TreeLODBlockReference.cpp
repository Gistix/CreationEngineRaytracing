#include "Core/Reference/TreeLODBlockReference.h"
#include "Util.h"
#include "Types/RE/RE.h"

void TreeLODBlockReference::UpdateVisibility()
{
	if (m_Detached) {
		if (block->attached)
			logger::info("TreeLODBlockReference::UpdateVisibility - Detached object reference has attached block");

		return;
	}

	if (!block->attached) {
		logger::info("TreeLODBlockReference::UpdateVisibility - Attached object reference has detached block");
		return;
	}

	auto numInstances = instances.size();

	if (numInstances != treeInstanceData.size()) {
		logger::critical("Mismatch in number of instances and instances data");
		return;
	}

	// This could probably be moved into TreeLODInstance::SkipAS
	for (size_t i = 0; i < numInstances; i++)
	{
		auto* instance = instances[i];
		auto* instanceData = treeInstanceData[i];

		instance->SetLODHidden(instanceData->hidden);
	}
}