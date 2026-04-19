#include "OMMBaker.h"
#include "Renderer.h"
#include "Core/Model.h"
#include "Core/Mesh.h"

OMMBaker::OMMBaker()
{
    auto logger = [](omm::MessageSeverity severity, const char* message)
        {
            switch (severity)
            {
            case omm::MessageSeverity::Info:
                logger::info("{}", message);
                break;
            case omm::MessageSeverity::Warning:
                logger::warn("{}", message);
                break;
            case omm::MessageSeverity::PerfWarning:
                logger::warn("{}", message);
                break;
            case omm::MessageSeverity::Fatal:
                logger::critical("{}", message);
                break;
            default:
                break;
            }
        };

	auto* renderer = Renderer::GetSingleton();
	auto device = renderer->GetDevice();

    auto initCommandList = renderer->GetGraphicsCommandList();
	initCommandList->open();

    m_Baker = eastl::make_unique<omm::GpuBakeNvrhi>(device, initCommandList, true, nullptr, logger);

	initCommandList->close();
	device->executeCommandList(initCommandList);
}

void OMMBaker::Bake(Model* model)
{
	bool requiresBuild = false;
	for (auto& mesh : model->meshes) {
		if (mesh->NeedsOpacityMicromap() && !mesh->ommBuffers.buildAttempted) {
			requiresBuild = true;
			break;
		}
	}

	if (!requiresBuild)
		return;

    std::unique_lock lock(m_OMMBakerMutex);

	auto renderer = Renderer::GetSingleton();
	auto device = renderer->GetDevice();

	auto commandList = renderer->GetGraphicsCommandList();
	commandList->open();

    bool dispatched = false;

    for (auto& mesh : model->meshes)
        dispatched |= RecordBuild(mesh.get(), commandList);

	commandList->close();

	if (!dispatched)
		return;

    device->executeCommandList(commandList, nvrhi::CommandQueue::Graphics);
    device->waitForIdle();

    for (auto& mesh : model->meshes)
        FinalizeBuild(mesh.get());

	m_Baker->Clear();
}

