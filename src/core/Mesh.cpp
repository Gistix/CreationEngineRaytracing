#include "Mesh.h"
#include "Util.h"
#include "byte4.hlsli"

#include "Scene.h"
#include "Renderer.h"
#include "SceneGraph.h"

#include "Types/CommunityShaders/BSLightingShaderMaterialPBR.h"
#include "Types/CommunityShaders/BSLightingShaderMaterialPBRLandscape.h"

#include "Utils/CalcTangents.h"
#include "Core/D3D12Texture.h"

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
			vertexData.dynamicPosition.resize(vertexCountIn);

			static REL::Relocation<const RE::NiRTTI*> dynamicTriShapeRTTI{ NiRTTI(BSDynamicTriShape) };

			if (bsGeometryPtr->GetRTTI() == dynamicTriShapeRTTI.get()) {
				auto* pDynamicTriShape = reinterpret_cast<RE::BSDynamicTriShape*>(bsGeometryPtr.get());

				if (pDynamicTriShape) {
					auto& dynTriShapeRuntime = pDynamicTriShape->GetDynamicTrishapeRuntimeData();

					dynTriShapeRuntime.lock.Lock();
					std::memcpy(vertexData.dynamicPosition.data(), dynTriShapeRuntime.dynamicData, dynTriShapeRuntime.dataSize);
					dynTriShapeRuntime.lock.Unlock();

					dynamic = true;
				}
			}

			// Clear Dynamic flag if geometry is not a valid BSDynamicTriShape.
			// Enforces the invariant that when Flags::Dynamic is set, geometry is always a BSDynamicTriShape.
			if (!dynamic)
				flags.reset(Flags::Dynamic);
		}

		vertexData.vertices.resize(vertexCountIn);

		if (skinned)
			vertexData.skinning.resize(vertexCountIn);

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
			uint8_t* vtx = Util::Adapter::CLib::GetVertexData(rendererData) + i * vertexSize;

			Vertex vertex{};

			float4 pos;

			if (hasPosition) {
				std::memcpy(&pos, vtx + posOffset, sizeof(float4));
			}
			else if (dynamic) {
				pos = vertexData.dynamicPosition[i];
			}

			min = float3::Min(min, float3(pos));
			max = float3::Max(max, float3(pos));

			if (hasPosition || dynamic) {
				vertex.Position = { pos.x, pos.y, pos.z };
			}

			if (vertexFlags & RE::BSGraphics::Vertex::VF_UV) {
				std::memcpy(&vertex.Texcoord0, vtx + uvOffset, sizeof(half2));
			}

			if (hasNormal) {
				byte4f normalPacked;
				std::memcpy(&normalPacked, vtx + normOffset, sizeof(byte4f));
				auto normal = normalPacked.unpack();

				float3 N = Util::Math::Normalize({ normal.x, normal.y, normal.z });
				vertex.Normal = N;

				if (hasTangent) {
					byte4f bitangentPacked;
					std::memcpy(&bitangentPacked, vtx + tangOffset, sizeof(byte4f));
					auto bitangent = bitangentPacked.unpack();

					float3 B = { bitangent.x, bitangent.y, bitangent.z };
					B = Util::Math::Normalize(B - N * N.Dot(B));

					float3 T = { pos.w, normal.w, bitangent.w };

					// Dynamic TriShapes (Blendshape/Morphtarget) do not have vertex position
					if (!hasPosition) {
						float sign = B.Cross(N).x < 0 ? -1.0f : 1.0f;
						T.x = std::sqrt(std::max(0.0f, 1.0f - bitangent.y * bitangent.y - bitangent.z * bitangent.z)) * sign;
					}

					T = Util::Math::Normalize(T - N * N.Dot(T));

					vertex.Tangent = Util::Math::Normalize(T);

					vertex.Handedness = -(N.Cross(T).Dot(B) < 0 ? -1.0f : 1.0f);
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

				vertexData.skinning[i] = Skinning(weights, boneIds);
			}

			if (vertexFlags & RE::BSGraphics::Vertex::VF_LANDDATA) {
				std::memcpy(&vertex.LandBlend0, vtx + landOffset, sizeof(uint32_t));
				std::memcpy(&vertex.LandBlend1, vtx + landOffset + sizeof(uint32_t), sizeof(uint32_t));
			}

			if (vertexFlags & RE::BSGraphics::Vertex::VF_COLORS) {
				std::memcpy(&vertex.Color, vtx + colorOffset, sizeof(uint32_t));
			}
			else {
				vertex.Color.pack({ 1.0f, 1.0f, 1.0f, 1.0f });
			}

			vertexData.vertices[i] = vertex;
		}

		vertexData.count = vertexCountIn;
	}

	// Triangles
	{
		// Landscape contains no triangles, so we build them ourselves
		if (flags.any(Flags::Landscape)) {
			triangleData.triangles = GetLandscapeTriangles();
		}
		else {
			triangleData.triangles.resize(triangleCountIn);
			std::memcpy(triangleData.triangles.data(), Util::Adapter::CLib::GetIndexData(rendererData), sizeof(Triangle) * triangleCountIn);

			// Validate triangle indices are within vertex bounds
			if (vertexCountIn > 0) {
				const uint16_t maxIndex = static_cast<uint16_t>(std::min(vertexCountIn, 65536u) - 1);
				for (uint32_t i = 0; i < triangleCountIn; i++) {
					auto& tri = triangleData.triangles[i];
					if (tri.x > maxIndex || tri.y > maxIndex || tri.z > maxIndex) {
						logger::warn("[RT] Mesh::BuildMesh - Triangle {} has out-of-bounds index ({}, {}, {}) for vertexCount {}", i, tri.x, tri.y, tri.z, vertexCountIn);
						tri.x = std::min(tri.x, maxIndex);
						tri.y = std::min(tri.y, maxIndex);
						tri.z = std::min(tri.z, maxIndex);
					}
				}
			}

			if (HasDoubleSidedGeom())
				flags.set(Mesh::Flags::DoubleSidedGeom);
		}

		triangleData.count = triangleCountIn;
	}

	if (!hasNormal)
		CalculateNormals();

	if (!hasTangent)
		Util::CalcTangents::GetSingleton()->calc(this);
}

