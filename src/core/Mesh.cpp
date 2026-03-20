#include "Mesh.h"
#include "Util.h"
#include "byte4.hlsli"

#include "Scene.h"
#include "Renderer.h"
#include "SceneGraph.h"

#include "Types\CommunityShaders\BSLightingShaderMaterialPBR.h"
#include "Types\CommunityShaders\BSLightingShaderMaterialPBRLandscape.h"

#include "Utils/CalcTangents.h"

void Mesh::BuildMesh(RE::BSGraphics::TriShape* rendererData, const uint32_t& vertexCountIn, const uint32_t& triangleCountIn, const uint16_t& bonesPerVertex)
{
	auto vertexDesc = rendererData->vertexDesc;

	vertexFlags = vertexDesc.GetFlags();

	bool hasNormal = vertexFlags & RE::BSGraphics::Vertex::VF_NORMAL;
	bool hasTangent = vertexFlags & RE::BSGraphics::Vertex::VF_TANGENT;

	// Vertices
	{
		bool dynamic = false;
		bool skinned = flags.any(Flags::Skinned);

		if (flags.any(Flags::Dynamic)) {
			geometry.dynamicPosition.resize(vertexCountIn);

			static REL::Relocation<const RE::NiRTTI*> dynamicTriShapeRTTI{ RE::BSDynamicTriShape::Ni_RTTI };

			if (bsGeometryPtr->GetRTTI() == dynamicTriShapeRTTI.get()) {
				auto* pDynamicTriShape = reinterpret_cast<RE::BSDynamicTriShape*>(bsGeometryPtr);

				if (pDynamicTriShape) {
					auto& dynTriShapeRuntime = pDynamicTriShape->GetDynamicTrishapeRuntimeData();

					dynTriShapeRuntime.lock.Lock();
					std::memcpy(geometry.dynamicPosition.data(), dynTriShapeRuntime.dynamicData, dynTriShapeRuntime.dataSize);
					dynTriShapeRuntime.lock.Unlock();

					dynamic = true;
				}
			}

			// Clear Dynamic flag if geometry is not a valid BSDynamicTriShape.
			// Enforces the invariant that when Flags::Dynamic is set, geometry is always a BSDynamicTriShape.
			if (!dynamic)
				flags.reset(Flags::Dynamic);
		}

		geometry.vertices.resize(vertexCountIn);

		if (skinned)
			geometry.skinning.resize(vertexCountIn);

		auto vertexSize = Util::Geometry::GetSkyrimVertexSize(vertexFlags);
		auto vertexSize2 = Util::Geometry::GetStoredVertexSize(*reinterpret_cast<uint64_t*>(&vertexDesc));

		if (vertexSize != vertexSize2)
			logger::warn("[RT] Mesh::BuildMesh - Vertex size mismatch: {} != {}", vertexSize, vertexSize2);

		bool hasPosition = vertexFlags & RE::BSGraphics::Vertex::VF_VERTEX;

		uint32_t posOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_POSITION);
		uint32_t uvOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
		uint32_t normOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_NORMAL);
		uint32_t tangOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_BINORMAL);
		uint32_t colorOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_COLOR);
		uint32_t skinOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_SKINNING);
		uint32_t landOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_LANDDATA);

		uint32_t boneIDOffset = sizeof(uint16_t) * bonesPerVertex;

		eastl::vector<half> weights;
		eastl::vector<uint8_t> boneIds;

		if (skinned) {
			weights.resize(bonesPerVertex);
			boneIds.resize(bonesPerVertex);
		}

		float3 min(FLT_MAX), max(-FLT_MAX);

		for (uint32_t i = 0; i < vertexCountIn; i++) {
			uint8_t* vtx = rendererData->rawVertexData + i * vertexSize;

			Vertex vertexData{};

			float4 pos;

			if (hasPosition) {
				std::memcpy(&pos, vtx + posOffset, sizeof(float4));
			}
			else if (dynamic) {
				pos = geometry.dynamicPosition[i];
			}

			min = float3::Min(min, float3(pos));
			max = float3::Max(max, float3(pos));

			if (hasPosition || dynamic) {
				vertexData.Position = { pos.x, pos.y, pos.z };
			}

			if (vertexFlags & RE::BSGraphics::Vertex::VF_UV) {
				std::memcpy(&vertexData.Texcoord0, vtx + uvOffset, sizeof(half2));
			}

			if (hasNormal) {
				byte4f normalPacked;
				std::memcpy(&normalPacked, vtx + normOffset, sizeof(byte4f));
				auto normal = normalPacked.unpack();

				vertexData.Normal = Util::Math::Normalize({ normal.x, normal.y, normal.z });

				if (hasTangent) {
					byte4f tangentPacked;
					std::memcpy(&tangentPacked, vtx + tangOffset, sizeof(byte4f));
					auto tangent = tangentPacked.unpack();

					vertexData.Tangent = Util::Math::Normalize({ tangent.x, tangent.y, tangent.z });

					float3 bitangent = { pos.w, normal.w, tangent.w };

					if (!hasPosition) {
						bitangent.x = std::sqrt(std::max(0.0f, 1.0f - bitangent.y * bitangent.y - bitangent.z * bitangent.z));
					}

					vertexData.Handedness = (tangent.x * (vertexData.Tangent.y * vertexData.Normal.z - vertexData.Tangent.z * vertexData.Normal.y) +
						bitangent.y * (vertexData.Tangent.z * vertexData.Normal.x - vertexData.Tangent.x * vertexData.Normal.z) +
						bitangent.z * (vertexData.Tangent.x * vertexData.Normal.y - vertexData.Tangent.y * vertexData.Normal.x)) < 0 ?
						-1.0f : 1.0f;
				}
			}

			if (skinned) {
				if (vertexFlags & RE::BSGraphics::Vertex::VF_SKINNED) {
					std::memcpy(weights.data(), vtx + skinOffset, sizeof(half) * bonesPerVertex);
					std::memcpy(boneIds.data(), vtx + skinOffset + boneIDOffset, sizeof(uint8_t) * bonesPerVertex);

					float sum = 0.0f;
					for (float w : weights) {
						sum += w;
					}

					if (sum < 1.0f) {
						weights[0] += 1.0f - sum;
					}
					else if (sum > eastl::numeric_limits<float>::epsilon()) {
						float sumRcp = 1.0f / sum;

						for (half& w : weights) {
							w *= sumRcp;
						}
					}
					else {
						weights = { 1.0f };
					}
				}
				else {
					weights = { 1.0f };
					boneIds = { 0 };
				}

				auto fillSkinningData = []<typename T>(eastl::vector<T>&vector) {
					auto currSize = vector.size();

					if (currSize < 4) {
						vector.insert(vector.end(), 4 - currSize, 0);
					}
				};

				fillSkinningData(weights);
				fillSkinningData(boneIds);

				geometry.skinning[i] = Skinning(weights, boneIds);
			}

			if (vertexFlags & RE::BSGraphics::Vertex::VF_LANDDATA) {
				std::memcpy(&vertexData.LandBlend0, vtx + landOffset, sizeof(uint32_t));
				std::memcpy(&vertexData.LandBlend1, vtx + landOffset + sizeof(uint32_t), sizeof(uint32_t));
			}

			if (vertexFlags & RE::BSGraphics::Vertex::VF_COLORS) {
				std::memcpy(&vertexData.Color, vtx + colorOffset, sizeof(uint32_t));
			}
			else {
				vertexData.Color.pack({ 1.0f, 1.0f, 1.0f, 1.0f });
			}

			geometry.vertices[i] = vertexData;
		}

		vertexCount = vertexCountIn;
	}

	// Triangles
	{
		// Landscape contains no triangles, so we build them ourselves
		if (flags.any(Flags::Landscape)) {
			geometry.triangles.reserve(triangleCountIn);

			constexpr uint16_t GRID_SIZE = 16;
			constexpr uint16_t VERTICES = GRID_SIZE + 1;

			for (uint16_t y = 0; y < GRID_SIZE; y++) {
				for (uint16_t x = 0; x < GRID_SIZE; x++) {
					uint16_t v0 = y * VERTICES + x;
					uint16_t v1 = v0 + 1;
					uint16_t v2 = v0 + VERTICES;
					uint16_t v3 = v2 + 1;

					if (v0 >= vertexCount || v1 >= vertexCount || v2 >= vertexCount)
						logger::critical("[RT] Quad {} {} vertex overflow: [{}, {}, {}]", x, y, v0, v1, v2);

					// First triangle
					geometry.triangles.emplace_back(v0, v1, v2);

					// Second triangle
					geometry.triangles.emplace_back(v1, v3, v2);
				}
			}
		}
		else {
			geometry.triangles.resize(triangleCountIn);
			std::memcpy(geometry.triangles.data(), rendererData->rawIndexData, sizeof(Triangle) * triangleCountIn);
		}

		triangleCount = triangleCountIn;
	}

	if (!hasNormal)
		CalculateNormals();

	if (!hasTangent) {
		Util::CalcTangents::GetSingleton()->calc(this);

	}
}

