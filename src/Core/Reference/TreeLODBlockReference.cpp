#if defined(SKYRIM)

#include "Core/Reference/TreeLODBlockReference.h"
#include "Util.h"
#include "Types/RE/RE.h"

void TreeLODBlockReference::AddTreeInstanceData(RE::BGSDistantTreeBlock::InstanceData* a_treeInstanceData)
{
	treeInstanceData.push_back(a_treeInstanceData);
}

void TreeLODBlockReference::UpdateVisibility()
{
	if (m_Attached != block->attached) {
		SetAttached(block->attached);
	}

	if (!m_Attached) {
		return;
	}

	//logger::info("Block: 0x{:08X}, Groups: {}", reinterpret_cast<uintptr_t>(block), block->treeGroups.size());

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

#endif