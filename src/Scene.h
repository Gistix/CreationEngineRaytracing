#pragma once

#include "core/Mesh.h"
#include "core/Model.h"
#include "SceneGraph.h"
#include "Types/RendererParams.h"

#include "Renderer/RenderNode.h"

#include "interop/CameraData.hlsli"
#include "interop/SharedData.hlsli"

#include "Types/Settings.h"

struct Scene
{
	std::shared_mutex m_SceneMutex;

	eastl::unique_ptr<SceneGraph> m_SceneGraph;

	eastl::unique_ptr<RenderNode> m_GlobalIllumination;
	eastl::unique_ptr<RenderNode> m_PathTracing;
	eastl::unique_ptr<RenderNode> m_GBuffer;

	eastl::unique_ptr<CameraData> m_CameraData;
	nvrhi::BufferHandle m_CameraBuffer;

	eastl::unique_ptr<FeatureData> m_FeatureData;
	bool m_DirtyFeatureData = true;
	nvrhi::BufferHandle m_FeatureBuffer;

	ID3D12Resource* m_SkyHemisphereResource = nullptr;
	nvrhi::TextureHandle m_SkyHemisphereTexture;

	ID3D12Resource* m_FlowMapResource = nullptr;
	nvrhi::TextureHandle m_FlowMapTexture;

	int32_t* g_FlowMapSize = nullptr;
	RE::NiPointer<RE::NiSourceTexture>* g_FlowMapSourceTex = nullptr;
	float4* g_DisplacementCellTexCoordOffset = nullptr;
	RE::NiPoint2* g_DisplacementMeshPos = nullptr;
	RE::NiPoint2* g_DisplacementMeshFlowCellOffset = nullptr;

	Settings m_Settings;

	spdlog::level::level_enum logLevel = spdlog::level::info;

	Scene();

	void Load();

	void PostPostLoad();

	void DataLoaded();

	void SetLogLevel(spdlog::level::level_enum a_level = spdlog::level::info);
	spdlog::level::level_enum GetLogLevel();

	static Scene* GetSingleton()
	{
		static Scene singleton;
		return &singleton;
	}

	SceneGraph* GetSceneGraph() const;

	inline auto GetCameraData() const { return m_CameraData.get(); }

	inline auto GetCameraBuffer() const { return m_CameraBuffer; }

	inline auto GetFeatureBuffer() const { return m_FeatureBuffer; }

	inline bool IsPathTracingActive() const { return m_Settings.Enabled && m_Settings.GeneralSettings.Mode == Mode::PathTracing; };

	inline bool ApplyPathTracingCull() const { return m_Settings.Enabled && m_Settings.GeneralSettings.Mode == Mode::PathTracing && m_Settings.ExperimentalSettings.PathTracingCull; };

	inline nvrhi::ITexture* GetSkyHemiTexture() const { return m_SkyHemisphereTexture; }

	nvrhi::ITexture* GetFlowMapTexture();

	RenderNode* GetGlobalIllumination();

	RenderNode* GetPathTracing();

	RenderNode* GetModeNode(Mode mode);

	bool IsModeInitialized(Mode mode);

	void UpdateMode(Mode mode, Mode previousMode);

	void Initialize();

	void Execute();

	void ClearDirtyStates();

	void AttachModel(RE::TESForm* form);

	void AttachLand(RE::TESObjectLAND* land);

	void UpdateCameraData() const;

	void UpdateFeatureData(void* data, uint32_t size);

	void SetSkyHemisphere(ID3D12Resource* skyHemi);

	void UpdateSettings(Settings settings);
};