eastl::vector<Triangle> Mesh::GetLandscapeTriangles()
{
	static const eastl::vector<Triangle> triangles = [] {
		eastl::vector<Triangle> t;

		constexpr uint16_t GRID_SIZE = 16;
		constexpr uint16_t VERTICES = GRID_SIZE + 1;

		t.reserve(GRID_SIZE * GRID_SIZE * 2);

		for (uint16_t y = 0; y < GRID_SIZE; y++) {
			for (uint16_t x = 0; x < GRID_SIZE; x++) {
				uint16_t v0 = y * VERTICES + x;
				uint16_t v1 = v0 + 1;
				uint16_t v2 = v0 + VERTICES;
				uint16_t v3 = v2 + 1;

				t.emplace_back(v0, v1, v2);
				t.emplace_back(v1, v3, v2);
			}
		}

		return t;
	}();

	return triangles;
}

Texture Mesh::GetTexture(const RE::NiPointer<RE::NiSourceTexture> niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle, [[maybe_unused]] bool modelSpaceNormalMap = false)
{
	if (!niPointer || !niPointer->rendererTexture)
		return Texture(defaultDescHandle, nullptr);

	eastl::shared_ptr<DescriptorHandle> result = nullptr;

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	auto* texture = niPointer->rendererTexture;

	if (modelSpaceNormalMap)
		result = sceneGraph->GetMSNormalMapDescriptor(this, texture);
	else {
		ID3D12Resource* d3d12Resource = nullptr;

		if (texture->pad24 == 1)
			d3d12Resource = reinterpret_cast<RE::BSGraphics::D3D12Texture*>(texture)->d3d12Texture;

		result = sceneGraph->GetTextureDescriptor(texture->texture, d3d12Resource);
	}

	if (!result)
		return Texture(defaultDescHandle, nullptr);

	return Texture(result, defaultDescHandle.get());

	//return Texture(defaultDescHandle, nullptr);
}