bool OMMBaker::RecordBuild(Mesh* mesh, nvrhi::ICommandList* commandList) {
	if (!mesh->NeedsOpacityMicromap() || mesh->ommBuffers.buildAttempted)
		return false;

	mesh->ommBuffers.buildAttempted = true;

	omm::GpuBakeNvrhi::Input input = {};
	input.operation = omm::GpuBakeNvrhi::Operation::SetupAndBake;
	input.alphaTexture = mesh->material.Textures[0].handle;
	input.alphaTextureChannel = mesh->material.Textures[0].alphaChannel;
	input.alphaCutoff = mesh->material.alphaThreshold;
	input.alphaCutoffGreater = omm::OpacityState::Opaque;
	input.alphaCutoffLessEqual = omm::OpacityState::Transparent;
	input.bilinearFilter = true;
	input.enableLevelLineIntersection = true;
	input.sampleMode = nvrhi::SamplerAddressMode::Clamp;
	input.texCoordFormat = nvrhi::Format::R16_FLOAT;
	input.texCoordBuffer = mesh->buffers.vertexBuffer;
	input.texCoordBufferOffsetInBytes = uint32_t(offsetof(Vertex, Texcoord0));
	input.texCoordStrideInBytes = sizeof(Vertex);
	input.indexBuffer = mesh->buffers.triangleBuffer;
	input.numIndices = mesh->triangleData.count * 3;
	input.maxSubdivisionLevel = Constants::OMM_SUBDIV_LEVEL;
	input.format = nvrhi::rt::OpacityMicromapFormat::OC1_4_State;
	input.dynamicSubdivisionScale = 1.f;
	input.enableSpecialIndices = true;
	input.enableTexCoordDeduplication = true;
	input.computeOnly = false;

	omm::GpuBakeNvrhi::PreDispatchInfo preDispatchInfo = {};
	m_Baker->GetPreDispatchInfo(input, preDispatchInfo);

	auto device = Renderer::GetSingleton()->GetDevice();

	auto createGpuBuffer = [device, this, mesh](size_t byteSize, const char* suffix) -> nvrhi::BufferHandle {
		if (byteSize == 0) {
			logger::warn("createGpuBuffer - {} has size of 0", suffix);
			return nullptr;
		}

		nvrhi::BufferDesc desc;
		desc.byteSize = byteSize;
		desc.canHaveRawViews = true;
		desc.canHaveUAVs = true;
		desc.isAccelStructBuildInput = true;
		desc.initialState = nvrhi::ResourceStates::Common;
		desc.keepInitialState = true;
		desc.debugName = std::format("{} ({})", mesh->m_Name.c_str(), suffix);
		return device->createBuffer(desc);
		};

	auto createReadbackBuffer = [device, this, mesh](size_t byteSize, const char* suffix) -> nvrhi::BufferHandle {
		if (byteSize == 0) {
			logger::warn("createReadbackBuffer - {} has size of 0", suffix);
			return nullptr;
		}

		nvrhi::BufferDesc desc;
		desc.byteSize = byteSize;
		desc.cpuAccess = nvrhi::CpuAccessMode::Read;
		desc.initialState = nvrhi::ResourceStates::Common;
		desc.keepInitialState = true;
		desc.debugName = std::format("{} ({})", mesh->m_Name.c_str(), suffix);
		return device->createBuffer(desc);
		};

	auto& ommBuffers = mesh->ommBuffers;

	ommBuffers.ommIndexFormat = preDispatchInfo.ommIndexFormat;
	ommBuffers.ommArrayBuffer = createGpuBuffer(preDispatchInfo.ommArrayBufferSize, "OMM Array Buffer");
	ommBuffers.ommDescBuffer = createGpuBuffer(preDispatchInfo.ommDescBufferSize, "OMM Desc Buffer");
	ommBuffers.ommIndexBuffer = createGpuBuffer(preDispatchInfo.ommIndexBufferSize, "OMM Index Buffer");
	ommBuffers.ommDescArrayHistogramBuffer = createGpuBuffer(preDispatchInfo.ommDescArrayHistogramSize, "OMM Desc Histogram Buffer");
	ommBuffers.ommIndexHistogramBuffer = createGpuBuffer(preDispatchInfo.ommIndexHistogramSize, "OMM Index Histogram Buffer");
	ommBuffers.ommPostDispatchInfoBuffer = createGpuBuffer(preDispatchInfo.ommPostDispatchInfoBufferSize, "OMM Post Dispatch Info Buffer");

	ommBuffers.ommDescArrayHistogramReadbackBuffer = createReadbackBuffer(preDispatchInfo.ommDescArrayHistogramSize, "OMM Desc Histogram Readback");
	ommBuffers.ommIndexHistogramReadbackBuffer = createReadbackBuffer(preDispatchInfo.ommIndexHistogramSize, "OMM Index Histogram Readback");

	ommBuffers.ommDescArrayHistogramSize = preDispatchInfo.ommDescArrayHistogramSize;
	ommBuffers.ommIndexHistogramSize = preDispatchInfo.ommIndexHistogramSize;

	omm::GpuBakeNvrhi::Buffers output = {};
	output.ommArrayBuffer = ommBuffers.ommArrayBuffer;
	output.ommDescBuffer = ommBuffers.ommDescBuffer;
	output.ommIndexBuffer = ommBuffers.ommIndexBuffer;
	output.ommDescArrayHistogramBuffer = ommBuffers.ommDescArrayHistogramBuffer;
	output.ommIndexHistogramBuffer = ommBuffers.ommIndexHistogramBuffer;
	output.ommPostDispatchInfoBuffer = ommBuffers.ommPostDispatchInfoBuffer;

	m_Baker->Dispatch(commandList, input, output);

	if (ommBuffers.ommDescArrayHistogramReadbackBuffer && ommBuffers.ommDescArrayHistogramBuffer)
		commandList->copyBuffer(ommBuffers.ommDescArrayHistogramReadbackBuffer, 0, ommBuffers.ommDescArrayHistogramBuffer, 0, ommBuffers.ommDescArrayHistogramSize);

	if (ommBuffers.ommIndexHistogramReadbackBuffer && ommBuffers.ommIndexHistogramBuffer)
		commandList->copyBuffer(ommBuffers.ommIndexHistogramReadbackBuffer, 0, ommBuffers.ommIndexHistogramBuffer, 0, ommBuffers.ommIndexHistogramSize);

	return true;
}