Texture Mesh::GetTexture(const RE::NiPointer<RE::NiSourceTexture> niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, bool modelSpaceNormalMap = false)
{
	if (!niPointer || !niPointer->rendererTexture)
		return Texture(defaultDescHandle, nullptr);

	eastl::shared_ptr<DescriptorHandle> result = nullptr;

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	if (modelSpaceNormalMap)
		result = sceneGraph->GetMSNormalMapDescriptor(this, niPointer->rendererTexture);
	else
		result = sceneGraph->GetTextureDescriptor(niPointer->rendererTexture->texture);

	if (!result)
		return Texture(defaultDescHandle, nullptr);

	return Texture(result, defaultDescHandle.get());
}

void Mesh::BuildMaterial([[maybe_unused]] const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, [[maybe_unused]] RE::FormID formID)
{
	auto* renderer = Renderer::GetSingleton();

	auto& grayTexture = renderer->GetGrayTextureIndex();
	auto& normalTexture = renderer->GetNormalTextureIndex();
	auto& blackTexture = renderer->GetBlackTextureIndex();
	auto& rmaosTexture = renderer->GetRMAOSTextureIndex();
	auto& detailTexture = renderer->GetDetailTextureIndex();

	using State = RE::BSGeometry::States;
	using Feature = RE::BSShaderMaterial::Feature;
	using EShaderPropertyFlag = RE::BSShaderProperty::EShaderPropertyFlag;

	eastl::array<half4, 3> colors = {
		float4(1.0f, 1.0f, 1.0f, 1.0f),
		float4(0.0f, 0.0f, 0.0f, 0.0f),
		float4(1.0f, 1.0f, 1.0f, 1.0f)
	};

	eastl::array<half, 2> scalars;
	scalars.fill(0.0f);

	eastl::array<half4, 2> texCoordOffsetScales = {
		float4(0.0f, 0.0f, 1.0f, 1.0f),
		float4(0.0f, 0.0f, 1.0f, 1.0f)
	};

	Material::AlphaMode alphaMode = Material::AlphaMode::None;
	
	half alphaThreshold = 0.5f;

	eastl::array<Texture, 20> textures;
	textures.fill(Texture(blackTexture, nullptr));

	RE::BSShader::Type shaderType = RE::BSShader::Type::None;
	REX::EnumSet<EShaderPropertyFlag, std::uint64_t> shaderFlags;
	RE::BSShaderMaterial::Feature feature = RE::BSShaderMaterial::Feature::kNone;
	stl::enumeration<PBRShaderFlags, uint32_t> pbrFlags;

	bool missingPBREmissiveColor = false;

	{
		auto* property = geometryRuntimeData.properties[State::kProperty].get();

		auto* effect = geometryRuntimeData.properties[State::kEffect].get();

		if (effect) {
			if (RE::BSShaderProperty* shaderProp = netimmerse_cast<RE::BSShaderProperty*>(effect)) {
				shaderFlags = shaderProp->flags.get();
				colors[0].w *= shaderProp->alpha;
			}

			if (RE::BSLightingShaderProperty* lightingShaderProp = skyrim_cast<RE::BSLightingShaderProperty*>(effect)) {
				shaderType = RE::BSShader::Type::Lighting;

				// Set alpha flags
				if (property && property->GetType() == RE::NiProperty::Type::kAlpha) {
					static REL::Relocation<const RE::NiRTTI*> niAlphaPropertyRTTI{ RE::NiAlphaProperty::Ni_RTTI };
					auto alphaProperty = property->GetRTTI() == niAlphaPropertyRTTI.get() ? reinterpret_cast<RE::NiAlphaProperty*>(property) : nullptr;

					if (alphaProperty && alphaProperty->GetAlphaBlending()) {
						alphaMode = Material::AlphaMode::Blend;
					}

					// Alpha Test takes precedence
					if (alphaProperty && alphaProperty->GetAlphaTesting()) {
						alphaMode = Material::AlphaMode::Test;
						alphaThreshold = alphaProperty->alphaThreshold / 255.0f;
					}
				}

				colors[Constants::Material::EMISSIVE_COLOR] = {
					lightingShaderProp->emissiveColor->red,
					lightingShaderProp->emissiveColor->green,
					lightingShaderProp->emissiveColor->blue,
					lightingShaderProp->emissiveMult
				};

				if (auto shaderMaterial = lightingShaderProp->material) {
					feature = shaderMaterial->GetFeature();

					for (size_t i = 0; i < 2; i++) {
						texCoordOffsetScales[i] = {
							shaderMaterial->texCoordOffset[i].x, shaderMaterial->texCoordOffset[i].y,
							shaderMaterial->texCoordScale[i].x, shaderMaterial->texCoordScale[i].y
						};
					}

					// Landscape
					if (const auto* lightingBaseMaterialLand = skyrim_cast<RE::BSLightingShaderMaterialLandscape*>(shaderMaterial)) {
						textures[0] = GetTexture(lightingBaseMaterialLand->diffuseTexture, grayTexture);
						textures[Material::MAX_PBRLAND_TEXTURES] = GetTexture(lightingBaseMaterialLand->normalTexture, normalTexture);

						for (uint i = 0; i < std::min(lightingBaseMaterialLand->numLandscapeTextures, Material::MAX_LAND_TEXTURES); i++) {
							textures[i + 1] = GetTexture(lightingBaseMaterialLand->landscapeDiffuseTexture[i], grayTexture);
							textures[Material::MAX_PBRLAND_TEXTURES + i + 1] = GetTexture(lightingBaseMaterialLand->landscapeNormalTexture[i], normalTexture);
						}

						textures[Material::MAX_PBRLAND_TEXTURES * 3] = GetTexture(lightingBaseMaterialLand->terrainOverlayTexture, blackTexture);
						textures[Material::MAX_PBRLAND_TEXTURES * 3 + 1] = GetTexture(lightingBaseMaterialLand->terrainNoiseTexture, blackTexture);
					}
					else if (typeid(*shaderMaterial) == typeid(BSLightingShaderMaterialPBRLandscape)) {
						const auto* lightingPBRMaterialLand = static_cast<BSLightingShaderMaterialPBRLandscape*>(shaderMaterial);

						for (uint i = 0; i < std::min(lightingPBRMaterialLand->numLandscapeTextures, Material::MAX_PBRLAND_TEXTURES); i++) {
							textures[i] = GetTexture(lightingPBRMaterialLand->landscapeBaseColorTextures[i], grayTexture);
							textures[Material::MAX_PBRLAND_TEXTURES + i] = GetTexture(lightingPBRMaterialLand->landscapeNormalTextures[i], normalTexture);
							textures[Material::MAX_PBRLAND_TEXTURES * 2 + i] = GetTexture(lightingPBRMaterialLand->landscapeRMAOSTextures[i], rmaosTexture);
						}

						textures[Material::MAX_PBRLAND_TEXTURES * 3] = GetTexture(lightingPBRMaterialLand->terrainOverlayTexture, blackTexture);
						textures[Material::MAX_PBRLAND_TEXTURES * 3 + 1] = GetTexture(lightingPBRMaterialLand->terrainNoiseTexture, blackTexture);
					}
					else if (typeid(*shaderMaterial) == typeid(BSLightingShaderMaterialPBR)) {
						// TrueBR - Tried to check for 'lightingShaderProp->flags.any(EShaderPropertyFlag::kMenuScreen)'
						// but it did not work at all, skyrim_cast is not safe and will cast even if not PBR material (no RTTI?)

						const auto* lightingPBRMaterial = static_cast<BSLightingShaderMaterialPBR*>(shaderMaterial);

						textures[0] = GetTexture(lightingPBRMaterial->diffuseTexture, grayTexture);
						textures[Constants::Material::NORMALMAP_TEXTURE] = GetTexture(lightingPBRMaterial->normalTexture, normalTexture);
						textures[2] = GetTexture(lightingPBRMaterial->emissiveTexture, blackTexture);
						textures[3] = GetTexture(lightingPBRMaterial->rmaosTexture, rmaosTexture);

						// If emissive texture is set but kOwnEmit flag is missing the game has eaten up emissive color *here*
						// It will however be available during render updates where we will fetch it and set this back to false
						missingPBREmissiveColor = (textures[2].defaultTexture != nullptr && shaderFlags.none(EShaderPropertyFlag::kOwnEmit));

						scalars[0] = lightingPBRMaterial->GetRoughnessScale();
						scalars[1] = lightingPBRMaterial->GetSpecularLevel();

						pbrFlags = Util::Material::GetPBRShaderFlags(lightingPBRMaterial);

						if (pbrFlags & PBRShaderFlags::Subsurface) {
							textures[6] = GetTexture(lightingPBRMaterial->featuresTexture0, blackTexture);

							auto& sssColor = lightingPBRMaterial->GetSubsurfaceColor();
							colors[2] = { sssColor.red, sssColor.green, sssColor.blue, 1.0f };
							scalars[2] = lightingPBRMaterial->GetSubsurfaceOpacity();
						}

						// Enforce TruePBR flag
						shaderFlags.set(EShaderPropertyFlag::kMenuScreen);
					}
					else {
						// Roughness Scale
						scalars[0] = 1.0f;

						// Specular Level
						scalars[1] = 0.04f;

						// Vanilla Materials
						if (const RE::BSLightingShaderMaterialBase* lightingBaseMaterial = skyrim_cast<RE::BSLightingShaderMaterialBase*>(shaderMaterial)) {
							textures[0] = GetTexture(lightingBaseMaterial->diffuseTexture, grayTexture);

							bool isModelSpaceNormalMap = shaderFlags.any(EShaderPropertyFlag::kModelSpaceNormals);

							if (isModelSpaceNormalMap)
								textures[Constants::Material::NORMALMAP_TEXTURE] = GetTexture(nullptr, normalTexture, isModelSpaceNormalMap);
							else
								textures[Constants::Material::NORMALMAP_TEXTURE] = GetTexture(lightingBaseMaterial->normalTexture, normalTexture, isModelSpaceNormalMap);

							if (shaderFlags.any(EShaderPropertyFlag::kSpecular)) {
								if (shaderFlags.any(EShaderPropertyFlag::kModelSpaceNormals)) {
									textures[3] = GetTexture(lightingBaseMaterial->specularBackLightingTexture, blackTexture);
								}

								colors[2] = {
									lightingBaseMaterial->specularColor.red,
									lightingBaseMaterial->specularColor.green,
									lightingBaseMaterial->specularColor.blue,
									lightingBaseMaterial->specularColorScale
								};

								scalars[0] = Util::Material::ShininessToRoughness(lightingBaseMaterial->specularPower);
							}

							// Envmap
							if (feature == Feature::kEnvironmentMap || feature == Feature::kEye) {
								if (const auto* lightingEnvmapMaterial = skyrim_cast<RE::BSLightingShaderMaterialEnvmap*>(shaderMaterial)) {
									textures[4] = GetTexture(lightingEnvmapMaterial->envTexture, blackTexture);
									textures[5] = GetTexture(lightingEnvmapMaterial->envMaskTexture, blackTexture);
								}
							}

							// Glow
							if (feature == Feature::kGlowMap) {
								if (const auto* lightingGlowMaterial = skyrim_cast<RE::BSLightingShaderMaterialGlowmap*>(shaderMaterial)) {
									if (lightingShaderProp->flags.none(EShaderPropertyFlag::kOwnEmit)) {
										colors[Constants::Material::EMISSIVE_COLOR].x = 1.0f;
										colors[Constants::Material::EMISSIVE_COLOR].y = 1.0f;
										colors[Constants::Material::EMISSIVE_COLOR].z = 1.0f;
									}

									textures[2] = GetTexture(lightingGlowMaterial->glowTexture, blackTexture);
								}
							}

							// Hair
							if (feature == Feature::kHairTint) {
								if (const auto* lightingHairTintMaterial = skyrim_cast<RE::BSLightingShaderMaterialHairTint*>(shaderMaterial)) {
									colors[0].x = lightingHairTintMaterial->tintColor.red;
									colors[0].y = lightingHairTintMaterial->tintColor.green;
									colors[0].z = lightingHairTintMaterial->tintColor.blue;

									// Load flowmap texture for hair (stored in specularBackLightingTexture slot)
									textures[3] = GetTexture(lightingBaseMaterial->specularBackLightingTexture, blackTexture);
								}
							}

							// FaceGen
							if (feature == Feature::kFaceGen) {
								if (const auto* lightingFacegenMaterial = skyrim_cast<RE::BSLightingShaderMaterialFacegen*>(shaderMaterial)) {
									if (Util::IsPlayerFormID(formID)) {
										auto& gameRendererRuntimeData = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData();

										auto faceTintDescriptor = Scene::GetSingleton()->GetSceneGraph()->GetTextureDescriptor(
											gameRendererRuntimeData.renderTargets[RE::RENDER_TARGETS::kPLAYER_FACEGEN_TINT].texture
										);

										textures[4] = faceTintDescriptor ? Texture(faceTintDescriptor, grayTexture.get()) : Texture(grayTexture, nullptr);
									}
									else
										textures[4] = GetTexture(lightingFacegenMaterial->tintTexture, grayTexture);

									textures[5] = GetTexture(lightingFacegenMaterial->detailTexture, detailTexture);
								}
							}

							// FaceGen RGB Tint
							if (feature == Feature::kFaceGenRGBTint) {
								if (const auto* lightingFacegenTintMaterial = skyrim_cast<RE::BSLightingShaderMaterialFacegenTint*>(shaderMaterial)) {
									colors[0].x = lightingFacegenTintMaterial->tintColor.red;
									colors[0].y = lightingFacegenTintMaterial->tintColor.green;
									colors[0].z = lightingFacegenTintMaterial->tintColor.blue;
								}
							}
						}
					}
				}
				else {
					logger::warn("[RT] BuildMaterial - BSShaderMaterial is nullptr");
				}
			}

			if (auto effectShaderProp = netimmerse_cast<RE::BSEffectShaderProperty*>(effect)) {
				shaderType = RE::BSShader::Type::Effect;

				if (auto effectMaterial = skyrim_cast<RE::BSEffectShaderMaterial*>(effectShaderProp->material)) {
					colors[Constants::Material::EMISSIVE_COLOR] = {
						effectMaterial->baseColor.red,
						effectMaterial->baseColor.green,
						effectMaterial->baseColor.blue,
						effectMaterial->baseColorScale
					};

					textures[0] = GetTexture(effectMaterial->sourceTexture, blackTexture);
					textures[2] = GetTexture(effectMaterial->greyscaleTexture, blackTexture);
				}
			}
		}
	}

	// Fallback to alpha test if possible
	bool blendMaterial = feature == Feature::kHairTint || feature == Feature::kFaceGen || feature == Feature::kFaceGenRGBTint || feature == Feature::kEye || shaderFlags & EShaderPropertyFlag::kTwoSided;
	if (alphaMode == Material::AlphaMode::Blend && !blendMaterial)
		alphaMode = Material::AlphaMode::Transmission;

	geometryDesc.flags = alphaMode == Material::AlphaMode::None ? nvrhi::rt::GeometryFlags::Opaque : nvrhi::rt::GeometryFlags::None;

	material = Material(
		shaderFlags,
		shaderType,
		feature,
		pbrFlags,
		alphaMode,
		alphaThreshold,
		colors,
		scalars,
		texCoordOffsetScales,
		textures,
		missingPBREmissiveColor);
}

