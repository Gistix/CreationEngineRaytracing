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
	eastl::unique_ptr<SceneGraph> m_SceneGraph;

	std::recursive_mutex shareTextureMutex;
	bool shareTexture = false;

	eastl::unique_ptr<RenderNode> m_GlobalIllumination;
	eastl::unique_ptr<RenderNode> m_PathTracing;

	eastl::unique_ptr<CameraData> m_CameraData;
	nvrhi::BufferHandle m_CameraBuffer;

	eastl::unique_ptr<FeatureData> m_FeatureData;
	bool m_DirtyFeatureData = true;
	nvrhi::BufferHandle m_FeatureBuffer;

	ID3D12Resource* m_SkyHemisphereResource = nullptr;
	nvrhi::TextureHandle m_SkyHemisphereTexture;

	Settings m_Settings;

	Scene();

	static Scene* GetSingleton()
	{
		static Scene singleton;
		return &singleton;
	}

	SceneGraph* GetSceneGraph() const;

	inline auto GetCameraData() const { return m_CameraData.get(); }

	inline auto GetCameraBuffer() const { return m_CameraBuffer; }

	inline auto GetFeatureBuffer() const { return m_FeatureBuffer; }

	inline bool ApplyPathTracingCull() const { return m_Settings.Enabled && m_Settings.PathTracing && m_Settings.DebugSettings.PathTracingCull; };

	inline nvrhi::ITexture* GetSkyHemiTexture() const { return m_SkyHemisphereTexture; }

	bool Initialize(RendererParams rendererParams);

	void Render();

	void Update(nvrhi::ICommandList* commandList);

	void ClearDirtyStates();

	void AttachModel(RE::TESForm* form);

	void AttachLand(RE::TESForm* form, RE::NiAVObject* root);

	void AddLight(RE::BSLight* light);

	void RemoveLight(const RE::NiPointer<RE::BSLight>& a_light);

	void UpdateCameraData() const;

	void UpdateFeatureData(void* data, uint32_t size);

	void SetSkyHemisphere(ID3D12Resource* skyHemi);

	void UpdateSettings(Settings settings);
};