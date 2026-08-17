#pragma once

#include "SceneGraph.h"

#include "Renderer/RenderNode.h"

#include "interop/CameraData.hlsli"
#include "interop/SharedData.hlsli"

#include "Types/MenuState.h"
#include "Types/Settings.h"

#include "INISettings.h"

struct Scene
{
	eastl::unique_ptr<SceneGraph> m_SceneGraph;

	eastl::unique_ptr<CameraData> m_CameraData;
	nvrhi::BufferHandle m_CameraBuffer;

	eastl::unique_ptr<FeatureData> m_FeatureData;
	bool m_DirtyFeatureData = true;
	nvrhi::BufferHandle m_FeatureBuffer;

	ID3D12Resource* m_SkyHemisphereResource = nullptr;
	nvrhi::TextureHandle m_SkyHemisphereTexture;
	ID3D12Resource* m_SkinDetailNormalResource = nullptr;
	nvrhi::TextureHandle m_SkinDetailNormalTexture;

	mutable nvrhi::TextureHandle m_ProjNoiseTexture;

	ID3D12Resource* m_WaterFlowMapResource = nullptr;
	nvrhi::TextureHandle m_WaterFlowMapTexture;

	int32_t* g_FlowMapSize = nullptr;
	float4* g_DisplacementCellTexCoordOffset = nullptr;
	RE::NiPoint2* g_DisplacementMeshPos = nullptr;
	RE::NiPoint2* g_DisplacementMeshFlowCellOffset = nullptr;

	// High Resolution time, updated every frame
	float* g_Time = nullptr;

	RE::NiPointer<RE::NiSourceTexture>* g_TreeLODAtlasTex = nullptr;
	RE::NiPointer<RE::NiSourceTexture>* g_TreeLODAtlasNormalTex = nullptr;

	// Used to draw full LOD when world map is open
	bool* g_BypassSubIndexVisibility = nullptr;

	CESEAdapter::REX::EnumSet<MenuState> m_MenuState;
	uint m_MenuStateUpdateFrame = 0;

	Settings m_Settings;

	bool m_IsDXVK = false;

	INISettings m_INISettings;

	spdlog::level::level_enum logLevel = spdlog::level::info;

	Scene();

	void Load();

	void PostPostLoad();

	void DataLoaded();

	void SetLogLevel(spdlog::level::level_enum a_level = spdlog::level::info);
	spdlog::level::level_enum GetLogLevel();

	bool IsDXVK() const { return m_IsDXVK; }

	static Scene* GetSingleton()
	{
		static Scene singleton;
		return &singleton;
	}

	SceneGraph* GetSceneGraph() const;

	inline auto GetCameraData() const { return m_CameraData.get(); }

	inline auto GetCameraBuffer() const { return m_CameraBuffer; }

	inline auto GetFeatureBuffer() const { return m_FeatureBuffer; }

	auto GetMenuState()
	{
		auto frameCount = Util::Adapter::GetGraphicsFrameCount();

		if (m_MenuStateUpdateFrame != frameCount) {
			m_MenuState = Util::Adapter::GetMenuState();

			m_MenuStateUpdateFrame = frameCount;
		}

		return m_MenuState;
	}

	bool CanCull()
	{
		return GetMenuState().none(MenuState::MainMenu, MenuState::LoadingMenu);
	}

	inline bool IsPathTracingActive() const { return m_Settings.Enabled && m_Settings.GeneralSettings.Mode == Mode::PathTracing; };

	inline bool ApplyPathTracingCull() 
	{ 
		return IsPathTracingActive() &&
			m_Settings.ExperimentalSettings.PathTracingCull != PTCullMode::Disabled && CanCull();
	}

	inline bool ApplyFullPathTracingCull()
	{
		return IsPathTracingActive() &&
			m_Settings.ExperimentalSettings.PathTracingCull == PTCullMode::Full && CanCull();
	}

	inline nvrhi::ITexture* GetSkyHemiTexture() const { return m_SkyHemisphereTexture; }
	nvrhi::ITexture* GetSkinDetailNormalTexture() const;

	nvrhi::ITexture* GetProjNoiseTexture() const;

	inline nvrhi::ITexture* GetFlowMapTexture() const { return m_WaterFlowMapTexture; }

	void UpdateMode(Mode mode);

	void Initialize();

	void Execute();

	void UpdateCameraData() const;

	void UpdateFeatureData(void* data, uint32_t size);

	void SetSkyHemisphere(ID3D12Resource* skyHemi);
	void SetSkinDetailNormal(ID3D12Resource* skinDetailNormal);

	void SetWaterFlowMap(ID3D12Resource* skyHemi);

	float GetResolutionScale() const;

	void UpdateSettings(Settings settings);
};