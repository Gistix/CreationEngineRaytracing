#include "GI12WorldSpaceReSTIRPass.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"
#include "ShaderUtils.h"
#include "Utils/Shader.h"

namespace Pass::Raytracing::GI12
{
	GI12WorldSpaceReSTIRPass::GI12WorldSpaceReSTIRPass(Renderer* renderer, SceneTLAS* sceneTLAS, GI12Resources* resources)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS), m_Resources(resources)
	{
		m_Defines = Util::Shader::GetGI12Defines(Scene::GetSingleton()->m_Settings);
	}

	void GI12WorldSpaceReSTIRPass::Initialize()
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void GI12WorldSpaceReSTIRPass::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_BindingSets.fill(nullptr);
		m_BindingSetDirty.fill(true);
	}

	void GI12WorldSpaceReSTIRPass::SettingsChanged(const Settings& settings)
	{
		auto defines = Util::Shader::GetGI12Defines(settings);
		if (defines != m_Defines) {
			m_Defines = defines;
			CreatePipeline();
			m_BindingSets.fill(nullptr);
			m_BindingSetDirty.fill(true);
		}
	}

	void GI12WorldSpaceReSTIRPass::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc desc;
		desc.visibility = nvrhi::ShaderType::Compute;
		desc.bindings = {
			// Samplers s0-s3
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			nvrhi::BindingLayoutItem::Sampler(2),
			nvrhi::BindingLayoutItem::Sampler(3),

			// CBVs
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // Camera
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1), // Raytracing
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2), // GI12CB
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(5), // WorldSpaceReSTIRCB

			// SRVs t0-t10
			nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
			nvrhi::BindingLayoutItem::Texture_SRV(1),  // Depth
			nvrhi::BindingLayoutItem::Texture_SRV(2),  // Albedo
			nvrhi::BindingLayoutItem::Texture_SRV(3),  // NormalRoughness
			nvrhi::BindingLayoutItem::Texture_SRV(4),  // VAOMAO
			nvrhi::BindingLayoutItem::Texture_SRV(5),  // MotionVectors
			nvrhi::BindingLayoutItem::Texture_SRV(6),  // FaceNormals
			nvrhi::BindingLayoutItem::Texture_SRV(7),  // SkyHemisphere
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(8),  // Lights
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(9),  // Instances
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10), // Meshes

			// UAVs u0-u3
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0), // ReservoirBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1), // MultibounceReservoirBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2), // CellBuffer
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3)  // CellPackedBuffer
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(desc);
	}

	void GI12WorldSpaceReSTIRPass::CreatePipeline()
	{
		if (!m_BindingLayout)
			CreateBindingLayout();

		auto device = GetRenderer()->GetDevice();
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		const wchar_t* shaderPath = L"data/shaders/raytracing/GI12/GI12WorldSpaceReSTIR.hlsl";

		auto createKernel = [&](const wchar_t* entryPoint, nvrhi::ShaderHandle& shader, nvrhi::ComputePipelineHandle& pipeline) {
			shader = nullptr;
			pipeline = nullptr;

			winrt::com_ptr<IDxcBlob> blob = ShaderCache::GetShader(shaderPath, defines, ShaderStage::Compute, entryPoint);
			if (!blob) {
				logger::error("GI12WorldSpaceReSTIRPass - Failed to compile kernel {}", Util::WStringToString(entryPoint));
				return;
			}

			shader = device->createShader({ nvrhi::ShaderType::Compute, "GI12WorldSpaceReSTIR", Util::WStringToString(entryPoint).c_str() }, blob->GetBufferPointer(), blob->GetBufferSize());
			if (!shader) {
				logger::error("GI12WorldSpaceReSTIRPass - Failed to create shader {}", Util::WStringToString(entryPoint));
				return;
			}

			pipeline = device->createComputePipeline(
				nvrhi::ComputePipelineDesc()
				.setComputeShader(shader)
				.addBindingLayout(m_BindingLayout));
		};

		createKernel(L"ClearReservoirs", m_ClearReservoirsShader, m_ClearReservoirsPipeline);
		createKernel(L"GenerateReservoirs", m_GenerateReservoirsShader, m_GenerateReservoirsPipeline);
		createKernel(L"GenerateMultibounceReservoirs", m_GenerateMultibounceReservoirsShader, m_GenerateMultibounceReservoirsPipeline);
		createKernel(L"CompactReservoirs", m_CompactReservoirsShader, m_CompactReservoirsPipeline);
		createKernel(L"ResampleReservoirs", m_ResampleReservoirsShader, m_ResampleReservoirsPipeline);
	}

	void GI12WorldSpaceReSTIRPass::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (m_BindingSets[currentSlot] && !m_BindingSetDirty[currentSlot])
			return;

		auto* renderer = GetRenderer();
		auto* scene = Scene::GetSingleton();
		auto* sceneGraph = scene->GetSceneGraph();
		auto* renderTargets = renderer->GetRenderTargets();
		auto& textureManager = renderer->RenderTargetManager();

		nvrhi::BindingSetDesc desc;
		desc.bindings = {
			nvrhi::BindingSetItem::Sampler(0, m_Resources->m_LinearWrapSampler),
			nvrhi::BindingSetItem::Sampler(1, m_Resources->m_LinearClampSampler),
			nvrhi::BindingSetItem::Sampler(2, m_Resources->m_PointWrapSampler),
			nvrhi::BindingSetItem::Sampler(3, m_Resources->m_PointClampSampler),

			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(2, m_Resources->m_GI12CB),
			nvrhi::BindingSetItem::ConstantBuffer(5, m_Resources->m_WorldSpaceReSTIRCB),

			nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_SceneTLAS->GetTopLevelAS().GetHandle()),
			nvrhi::BindingSetItem::Texture_SRV(1, renderer->GetDepthTexture()),
			nvrhi::BindingSetItem::Texture_SRV(2, renderTargets->albedo),
			nvrhi::BindingSetItem::Texture_SRV(3, renderTargets->normalRoughness),
			nvrhi::BindingSetItem::Texture_SRV(4, renderTargets->gnmao),
			nvrhi::BindingSetItem::Texture_SRV(5, renderer->GetMotionVectorTexture()),
			nvrhi::BindingSetItem::Texture_SRV(6, textureManager.GetTexture(RenderTarget::FaceNormals)),
			nvrhi::BindingSetItem::Texture_SRV(7, scene->GetSkyHemiTexture()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(8, sceneGraph->GetLightBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(9, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(10, sceneGraph->GetMeshBuffer()),

			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_Resources->m_ReservoirBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(1, m_Resources->m_MultibounceReservoirBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(2, m_Resources->m_VisibilityCountBuffer0),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(3, m_Resources->m_VisibilityCountBuffer1)
		};

		m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(desc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void GI12WorldSpaceReSTIRPass::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		auto bindingSet = m_BindingSets[currentSlot];
		if (!bindingSet)
			return;

		const uint32_t numEntries = ::GI12::Constants::kNumEntries;

		auto dispatch = [&](nvrhi::ComputePipelineHandle pipeline, uint32_t gx, uint32_t gy, uint32_t gz) {
			if (!pipeline) return;
			nvrhi::ComputeState state;
			state.pipeline = pipeline;
			state.bindings = { bindingSet };
			commandList->setComputeState(state);
			commandList->dispatch(gx, gy, gz);
		};

		// 1. Clear Reservoirs
		dispatch(m_ClearReservoirsPipeline, (numEntries + 127) / 128, 1, 1);

		// 2. Generate Reservoirs
		dispatch(m_GenerateReservoirsPipeline, (numEntries + 63) / 64, 1, 1);

		// 3. Generate Multi-bounce Reservoirs if enabled
		if (Scene::GetSingleton()->m_Settings.GI12.UseMultibounce) {
			dispatch(m_GenerateMultibounceReservoirsPipeline, (numEntries + 63) / 64, 1, 1);
		}

		// 4. Compact Reservoirs
		dispatch(m_CompactReservoirsPipeline, (numEntries + 63) / 64, 1, 1);

		// 5. Resample Reservoirs
		dispatch(m_ResampleReservoirsPipeline, (numEntries + 63) / 64, 1, 1);
	}
}