Texture Mesh::GetCubemapTexture(const RE::NiPointer<RE::NiSourceTexture> niPointer, eastl::shared_ptr<DescriptorHandle> defaultDescHandle)
{
	if (!niPointer || !niPointer->rendererTexture)
		return Texture(defaultDescHandle, nullptr);

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
	auto result = sceneGraph->GetCubemapDescriptor(niPointer->rendererTexture->texture);

	if (!result)
		return Texture(defaultDescHandle, nullptr);

	return Texture(result, defaultDescHandle.get());
}

void Mesh::BuildMaterial(const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, RE::TESForm* form)
{
	auto* renderer = Renderer::GetSingleton();

	auto& grayTexture = renderer->GetGrayTextureIndex();
	auto& normalTexture = renderer->GetNormalTextureIndex();
	auto& blackTexture = renderer->GetBlackTextureIndex();
	auto& whiteTexture = renderer->GetWhiteTextureIndex();
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

	eastl::array<half, 3> scalars;
	scalars.fill(0.0f);

	eastl::array<half4, 4> vectors = {
		float4(1.0f, 1.0f, 1.0f, 1.0f),
		float4(1.0f, 1.0f, 1.0f, 1.0f),
		float4(1.0f, 1.0f, 1.0f, 1.0f),
		float4(1.0f, 1.0f, 1.0f, 1.0f)
	};

	eastl::array<half4, 2> texCoordOffsetScales = {
		float4(0.0f, 0.0f, 1.0f, 1.0f),
		float4(0.0f, 0.0f, 1.0f, 1.0f)
	};

	Material::AlphaFlags alphaFlags = Material::AlphaFlags::None;
	
	half alphaThreshold = 0.5f;

	eastl::array<Texture, 20> textures;
	textures.fill(Texture(blackTexture, nullptr));

	RE::BSShader::Type shaderType = RE::BSShader::Type::None;
	REX::EnumSet<EShaderPropertyFlag, std::uint64_t> shaderFlags;
	REX::EnumSet<Material::WaterShaderFlags, std::uint32_t> waterShaderFlags;
	RE::BSShaderMaterial::Feature feature = RE::BSShaderMaterial::Feature::kNone;
	stl::enumeration<PBRShaderFlags, uint32_t> pbrFlags;

	{
		auto* property = geometryRuntimeData.properties[State::kProperty].get();

		auto* effect = geometryRuntimeData.properties[State::kEffect].get();

		if (effect) {
			if (RE::BSShaderProperty* shaderProp = netimmerse_cast<RE::BSShaderProperty*>(effect)) {
				shaderFlags = shaderProp->flags.get();
				colors[0].w = shaderProp->alpha;
			}

			if (RE::BSLightingShaderProperty* lightingShaderProp = skyrim_cast<RE::BSLightingShaderProperty*>(effect)) {
				shaderType = RE::BSShader::Type::Lighting;

				// Set alpha flags
				if (property && property->GetType() == RE::NiProperty::Type::kAlpha) {
					static REL::Relocation<const RE::NiRTTI*> niAlphaPropertyRTTI{ RE::NiAlphaProperty::Ni_RTTI };
					auto alphaProperty = property->GetRTTI() == niAlphaPropertyRTTI.get() ? reinterpret_cast<RE::NiAlphaProperty*>(property) : nullptr;

					if (alphaProperty && alphaProperty->GetAlphaBlending()) {
						alphaFlags |= Material::AlphaFlags::Blend;
					}

					if (alphaProperty && alphaProperty->GetAlphaTesting()) {
						alphaFlags |= Material::AlphaFlags::Test;
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

					// BSLightingShaderProperty with materialAlpha != 1 treated as alpha blending
					if (const auto* lightingBaseMaterial = skyrim_cast<RE::BSLightingShaderMaterialBase*>(shaderMaterial)) {
						if (lightingBaseMaterial->materialAlpha != 1.0f && (!property || property->GetType() != RE::NiProperty::Type::kAlpha)) {
							colors[0].w = lightingBaseMaterial->materialAlpha;
							alphaFlags |= Material::AlphaFlags::Blend;
						}
					}

					// Some eye meshes use EnvironmentMap shader instead of Eye shader;
					// detect them by geometry name and override the feature
					if (feature == Feature::kEnvironmentMap) {
						eastl::string nameLower(m_Name);
						for (auto& c : nameLower)
							c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
						if (nameLower.find("eye") != eastl::string::npos) {
							feature = Feature::kEye;
							logger::debug("[RT] BuildMaterial - Overriding EnvironmentMap to Eye for mesh: {}", m_Name.c_str());
						}
					}

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

						scalars[0] = lightingPBRMaterial->GetRoughnessScale();
						scalars[1] = lightingPBRMaterial->GetSpecularLevel();

						pbrFlags = Util::Material::Skyrim::GetPBRShaderFlags(lightingPBRMaterial);

						if (pbrFlags & PBRShaderFlags::Subsurface) {
							textures[6] = GetTexture(lightingPBRMaterial->featuresTexture0, blackTexture);

							auto& sssColor = lightingPBRMaterial->GetSubsurfaceColor();
							colors[2] = { sssColor.red, sssColor.green, sssColor.blue, 1.0f };
							scalars[2] = lightingPBRMaterial->GetSubsurfaceOpacity();
						}

						if (pbrFlags & PBRShaderFlags::TwoLayer) {
							textures[6] = GetTexture(lightingPBRMaterial->featuresTexture0, whiteTexture);
							textures[7] = GetTexture(lightingPBRMaterial->featuresTexture1, whiteTexture);

							auto& coatColor = lightingPBRMaterial->GetSubsurfaceColor();
							float coatStrength = lightingPBRMaterial->GetSubsurfaceOpacity();
							colors[2] = { coatColor.red, coatColor.green, coatColor.blue, coatStrength };
							scalars[2] = lightingPBRMaterial->coatRoughness;
						}

						if (pbrFlags & PBRShaderFlags::Fuzz) {
							textures[7] = GetTexture(lightingPBRMaterial->featuresTexture1, whiteTexture);

							colors[2] = { lightingPBRMaterial->fuzzColor.red, lightingPBRMaterial->fuzzColor.green, lightingPBRMaterial->fuzzColor.blue, lightingPBRMaterial->fuzzWeight };
						}

						if (pbrFlags & PBRShaderFlags::Glint) {
							const auto& glint = lightingPBRMaterial->GetGlintParameters();
							vectors[3] = { glint.screenSpaceScale, glint.logMicrofacetDensity, glint.microfacetRoughness, glint.densityRandomization };
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

								scalars[0] = Util::Material::Skyrim::ShininessToRoughness(lightingBaseMaterial->specularPower);
							}

							// SSS color
							if (lightingShaderProp->flags.all(EShaderPropertyFlag::kSoftLighting)) {								
								textures[6] = GetTexture(lightingBaseMaterial->rimSoftLightingTexture, blackTexture);
							}

							// Envmap / Eye
							if (feature == Feature::kEnvironmentMap || feature == Feature::kEye) {
								if (const auto* lightingEnvmapMaterial = skyrim_cast<RE::BSLightingShaderMaterialEnvmap*>(shaderMaterial)) {
									textures[4] = GetCubemapTexture(lightingEnvmapMaterial->envTexture, blackTexture);
									textures[5] = GetTexture(lightingEnvmapMaterial->envMaskTexture, whiteTexture);
								} else if (const auto* lightingEyeMaterial = skyrim_cast<RE::BSLightingShaderMaterialEye*>(shaderMaterial)) {
									textures[4] = GetCubemapTexture(lightingEyeMaterial->envTexture, blackTexture);
									textures[5] = GetTexture(lightingEyeMaterial->envMaskTexture, whiteTexture);
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

							//							textures[6] = GetTexture(lightingPBRMaterial->featuresTexture0, blackTexture);

							// FaceGen
							if (feature == Feature::kFaceGen) {
								if (const auto* lightingFacegenMaterial = skyrim_cast<RE::BSLightingShaderMaterialFacegen*>(shaderMaterial)) {
									if (Util::IsPlayer(form)) {
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

							// Skin Tint
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

			if (auto waterShaderProp = netimmerse_cast<RE::BSWaterShaderProperty*>(effect)) {
				shaderType = RE::BSShader::Type::Water;
				waterShaderFlags = static_cast<Material::WaterShaderFlags>(waterShaderProp->waterFlags.underlying());

				if (auto waterMaterial = skyrim_cast<RE::BSWaterShaderMaterial*>(waterShaderProp->material)) {
					colors[0] = {
						waterMaterial->shallowWaterColor.red,
						waterMaterial->shallowWaterColor.green,
						waterMaterial->shallowWaterColor.blue,
						1.0f
					};

					colors[1] = {
						waterMaterial->deepWaterColor.red,
						waterMaterial->deepWaterColor.green,
						waterMaterial->deepWaterColor.blue,
						waterMaterial->deepWaterColor.alpha
					};

					colors[2] = {
						waterMaterial->reflectionColor.red,
						waterMaterial->reflectionColor.green,
						waterMaterial->reflectionColor.blue,
						waterMaterial->reflectionColor.alpha
					};

					// NormalsAmplitude
					scalars[0] = waterMaterial->amplitudeA[0];
					scalars[1] = waterMaterial->amplitudeA[1];
					scalars[2] = waterMaterial->amplitudeA[2];

					// NormalsScale
					vectors[1].z = waterMaterial->uvScaleA[0];
					vectors[1].w = waterMaterial->uvScaleA[1];
					vectors[2].x = waterMaterial->uvScaleA[2];

					// ObjectUV
					vectors[2].y = 0.0f;
					vectors[2].z = 0.0f;
					vectors[2].w = 0.0f;

					textures[0] = GetTexture(waterMaterial->normalTexture1, normalTexture);
					textures[1] = GetTexture(waterMaterial->normalTexture2, normalTexture);
					textures[2] = GetTexture(waterMaterial->normalTexture3, normalTexture);
					textures[3] = GetTexture(waterMaterial->normalTexture4, normalTexture);
				}
			}
		}
	}

	if (shaderType == RE::BSShader::Type::Water) {
		alphaFlags = Material::AlphaFlags::Transmission;
	}

	// Fallback to alpha test if possible
	bool blendMaterial = feature == Feature::kHairTint || feature == Feature::kFaceGen || feature == Feature::kFaceGenRGBTint || feature == Feature::kEye;
	if ((alphaFlags & Material::AlphaFlags::Blend) != Material::AlphaFlags::None && !blendMaterial) {
		alphaFlags &= ~Material::AlphaFlags::Blend;

		alphaFlags |= Material::AlphaFlags::Transmission;  // I want them to behave like glass for now
	}

	// Refraction materials: treat as transmission (glass)
	if (shaderFlags.any(EShaderPropertyFlag::kRefraction) && alphaFlags == Material::AlphaFlags::None) {
		alphaFlags |= Material::AlphaFlags::Transmission;
	}

	// Window transparency: mark window materials (GlowMap/HasEmissive + AssumeShadowmask) as non-opaque
	// so the any-hit shader can compute transmittance for shadow rays
	bool isWindow = (feature == Feature::kGlowMap || (pbrFlags & PBRShaderFlags::HasEmissive)) &&
	                shaderFlags.any(EShaderPropertyFlag::kAssumeShadowmask);
	if (isWindow && alphaFlags == Material::AlphaFlags::None) {
		alphaFlags |= Material::AlphaFlags::Transmission;
	}

	// Attempt to clear up fake positives
	if (shaderFlags.all(EShaderPropertyFlag::kTwoSided))
		flags.reset(Mesh::Flags::DoubleSidedGeom);

	geometryDesc.flags = (alphaFlags == Material::AlphaFlags::None) ? nvrhi::rt::GeometryFlags::Opaque : nvrhi::rt::GeometryFlags::None;

	material = Material(
		shaderFlags,
		waterShaderFlags,
		shaderType,
		feature,
		pbrFlags,
		alphaFlags,
		alphaThreshold,
		colors,
		scalars,
		vectors,
		texCoordOffsetScales,
		textures);
}

void Mesh::CreateBuffers(SceneGraph* sceneGraph, nvrhi::ICommandList* commandList)
{
	auto device = Renderer::GetSingleton()->GetDevice();

	bool updatable = flags.any(Flags::Dynamic, Flags::Skinned);

	logger::debug("Mesh::CreateBuffers - {}", m_Name);

	// Triangle Buffer
	{
		const size_t size = sizeof(Triangle) * triangleData.count;

		logger::debug("Mesh::CreateBuffers - Triangle Count: {}, Buffer Size: {}", triangleData.count, size);

		auto& triangleBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(Triangle))
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setIsAccelStructBuildInput(true)
			.setDebugName(std::format("{} (Triangle Buffer)", m_Name.c_str()));

		buffers.triangleBuffer = device->createBuffer(triangleBufferDesc);

		commandList->writeBuffer(buffers.triangleBuffer, triangleData.triangles.data(), size);

		{
			// Create SRV binding for triangles
			auto triangleBindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(0, buffers.triangleBuffer);
			// Register descriptor, get handle within heap and writes the SRV
			m_DescriptorHandle = sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->CreateDescriptorHandle(triangleBindingSet);
		}
	}

	auto descriptorIndex = m_DescriptorHandle.Get();

	if (flags.all(Flags::Dynamic)) {
		const size_t size = sizeof(float4) * vertexData.count;

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
		const size_t size = sizeof(Vertex) * vertexData.count;

		logger::debug("Mesh::CreateBuffers - Vertex Count: {}, Buffer Size: {}", vertexData.count, size);

		auto& vertexBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(Vertex))
			.setCanHaveUAVs(updatable)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setIsAccelStructBuildInput(true)
			.setDebugName(std::format("{} (Vertex Buffer)", m_Name.c_str()));

		buffers.vertexBuffer = device->createBuffer(vertexBufferDesc);

		commandList->writeBuffer(buffers.vertexBuffer, vertexData.vertices.data(), size);

		auto vertexBindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffers.vertexBuffer);
		device->writeDescriptorTable(sceneGraph->GetVertexDescriptors()->m_DescriptorTable, vertexBindingSet);

		if (updatable) {
			auto uavBindingSet = nvrhi::BindingSetItem::StructuredBuffer_UAV(descriptorIndex, buffers.vertexBuffer);
			device->writeDescriptorTable(sceneGraph->GetVertexWriteDescriptors()->m_DescriptorTable, uavBindingSet);
		}
	}

	// Vertex Copy
	if (updatable) {
		const size_t size = sizeof(Vertex) * vertexData.count;

		auto& vertexCopyBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(Vertex))
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setDebugName(std::format("{} (Vertex Copy Buffer)", m_Name.c_str()));

		buffers.vertexCopyBuffer = device->createBuffer(vertexCopyBufferDesc);

		commandList->writeBuffer(buffers.vertexCopyBuffer, vertexData.vertices.data(), size);

		auto bindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffers.vertexCopyBuffer);
		device->writeDescriptorTable(sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable, bindingSet);
	}

	if (flags.all(Flags::Skinned)) {
		const size_t size = sizeof(Skinning) * vertexData.count;

		auto& skinningBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(Skinning))
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setDebugName(std::format("{} (Skinning Buffer)", m_Name.c_str()));

		buffers.skinningBuffer = device->createBuffer(skinningBufferDesc);

		commandList->writeBuffer(buffers.skinningBuffer, vertexData.skinning.data(), size);

		auto bindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffers.skinningBuffer);
		device->writeDescriptorTable(sceneGraph->GetSkinningDescriptors()->m_DescriptorTable, bindingSet);

		// Previous position buffer for per-vertex motion vectors
		const size_t prevPosSize = sizeof(float3) * vertexData.count;

		auto& prevPositionBufferDesc = nvrhi::BufferDesc()
			.setByteSize(prevPosSize)
			.setStructStride(sizeof(float3))
			.setCanHaveUAVs(true)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setDebugName(std::format("{} (Prev Position Buffer)", m_Name.c_str()));

		buffers.prevPositionBuffer = device->createBuffer(prevPositionBufferDesc);

		auto prevPosSrvBinding = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffers.prevPositionBuffer);
		device->writeDescriptorTable(sceneGraph->GetPrevPositionDescriptors()->m_DescriptorTable, prevPosSrvBinding);

		auto prevPosUavBinding = nvrhi::BindingSetItem::StructuredBuffer_UAV(descriptorIndex, buffers.prevPositionBuffer);
		device->writeDescriptorTable(sceneGraph->GetPrevPositionWriteDescriptors()->m_DescriptorTable, prevPosUavBinding);
	}

	// Geometry description
	auto& geometryTriangles = geometryDesc.geometryData.triangles;

	geometryTriangles.indexBuffer = buffers.triangleBuffer;
	geometryTriangles.indexOffset = 0;
	geometryTriangles.indexFormat = nvrhi::Format::R16_UINT;
	geometryTriangles.indexCount = triangleData.count * 3;

	geometryTriangles.vertexBuffer = buffers.vertexBuffer;
	geometryTriangles.vertexOffset = 0;
	geometryTriangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
	geometryTriangles.vertexStride = sizeof(Vertex);
	geometryTriangles.vertexCount = vertexData.count;

	geometryDesc.setTransform(localToRoot.f);
}