void Mesh::CreateBuffers(SceneGraph* sceneGraph, nvrhi::ICommandList* commandList)
{
	auto device = Renderer::GetSingleton()->GetDevice();

	bool updatable = flags.any(Flags::Dynamic, Flags::Skinned);

	logger::debug("Mesh::CreateBuffers - {}", m_Name);

	// Triangle Buffer
	{
		const size_t size = sizeof(Triangle) * triangleCount;

		logger::debug("Mesh::CreateBuffers - Triangle Count: {}, Buffer Size: {}", triangleCount, size);

		auto& triangleBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(Triangle))
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setIsAccelStructBuildInput(true)
			.setDebugName(std::format("{} (Triangle Buffer)", m_Name.c_str()));

		buffers.triangleBuffer = device->createBuffer(triangleBufferDesc);

		commandList->writeBuffer(buffers.triangleBuffer, geometry.triangles.data(), size);

		{
			// Create SRV binding for triangles
			auto triangleBindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(0, buffers.triangleBuffer);
			// Register descriptor, get handle within heap and writes the SRV
			m_DescriptorHandle = sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->CreateDescriptorHandle(triangleBindingSet);
		}
	}

	auto descriptorIndex = m_DescriptorHandle.Get();

	if (flags.all(Flags::Dynamic)) {
		const size_t size = sizeof(float4) * vertexCount;

		logger::debug("Mesh::CreateBuffers - Dynamic Buffer Size: {}", size);

		auto& dynamicBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(float4))
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setDebugName(std::format("{} (Dynamic Position Buffer)", m_Name.c_str()));

		buffers.dynamicPositionBuffer = device->createBuffer(dynamicBufferDesc);

		UpdateUploadDynamicBuffers(commandList);

		{
			auto bindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffers.dynamicPositionBuffer);
			device->writeDescriptorTable(sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable, bindingSet);
		}

	}

	// Vertex Buffer
	{
		const size_t size = sizeof(Vertex) * vertexCount;

		logger::debug("Mesh::CreateBuffers - Vertex Count: {}, Buffer Size: {}", vertexCount, size);

		auto& vertexBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(Vertex))
			.setCanHaveUAVs(updatable)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setIsAccelStructBuildInput(true)
			.setDebugName(std::format("{} (Vertex Buffer)", m_Name.c_str()));

		buffers.vertexBuffer = device->createBuffer(vertexBufferDesc);

		commandList->writeBuffer(buffers.vertexBuffer, geometry.vertices.data(), size);

		auto vertexBindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffers.vertexBuffer);
		device->writeDescriptorTable(sceneGraph->GetVertexDescriptors()->m_DescriptorTable, vertexBindingSet);

		if (updatable) {
			auto uavBindingSet = nvrhi::BindingSetItem::StructuredBuffer_UAV(descriptorIndex, buffers.vertexBuffer);
			device->writeDescriptorTable(sceneGraph->GetVertexWriteDescriptors()->m_DescriptorTable, uavBindingSet);
		}
	}

	// Vertex Copy
	if (updatable) {
		const size_t size = sizeof(Vertex) * vertexCount;

		auto& vertexCopyBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(Vertex))
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setDebugName(std::format("{} (Vertex Copy Buffer)", m_Name.c_str()));

		buffers.vertexCopyBuffer = device->createBuffer(vertexCopyBufferDesc);

		commandList->writeBuffer(buffers.vertexCopyBuffer, geometry.vertices.data(), size);

		auto bindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffers.vertexCopyBuffer);
		device->writeDescriptorTable(sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable, bindingSet);
	}

	if (flags.all(Flags::Skinned)) {
		const size_t size = sizeof(Skinning) * vertexCount;

		auto& skinningBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(Skinning))
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setDebugName(std::format("{} (Skinning Buffer)", m_Name.c_str()));

		buffers.skinningBuffer = device->createBuffer(skinningBufferDesc);

		commandList->writeBuffer(buffers.skinningBuffer, geometry.skinning.data(), size);

		auto bindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffers.skinningBuffer);
		device->writeDescriptorTable(sceneGraph->GetSkinningDescriptors()->m_DescriptorTable, bindingSet);
	}

	// Updatable geometry is already in root space
	if (updatable)
		localToRoot = float3x4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f);

	// Geometry description
	auto& geometryTriangles = geometryDesc.geometryData.triangles;

	geometryTriangles.indexBuffer = buffers.triangleBuffer;
	geometryTriangles.indexOffset = 0;
	geometryTriangles.indexFormat = nvrhi::Format::R16_UINT;
	geometryTriangles.indexCount = triangleCount * 3;

	geometryTriangles.vertexBuffer = buffers.vertexBuffer;
	geometryTriangles.vertexOffset = 0;
	geometryTriangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
	geometryTriangles.vertexStride = sizeof(Vertex);
	geometryTriangles.vertexCount = vertexCount;

	if (!updatable)
		geometryDesc.setTransform(localToRoot.f);
}