bool OMMBaker::FinalizeBuild(Mesh* mesh) {
	auto& ommBuffers = mesh->ommBuffers;

	if (!ommBuffers.buildAttempted || ommBuffers.opacityMicromap)
		return bool(ommBuffers.opacityMicromap);

	auto device = Renderer::GetSingleton()->GetDevice();

	if (ommBuffers.ommDescArrayHistogramReadbackBuffer && ommBuffers.ommDescArrayHistogramSize != 0) {
		std::vector<nvrhi::rt::OpacityMicromapUsageCount> histogram;
		void* data = device->mapBuffer(ommBuffers.ommDescArrayHistogramReadbackBuffer, nvrhi::CpuAccessMode::Read);
		omm::GpuBakeNvrhi::ReadUsageDescBuffer(data, ommBuffers.ommDescArrayHistogramSize, histogram);
		device->unmapBuffer(ommBuffers.ommDescArrayHistogramReadbackBuffer);
		ommBuffers.ommDescArrayHistogram.resize(histogram.size());
		for (size_t i = 0; i < histogram.size(); ++i)
			ommBuffers.ommDescArrayHistogram[i] = histogram[i];
	}

	if (ommBuffers.ommIndexHistogramReadbackBuffer && ommBuffers.ommIndexHistogramSize != 0) {
		std::vector<nvrhi::rt::OpacityMicromapUsageCount> histogram;
		void* data = device->mapBuffer(ommBuffers.ommIndexHistogramReadbackBuffer, nvrhi::CpuAccessMode::Read);
		omm::GpuBakeNvrhi::ReadUsageDescBuffer(data, ommBuffers.ommIndexHistogramSize, histogram);
		device->unmapBuffer(ommBuffers.ommIndexHistogramReadbackBuffer);
		ommBuffers.ommIndexHistogram.resize(histogram.size());
		for (size_t i = 0; i < histogram.size(); ++i)
			ommBuffers.ommIndexHistogram[i] = histogram[i];
	}

	if (ommBuffers.ommDescArrayHistogram.empty() || ommBuffers.ommIndexHistogram.empty())
		return false;

	nvrhi::rt::OpacityMicromapDesc ommDesc;
	ommDesc.debugName = std::format("{} - OMM", mesh->m_Name.c_str());
	ommDesc.flags = nvrhi::rt::OpacityMicromapBuildFlags::FastTrace;
	ommDesc.counts.assign(ommBuffers.ommDescArrayHistogram.begin(), ommBuffers.ommDescArrayHistogram.end());
	ommDesc.inputBuffer = ommBuffers.ommArrayBuffer;
	ommDesc.perOmmDescs = ommBuffers.ommDescBuffer;

	ommBuffers.opacityMicromap = device->createOpacityMicromap(ommDesc);
	if (!ommBuffers.opacityMicromap)
		return false;

	auto commandList = Renderer::GetSingleton()->GetComputeCommandList();
	commandList->open();
	commandList->buildOpacityMicromap(ommBuffers.opacityMicromap, ommDesc);
	commandList->close();
	device->executeCommandList(commandList, nvrhi::CommandQueue::Compute);
	device->waitForIdle();

	auto& geometryTriangles = mesh->geometryDesc.geometryData.triangles;
	geometryTriangles.opacityMicromap = ommBuffers.opacityMicromap;
	geometryTriangles.ommIndexBuffer = ommBuffers.ommIndexBuffer;
	geometryTriangles.ommIndexBufferOffset = 0;
	geometryTriangles.ommIndexFormat = ommBuffers.ommIndexFormat;
	geometryTriangles.pOmmUsageCounts = ommBuffers.ommIndexHistogram.data();
	geometryTriangles.numOmmUsageCounts = uint32_t(ommBuffers.ommIndexHistogram.size());

	ommBuffers.ommDescArrayHistogramBuffer = nullptr;
	ommBuffers.ommIndexHistogramBuffer = nullptr;
	ommBuffers.ommDescArrayHistogramReadbackBuffer = nullptr;
	ommBuffers.ommIndexHistogramReadbackBuffer = nullptr;

	return true;
}