bool Mesh::UpdateDynamicPosition()
{
	auto* dynamicTriShape = reinterpret_cast<RE::BSDynamicTriShape*>(bsGeometryPtr.get());
	auto& runtimeData = dynamicTriShape->GetDynamicTrishapeRuntimeData();

	if (!runtimeData.dynamicData)
		return false;

	auto& dataSize = runtimeData.dataSize;

	// Is this even a possibility?
	if (dataSize == 0)
		return false;

	runtimeData.lock.Lock();

	// Has dynamic position changed?
	if (std::memcmp(vertexData.dynamicPosition.data(), runtimeData.dynamicData, dataSize) == 0) {
		runtimeData.lock.Unlock();
		return false;
	}

	std::memcpy(vertexData.dynamicPosition.data(), runtimeData.dynamicData, dataSize);
	runtimeData.lock.Unlock();

	return true;
}

void Mesh::UpdateUploadDynamicBuffers(nvrhi::ICommandList* commandList)
{
	if (flags.none(Flags::Dynamic))
		return;

	commandList->writeBuffer(buffers.dynamicPositionBuffer, vertexData.dynamicPosition.data(), sizeof(float4) * vertexData.count);
}

bool Mesh::UpdateSkinning(RE::NiAVObject* object, bool isPlayer)
{
	// Update Bone matrices
	auto* skinInstance = Util::Adapter::CLib::GetSkinInstance(bsGeometryPtr.get());

	// RaceMenu crash fix
	if (!skinInstance)
		return false;

	const auto frameID = skinInstance->frameID;

	if (frameID == Constants::INVALID_FRAME_ID)
		return false;

	auto* rootParent = skinInstance->rootParent;

	// UBE crash fix
	if (!rootParent)
		return false;

	// Mostly for COtR, head geometry becomes unparented after the first few frames
	const bool unparentedPlayer = isPlayer && !rootParent->parent;

	// Only update if the game has updated the animation
	// Part 1 of COtR fix
	if (!unparentedPlayer && m_FrameID == frameID)
		return false;

	m_FrameID = frameID;

	auto skinRoot = rootParent->world;

	static auto identity = RE::NiTransform();

	// If skin transform is identity it will break skinning
	if (skinRoot == identity)
		return false;

	// Part 2 of COtR fix
	if (unparentedPlayer)
		skinRoot = object->world;

	auto* skinData = skinInstance->skinData.get();

	if (!skinData)
		return false;

	if (skinInstance->numMatrices != skinData->bones)
		logger::warn("Mesh::UpdateSkinning - Num Matrices: {}, Num Bones: {}", skinInstance->numMatrices, skinData->bones);

	if (skinData->bones == 0)
		return false;

	auto skinRootInverse = skinRoot.Invert();

	if (m_BoneMatrices.empty() || skinData->bones != m_BoneMatrices.size())
		m_BoneMatrices.resize(skinData->bones);

	for (uint i = 0; i < skinData->bones; i++) {
		auto boneWorld = *skinInstance->boneWorldTransforms[i];
		auto boneMatrix = boneWorld * skinData->boneData[i].skinToBone;
		XMStoreFloat3x4(&m_BoneMatrices[i], Util::Math::GetXMFromNiTransform(skinRootInverse * boneMatrix));
	}

	return true;
}