bool Mesh::UpdateDynamicPosition()
{
	auto* dynamicTriShape = reinterpret_cast<RE::BSDynamicTriShape*>(bsGeometryPtr);
	auto& runtimeData = dynamicTriShape->GetDynamicTrishapeRuntimeData();

	if (!runtimeData.dynamicData)
		return false;

	auto& dataSize = runtimeData.dataSize;

	// Is this even a possibility?
	if (dataSize == 0)
		return false;

	runtimeData.lock.Lock();

	// Has dynamic position changed?
	if (std::memcmp(geometry.dynamicPosition.data(), runtimeData.dynamicData, dataSize) == 0) {
		runtimeData.lock.Unlock();
		return false;
	}

	std::memcpy(geometry.dynamicPosition.data(), runtimeData.dynamicData, dataSize);
	runtimeData.lock.Unlock();

	return true;
}

void Mesh::UpdateUploadDynamicBuffers(nvrhi::ICommandList* commandList)
{
	if (flags.none(Flags::Dynamic))
		return;

	commandList->writeBuffer(buffers.dynamicPositionBuffer, geometry.dynamicPosition.data(), sizeof(float4) * vertexCount);
}

bool Mesh::UpdateSkinning()
{
	// Update Bone matrices
	auto& skinInstance = bsGeometryPtr->GetGeometryRuntimeData().skinInstance;

	// RaceMenu crash fix
	if (!skinInstance || !skinInstance.get())
		return false;

	const auto frameID = skinInstance->frameID;

	if (frameID == Constants::INVALID_FRAME_ID)
		return false;

	//if (bsGeometryPtr->GetFlags().any(RE::NiAVObject::Flag::kNoAnimSyncZ, RE::NiAVObject::Flag::kNoAnimSyncS))
	//	return false;
	//logger::info("Mesh::UpdateSkinning - Flags: {}, {}", Util::GetFlagsString<RE::NiAVObject::Flag>(bsGeometryPtr->GetFlags().underlying()), m_Name);

	// Only update if the game has updated the matrices
	if (m_FrameID == frameID)
		return false;

	// UBE crash fix
	if (skinInstance->numMatrices == 0 || !skinInstance->boneMatrices)
		return false;

	if (m_BoneMatrices.empty() || skinInstance->numMatrices != m_BoneMatrices.size())
		m_BoneMatrices.resize(skinInstance->numMatrices);

	//float3x4* boneMatricesArray = reinterpret_cast<float3x4*>(skinInstance->boneMatrices);

	auto* rootParent = skinInstance->rootParent;

	// UBE crash fix
	if (!rootParent)
		return false;

	m_FrameID = frameID;

	auto skinRoot = rootParent->world;

	static auto identity = RE::NiTransform();

	// If skin transform is identity it will break skinning
	if (skinRoot == identity)
		return false;

	auto skinRootInverse = Util::Math::GetXMFromNiTransform(skinRoot.Invert());

	auto* skinData = skinInstance->skinData.get();

	if (skinInstance->numMatrices != skinData->bones) {
		logger::info("Mesh::UpdateSkinning - Num Matrices: {}, Num Bones: {}", skinInstance->numMatrices, skinData->bones);
	}

	for (uint i = 0; i < skinData->bones; i++) {
		auto boneData = skinData->boneData[i];
		auto boneWorld = *skinInstance->boneWorldTransforms[i];

		auto boneMatrix = boneWorld * boneData.skinToBone; //  skinData->rootParentToSkin * boneWorld * boneData.skinToBone;
		XMStoreFloat3x4(&m_BoneMatrices[i], XMMatrixMultiply(Util::Math::GetXMFromNiTransform(boneMatrix), skinRootInverse));
	}

	return true;
}

