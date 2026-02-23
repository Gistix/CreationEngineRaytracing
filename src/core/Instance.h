#pragma once

#include "core/Model.h"

#include "Light.hlsli"

#include "Util.h"

struct Instance
{
	enum State : uint8_t
	{
		Hidden = 1 << 0,
		Detached = 1 << 1
	};

	// Instance form id
	RE::FormID formID;

	// Node ptr
	RE::NiAVObject* node;

	// Model ptr
	Model* model;

	// Used for BLAS instance
	float3x4 transform;

	// Makes sure we only update once per frame
	uint64_t m_LastUpdate = 0;

	Instance(RE::FormID formID, RE::NiAVObject* node, Model* model) : formID(formID), node(node), model(model) { }
	
	nvrhi::rt::InstanceDesc GetInstanceDesc() const
	{
		nvrhi::rt::InstanceDesc instanceDesc;
		instanceDesc.bottomLevelAS = model->blas;
		assert(instanceDesc.bottomLevelAS);
		instanceDesc.instanceMask = 1;
		instanceDesc.instanceID = 0;
		memcpy(instanceDesc.transform, transform.m, sizeof(instanceDesc.transform));
		return instanceDesc;
	}

	bool SkipUpdate();

	void Update();

	eastl::vector<uint8_t> GatherLights(LightData* lightData, uint8_t numActiveLights) const
	{
		eastl::vector<uint8_t> instanceLights;

		float3 center = Util::Float3(node->worldBound.center);
		float radius = node->worldBound.radius;

		for (uint8_t i = 0; i < numActiveLights; i++) {
			auto& light = lightData[i];

			if (light.Type == LightType::Point && (center - light.Vector).Length() > radius + light.Radius)
				continue;

			instanceLights.push_back(i);
		}

		return instanceLights;
	}
};