bool Mesh::UpdateTransform(RE::NiAVObject* object)
{
	if (m_FormType != RE::FormType::Weapon)
		return false;

	// Update local to root transform
	XMStoreFloat3x4(&localToRoot, Util::Math::GetXMFromNiTransform(object->world.Invert() * bsGeometryPtr->world));

	return true;
}

void Mesh::UpdateDismember()
{
	if (!bsGeometryPtr)
		return;

	auto* skinInstance = Util::Adapter::CLib::GetSkinInstance(bsGeometryPtr.get());

	if (!skinInstance)
		return;

	static REL::Relocation<const RE::NiRTTI*> dismemberRTTI{ RE::BSDismemberSkinInstance::Ni_RTTI };

	if (skinInstance->GetRTTI() != dismemberRTTI.get())
		return;

	auto* dismemberSkinInstance = reinterpret_cast<RE::BSDismemberSkinInstance*>(skinInstance);
	auto& dismemberRuntime = dismemberSkinInstance->GetRuntimeData();

	if (dismemberRuntime.numPartitions == 0)
		return;

	auto& partition = dismemberRuntime.partitions[m_Partition];

	m_PendingState.set(!partition.editorVisible, State::DismemberHidden);
}

DirtyFlags Mesh::Update(RE::NiAVObject* object, bool isPlayer)
{
	// Only this reference remains, so erase it
	if (bsGeometryPtr->GetRefCount() == 1) {
		logger::warn("Mesh::Update - Released BSGeometry being referenced in {}", m_Name);
		return DirtyFlags::None;
	}

	const auto dynamic = flags.all(Mesh::Flags::Dynamic);
	const auto skinned = flags.all(Mesh::Flags::Skinned);

	// I don't know if kHidden is set on inner nodes for culling, so to be safe we check only for dynamic and skinned geometry
	if (dynamic || skinned)
		m_PendingState.set(Util::Game::IsHidden(bsGeometryPtr.get()), State::Hidden);

	if (skinned)
		UpdateDismember();

	// Store previous hidden state
	bool wasHidden = IsHidden();

	// Update states
	m_State = m_PendingState;

	// Current hiddent state
	bool isHidden = IsHidden();

	// Becomes hidden this frame
	if (!wasHidden && isHidden)
		return DirtyFlags::Visibility;

	// Nothing to update
	if (wasHidden && isHidden)
		return DirtyFlags::None;

	auto updateFlags = DirtyFlags::None;

	// Becomes visible this frame
	if (wasHidden && !isHidden)
		updateFlags |= DirtyFlags::Visibility;

	if (dynamic && UpdateDynamicPosition())
		updateFlags |= DirtyFlags::Vertex;

	if (skinned && UpdateSkinning(object, isPlayer))
		updateFlags |= DirtyFlags::Skin;

	if (!skinned && UpdateTransform(object))
		updateFlags |= DirtyFlags::Transform;

	geometryDesc.setTransform(localToRoot.f);

	return updateFlags;
}

MeshData Mesh::GetData(const float3 externalEmittance)
{
	auto* bsMaterial = bsGeometryPtr ? reinterpret_cast<RE::BSShaderProperty*>(bsGeometryPtr->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get()) : nullptr;

	return MeshData(
		material.GetData(externalEmittance, bsMaterial),
		static_cast<uint32_t>(m_DescriptorHandle.Get()),
		static_cast<uint32_t>(flags.get()),
		0u,
		localToRoot
	);
}

bool Mesh::IsHidden() const
{
	return m_State.any(State::Hidden,State::DismemberHidden);
}

void Mesh::CalculateNormals()
{
	eastl::vector<float3> normals;
	normals.resize(vertexData.count, float3(0, 0, 0));

	// Loop over triangles
	for (auto& t : triangleData.triangles) {
		Vertex& v0 = vertexData.vertices[t.x];
		Vertex& v1 = vertexData.vertices[t.y];
		Vertex& v2 = vertexData.vertices[t.z];

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
	for (size_t i = 0; i < vertexData.count; i++) {
		vertexData.vertices[i].Normal = Util::Math::Normalize(normals[i]);
	}
}
