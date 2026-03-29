#include "RenderGraph.h"
#include "Renderer.h"

void FrameGraphBuilder::Compile()
{
	auto& sharedFrameResources = m_Renderer->GetSharedFrameResources();
	auto& resourceManager = m_Renderer->GetResourceManager();

	sharedFrameResources = {};

	for (size_t i = 0; i < m_TextureEntries.size(); ++i)
	{
		const TextureEntry& entry = m_TextureEntries[i];
		if (!entry.hasDescriptor || entry.firstPass == 0)
			continue;

		nvrhi::TextureHandle texture = resourceManager.AcquireTextureForPassRange(
			entry.desc,
			entry.firstPass,
			entry.lastPass);

		switch (static_cast<FrameGraphTextureId>(i))
		{
		case FrameGraphTextureId::NrdDiffuseRadianceHitDistance:
			sharedFrameResources.nrdDiffuseRadianceHitDistance = texture;
			break;
		case FrameGraphTextureId::NrdSpecularRadianceHitDistance:
			sharedFrameResources.nrdSpecularRadianceHitDistance = texture;
			break;
		case FrameGraphTextureId::Count:
			break;
		}
	}
}

RenderGraph::RenderGraph(Renderer* renderer)
{
	m_Renderer = renderer;

	m_RootNode = eastl::make_unique<RootRenderNode>();
}

void RenderGraph::ResolutionChanged(uint2 resolution)
{
	if (m_RootNode)
		m_RootNode->ResolutionChanged(resolution);
}

void RenderGraph::SettingsChanged(const Settings& settings)
{
	if (m_RootNode)
		m_RootNode->SettingsChanged(settings);
}

void RenderGraph::Setup(const Settings& settings)
{
	if (!m_RootNode)
		return;

	FrameGraphBuilder builder(m_Renderer);
	m_RootNode->Setup(builder, settings);
	builder.Compile();
}

void RenderGraph::Execute(nvrhi::ICommandList* commandList)
{
	if (m_RootNode)
		m_RootNode->Execute(commandList);
}
