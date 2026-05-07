#include "Core/Reference/TreeLODBlockReference.h"
#include "Util.h"
#include "Types/RE/RE.h"

void TreeLODBlockReference::UpdateVisibility()
{
	if (detached)
		return;

	if (!block->attached) {
		logger::info("Unattached object block");
		return;
	}

	auto numInstances = instances.size();

	if (numInstances != treeInstanceData.size()) {
		logger::critical("Mismatch in number of instances and instances data");
		return;
	}

	for (size_t i = 0; i < numInstances; i++)
	{
		auto* instance = instances[i];
		auto* instanceData = treeInstanceData[i];

		instance->SetLODHidden(instanceData->hidden);
	}
}