DirtyFlags Mesh::Update()
{
	const auto dynamic = flags.all(Mesh::Flags::Dynamic);
	const auto skinned = flags.all(Mesh::Flags::Skinned);

	// I don't know if kHidden is set on inner nodes for culling, so to be safe we check
	if (dynamic || skinned)
		SetPendingState(State::Hidden, bsGeometryPtr->GetFlags().any(RE::NiAVObject::Flag::kHidden));

	// Visibility flag is handled by model not by mesh
	if (IsPendingHidden())
		return DirtyFlags::None;

	auto updateFlags = DirtyFlags::None;

	if (material.missingPBREmissiveColor) {
		auto* effect = bsGeometryPtr->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get();

		if (RE::BSLightingShaderProperty* lightingShaderProp = skyrim_cast<RE::BSLightingShaderProperty*>(effect)) {
			if (lightingShaderProp->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit)) {
				material.Colors[Constants::Material::EMISSIVE_COLOR].x = lightingShaderProp->emissiveColor->red;
				material.Colors[Constants::Material::EMISSIVE_COLOR].y = lightingShaderProp->emissiveColor->green;
				material.Colors[Constants::Material::EMISSIVE_COLOR].z = lightingShaderProp->emissiveColor->blue;

				material.missingPBREmissiveColor = false;
				updateFlags |= DirtyFlags::Material;
			}
		}
	}

	if (dynamic && UpdateDynamicPosition())
		updateFlags |= DirtyFlags::Vertex;

	if (skinned && UpdateSkinning())
		updateFlags |= DirtyFlags::Skin;

	return updateFlags;
}

