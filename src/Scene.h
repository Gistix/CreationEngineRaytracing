#pragma once

#include "core/Mesh.h"
#include "core/Model.h"
#include "SceneGraph.h"
#include "Types/RendererParams.h"

#include "Renderer/RenderNode.h"

#include "interop/CameraData.hlsli"
#include "interop/SharedData.hlsli"

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

	struct LightSettings
	{
		float Directional = 1.0f;
		float Point = 1.0f;
		bool LodDimmer = false;
	};

	struct LightingSettings
	{
		float Emissive = 1.0f;
		float Effect = 1.0f;
		float Sky = 1.0f;
	};

	struct RaytracingSettings
	{
		int Bounces = 2;
		int SamplesPerPixel = 1;
		bool RussianRoulette = true;
		float TexLODBias = -1.0f;
	};

	struct MaterialSettings
	{
		float2 Roughness = { 0.0f, 1.0f };
		float2 Metalness = { 0.0f, 1.0f };
	};

	struct Settings
	{
		bool Enabled = true;
		bool PathTracing = true;
		LightSettings LightSettings;
		LightingSettings LightingSettings;
		RaytracingSettings RaytracingSettings;
		MaterialSettings MaterialSettings;
	} settings;

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

	bool Initialize(RendererParams rendererParams);

	void Update(nvrhi::ICommandList* commandList);

	void AttachModel(RE::TESForm* form);

	void AttachLand(RE::TESForm* form, RE::NiAVObject* root);

	void AddLight(RE::BSLight* light);

	void RemoveLight(const RE::NiPointer<RE::BSLight>& a_light);

	void UpdateCameraData(float4x4 viewInverse, float4x4 projInverse, float4 cameraData, float4 NDCToView, float3 position) const;

	void UpdateFeatureData(void* data, uint32_t size);
};