MeshData Mesh::GetData(float3 externalEmittance) const
{
	return MeshData(
		material.GetData(externalEmittance),
		static_cast<uint32_t>(m_DescriptorHandle.Get()),
		{0, 0},
		localToRoot
	);
}

bool Mesh::IsHidden() const
{
	return ((state & State::Hidden) != State::None) || ((state & State::DismemberHidden) != State::None);
}

bool Mesh::IsPendingHidden() const
{
	return ((pendingState & State::Hidden) != State::None) || ((pendingState & State::DismemberHidden) != State::None);
}

bool Mesh::IsDirtyState() const
{
	return pendingState != state;
}

// State is set as pending first, final state is updated after BLAS rebuild call
void Mesh::SetPendingState(State stateIn, bool activate)
{
	if (activate)
		pendingState |= stateIn;
	else
		pendingState &= ~stateIn;
}

void Mesh::UpdateDismember(bool enable)
{
	SetPendingState(State::DismemberHidden, !enable);
}

// Updates state from pending
void Mesh::UpdateState()
{
	state = pendingState;
}

void Mesh::CalculateNormals()
{
	eastl::vector<float3> normals;
	normals.resize(vertexCount, float3(0, 0, 0));

	// Loop over triangles
	for (auto& t : geometry.triangles) {
		Vertex& v0 = geometry.vertices[t.x];
		Vertex& v1 = geometry.vertices[t.y];
		Vertex& v2 = geometry.vertices[t.z];

		float3 pos0 = v0.Position;
		float3 pos1 = v1.Position;
		float3 pos2 = v2.Position;

		float3 deltaPos1 = pos1 - pos0;
		float3 deltaPos2 = pos2 - pos0;

		float3 faceNormal = deltaPos1.Cross(deltaPos2);

		normals[t.x] += faceNormal;
		normals[t.y] += faceNormal;
		normals[t.z] += faceNormal;
	}

	// Normalize and orthogonalize
	for (size_t i = 0; i < vertexCount; i++) {
		geometry.vertices[i].Normal = Util::Math::Normalize(normals[i]);
	}
}
