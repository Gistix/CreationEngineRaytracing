#if defined(SKYRIM)
#include "Core/Skyrim/Material.h"

#include "Scene.h"
#include "Renderer.h"

Material::Material(const eastl::string& name, const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& runtimeData, RE::FormID formID)
{
	auto* renderer = Renderer::GetSingleton();

	auto& grayTexture = renderer->GetGrayTextureIndex();
	auto& normalTexture = renderer->GetNormalTextureIndex();
	auto& blackTexture = renderer->GetBlackTextureIndex();
	auto& whiteTexture = renderer->GetWhiteTextureIndex();
	auto& rmaosTexture = renderer->GetRMAOSTextureIndex();
	auto& detailTexture = renderer->GetDetailTextureIndex();

	using Feature = RE::BSShaderMaterial::Feature;
	using EShaderPropertyFlag = RE::BSShaderProperty::EShaderPropertyFlag;

	colors.fill(float4(1.0f, 1.0f, 1.0f, 1.0f));
	colors[Constants::Material::EMISSIVE_COLOR] = float4(0.0f, 0.0f, 0.0f, 0.0f);

	scalars.fill(0.0f);

	vectors.fill(float4(1.0f, 1.0f, 1.0f, 1.0f));

	texCoordOffsetScale.fill(float4(0.0f, 0.0f, 1.0f, 1.0f));

	alphaThreshold = 0.5f;

	textures.fill(Texture(blackTexture, nullptr));

	auto* alphaProperty = runtimeData.alphaProperty.get();

	// Set alpha flags
	if (alphaProperty) {
		if (alphaProperty->GetAlphaBlending()) {
			using AlphaFunction = RE::NiAlphaProperty::AlphaFunction;

			if (alphaProperty->GetDestBlendMode() == AlphaFunction::kOne) {
				alphaFlags |= Material::AlphaFlags::Additive;
				alphaFlags |= Material::AlphaFlags::Transmission;
			}
			else {
				alphaFlags |= Material::AlphaFlags::Blend;
			}
		}

		if (alphaProperty->GetAlphaTesting()) {
			alphaFlags |= Material::AlphaFlags::Test;
			alphaThreshold = alphaProperty->alphaThreshold / 255.0f;
		}
	}

	auto* shaderProperty = runtimeData.shaderProperty.get();

	if (shaderProperty) {
		shaderFlags = shaderProperty->flags.get();
		colors[0].w = shaderProperty->alpha;

		if (RE::BSLightingShaderProperty* lightingShaderProp = skyrim_cast<RE::BSLightingShaderProperty*>(shaderProperty)) {
			shaderType = RE::BSShader::Type::Lighting;

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
					if (lightingBaseMaterial->materialAlpha != 1.0f && !alphaProperty) {
						colors[0].w = lightingBaseMaterial->materialAlpha;
						alphaFlags |= Material::AlphaFlags::Blend;
					}
				}

				// Some eye meshes use EnvironmentMap shader instead of Eye shader;
				// detect them by geometry name and override the feature
				if (feature == Feature::kEnvironmentMap) {
					eastl::string nameLower(name);
					for (auto& c : nameLower)
						c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					if (nameLower.find("eye") != eastl::string::npos) {
						feature = Feature::kEye;
						logger::debug("[RT] BuildMaterial - Overriding EnvironmentMap to Eye for mesh: {}", name.c_str());
					}
				}

				for (size_t i = 0; i < 2; i++) {
					texCoordOffsetScale[i] = {
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

					colors[2] = {
						lightingBaseMaterialLand->specularColor.red,
						lightingBaseMaterialLand->specularColor.green,
						lightingBaseMaterialLand->specularColor.blue,
						lightingBaseMaterialLand->specularColorScale
					};

					scalars[0] = Util::Material::Skyrim::ShininessToRoughness(lightingBaseMaterialLand->specularPower);

					vectors[0] = Util::Math::Float4(lightingBaseMaterialLand->landBlendParams);

					vectors[1] = {
						lightingBaseMaterialLand->textureIsSnow[0],
						lightingBaseMaterialLand->textureIsSnow[1],
						lightingBaseMaterialLand->textureIsSnow[2],
						lightingBaseMaterialLand->textureIsSnow[3]
					};

					vectors[2] = {
						lightingBaseMaterialLand->textureIsSnow[4],
						lightingBaseMaterialLand->textureIsSnow[5],
						Util::Material::Skyrim::ShininessToRoughness(lightingBaseMaterialLand->textureIsSpecPower[0]),
						Util::Material::Skyrim::ShininessToRoughness(lightingBaseMaterialLand->textureIsSpecPower[1])
					};

					vectors[3] = {
						Util::Material::Skyrim::ShininessToRoughness(lightingBaseMaterialLand->textureIsSpecPower[2]),
						Util::Material::Skyrim::ShininessToRoughness(lightingBaseMaterialLand->textureIsSpecPower[3]),
						Util::Material::Skyrim::ShininessToRoughness(lightingBaseMaterialLand->textureIsSpecPower[4]),
						Util::Material::Skyrim::ShininessToRoughness(lightingBaseMaterialLand->textureIsSpecPower[5])
					};

					/*vectors[4] = {
						lightingBaseMaterialLand->terrainTexOffsetX,
						lightingBaseMaterialLand->terrainTexOffsetY,
						lightingBaseMaterialLand->terrainTexFade,
						0
					};*/
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

					pbrFlags = Util::Material::Skyrim::GetPBRShaderFlags(lightingPBRMaterialLand);

					vectors[0] = Util::Math::Float4(lightingPBRMaterialLand->landBlendParams);

					vectors[1] = {
						lightingPBRMaterialLand->specularLevels[0],
						lightingPBRMaterialLand->specularLevels[1],
						lightingPBRMaterialLand->specularLevels[2],
						lightingPBRMaterialLand->specularLevels[3]
					};

					vectors[2] = {
						lightingPBRMaterialLand->specularLevels[4],
						lightingPBRMaterialLand->specularLevels[5],
						lightingPBRMaterialLand->roughnessScales[0],
						lightingPBRMaterialLand->roughnessScales[1]
					};

					vectors[3] = {
						lightingPBRMaterialLand->roughnessScales[2],
						lightingPBRMaterialLand->roughnessScales[3],
						lightingPBRMaterialLand->roughnessScales[4],
						lightingPBRMaterialLand->roughnessScales[5]
					};

					// LODTexParams - TerrainTexOffset in xy, LodBlendingEnabled in z
					/*vectors[4] = {
						lightingPBRMaterialLand->terrainTexOffsetX,
						lightingPBRMaterialLand->terrainTexOffsetY,
						0,
						lightingPBRMaterialLand->terrainTexFade
					};*/

					// Enforce TruePBR flag
					shaderFlags.set(EShaderPropertyFlag::kMenuScreen);
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

						auto textureType = shaderFlags.all(EShaderPropertyFlag::kModelSpaceNormals) ? TextureType::ModelSpaceNormalMap : TextureType::Standard;
						textures[Constants::Material::NORMALMAP_TEXTURE] = GetTexture(lightingBaseMaterial->normalTexture, normalTexture, textureType);

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

						// Envmap
						if (feature == Feature::kEnvironmentMap) {
							if (const auto* lightingEnvmapMaterial = skyrim_cast<RE::BSLightingShaderMaterialEnvmap*>(shaderMaterial)) {
								textures[4] = GetTexture(lightingEnvmapMaterial->envTexture, blackTexture, TextureType::CubeMap);
								textures[5] = GetTexture(lightingEnvmapMaterial->envMaskTexture, whiteTexture);
							}
						}

						// Eye
						if (feature == Feature::kEye) {
							if (const auto* lightingEyeMaterial = skyrim_cast<RE::BSLightingShaderMaterialEye*>(shaderMaterial)) {
								textures[4] = GetTexture(lightingEyeMaterial->envTexture, blackTexture, TextureType::CubeMap);
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

						// FaceGen
						if (feature == Feature::kFaceGen) {
							if (const auto* lightingFacegenMaterial = skyrim_cast<RE::BSLightingShaderMaterialFacegen*>(shaderMaterial)) {
								if (Util::IsPlayerFormID(formID)) {
									auto& gameRendererRuntimeData = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData();

									auto faceTintDescriptor = Scene::GetSingleton()->GetSceneGraph()->GetTextureManager()->GetDescriptor(
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
		else if (auto effectShaderProp = netimmerse_cast<RE::BSEffectShaderProperty*>(shaderProperty)) {
			shaderType = RE::BSShader::Type::Effect;

			if (auto effectMaterial = skyrim_cast<RE::BSEffectShaderMaterial*>(effectShaderProp->material)) {
				colors[Constants::Material::EMISSIVE_COLOR] = {
					effectMaterial->baseColor.red,
					effectMaterial->baseColor.green,
					effectMaterial->baseColor.blue,
					effectMaterial->baseColor.alpha
				};

				scalars[0] = effectMaterial->baseColorScale;

				textures[0] = GetTexture(effectMaterial->sourceTexture, blackTexture);
				textures[2] = GetTexture(effectMaterial->greyscaleTexture, blackTexture);
			}
		}
		else if (auto waterShaderProp = netimmerse_cast<RE::BSWaterShaderProperty*>(shaderProperty)) {
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
		else if (auto* distantTreeProp = netimmerse_cast<RE::BSDistantTreeShaderProperty*>(shaderProperty)) {
			shaderType = RE::BSShader::Type::DistantTree;

			auto* treeLODAtlas = Scene::GetSingleton()->g_TreeLODAtlasTex;
			textures[0] = GetTexture(*treeLODAtlas, blackTexture);
		}
	}

	if (shaderType == RE::BSShader::Type::Water) {
		alphaFlags = Material::AlphaFlags::Transmission;
	}

	bool blendMaterial = feature == Feature::kHairTint || feature == Feature::kFaceGen || feature == Feature::kFaceGenRGBTint || feature == Feature::kEye;
	blendMaterial |= shaderFlags.any(EShaderPropertyFlag::kDecal, EShaderPropertyFlag::kDynamicDecal);

	if ((alphaFlags & Material::AlphaFlags::Additive) != Material::AlphaFlags::None) {
		alphaFlags &= ~Material::AlphaFlags::Blend;
		alphaFlags |= Material::AlphaFlags::Transmission;
	}
	else if ((alphaFlags & Material::AlphaFlags::Blend) != Material::AlphaFlags::None && !blendMaterial) {
		alphaFlags &= ~Material::AlphaFlags::Blend;

		alphaFlags |= Material::AlphaFlags::Transmission;  // I want them to behave like glass for now
	}

	// Refraction materials: treat as transmission (glass)
	if (shaderFlags.any(EShaderPropertyFlag::kRefraction) && alphaFlags == Material::AlphaFlags::None) {
		alphaFlags |= Material::AlphaFlags::Transmission;
	}

	// Window transparency: mark window materials (GlowMap/HasEmissive + AssumeShadowmask) as non-opaque
	// so the any-hit shader can compute transmittance for shadow rays
	bool isWindow = (feature == Feature::kGlowMap || pbrFlags.any(PBRShaderFlags::HasEmissive)) &&
		shaderFlags.any(EShaderPropertyFlag::kAssumeShadowmask);
	if (isWindow && alphaFlags == Material::AlphaFlags::None) {
		alphaFlags |= Material::AlphaFlags::Transmission;
	}

	m_MaterialData = eastl::make_unique<MaterialData>();
	m_PrevMaterialData = eastl::make_unique<MaterialData>();
}

void Material::UpdateWaterMaterial(RE::BSShaderProperty* shaderProperty)
{
	auto* scene = Scene::GetSingleton();

	auto* bsWaterProperty = reinterpret_cast<RE::BSWaterShaderProperty*>(shaderProperty);
	if (!bsWaterProperty)
		return;

	auto* bsWaterMaterial = reinterpret_cast<RE::BSWaterShaderMaterial*>(bsWaterProperty->material);
	if (!bsWaterMaterial)
		return;

	int32_t flowMapSize = *scene->g_FlowMapSize;

	// ObjectUV
	if (waterShaderFlags.all(WaterShaderFlags::kVertexUV)) {
		vectors[2].y = 1.0f;
	}
	else if (waterShaderFlags.all(WaterShaderFlags::kEnableFlowmap)) {
		vectors[2].y = static_cast<float>(flowMapSize);
		vectors[2].z = scene->g_DisplacementMeshFlowCellOffset->x,
		vectors[2].w = 1.0f - scene->g_DisplacementMeshFlowCellOffset->y;
	}
	else {
		vectors[2].y = 0.0f;
	}


	if (waterShaderFlags.all(WaterShaderFlags::kEnableFlowmap)) {
		// CellTexCoordOffset 
		vectors[3] = {
			static_cast<float>(bsWaterProperty->flowX),
			static_cast<float>(flowMapSize - bsWaterProperty->flowY - 1),
			static_cast<float>(bsWaterProperty->cellX),
			static_cast<float>(-bsWaterProperty->cellY)
		};

		auto* defaultNormal = RE::BSGraphics::State::GetSingleton()->GetRuntimeData().defaultTextureNormalMap.get();

		if (bsWaterMaterial->normalTexture4.get() && bsWaterMaterial->normalTexture4.get() != defaultNormal) {
			auto& normalTexture = Renderer::GetSingleton()->GetNormalTextureIndex();
			textures[3] = GetTexture(bsWaterMaterial->normalTexture4, normalTexture);
		}
	}
	else {
		// NormalsScroll0
		vectors[0].x = bsWaterMaterial->normalScroll1.x;
		vectors[0].y = bsWaterMaterial->normalScroll1.y;

		vectors[0].z = bsWaterMaterial->normalScroll2.x;
		vectors[0].w = bsWaterMaterial->normalScroll2.y;

		// NormalsScroll1
		vectors[1].x = bsWaterMaterial->normalScroll3.x;
		vectors[1].y = bsWaterMaterial->normalScroll3.y;
	}
}

void Material::CreateBuffer(const eastl::string& name, DescriptorIndex descriptorIndex)
{
	const size_t size = sizeof(MaterialData);

	auto& bufferDesc = nvrhi::BufferDesc()
		.setByteSize(size)
		.setStructStride(size)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
		.setDebugName(std::format("{} (Material Buffer)", name.c_str()));

	auto device = Renderer::GetSingleton()->GetDevice();
	buffer = device->createBuffer(bufferDesc);

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
	auto bindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffer);
	device->writeDescriptorTable(sceneGraph->GetMaterialDescriptors()->m_DescriptorTable, bindingSet);
}

void Material::Update(RE::BSShaderProperty* shaderProperty)
{
	if (shaderType == RE::BSShader::Type::Water)
		UpdateWaterMaterial(shaderProperty);
}

void Material::UpdateData(nvrhi::ICommandList * commandList, const float3& externalEmittance) const
{
	auto color1 = colors[1];

	if (shaderFlags.all(RE::BSShaderProperty::EShaderPropertyFlag::kExternalEmittance)) {
		if (shaderFlags.all(RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit)) {
			color1.x *= externalEmittance.x;
			color1.y *= externalEmittance.y;
			color1.z *= externalEmittance.z;
		}
		else {
			color1.x = externalEmittance.x;
			color1.y = externalEmittance.y;
			color1.z = externalEmittance.z;
		}
	}

	m_MaterialData->TexCoordOffsetScale0 = texCoordOffsetScale[0];
	m_MaterialData->TexCoordOffsetScale1 = texCoordOffsetScale[1];

	m_MaterialData->Color0 = colors[0];
	m_MaterialData->Color1 = color1;
	m_MaterialData->Color2 = colors[2];

	m_MaterialData->AlphaThreshold = alphaThreshold;

	m_MaterialData->Scalar0 = scalars[0];
	m_MaterialData->Scalar1 = scalars[1];
	m_MaterialData->Scalar2 = scalars[2];

	m_MaterialData->Vector0 = vectors[0];
	m_MaterialData->Vector1 = vectors[1];
	m_MaterialData->Vector2 = vectors[2];
	m_MaterialData->Vector3 = vectors[3];

	m_MaterialData->Texture0 = GetTextureDescriptorIndex(0);
	m_MaterialData->Texture1 = GetTextureDescriptorIndex(1);
	m_MaterialData->Texture2 = GetTextureDescriptorIndex(2);
	m_MaterialData->Texture3 = GetTextureDescriptorIndex(3);
	m_MaterialData->Texture4 = GetTextureDescriptorIndex(4);
	m_MaterialData->Texture5 = GetTextureDescriptorIndex(5);

	m_MaterialData->Texture6 = GetTextureDescriptorIndex(6);
	m_MaterialData->Texture7 = GetTextureDescriptorIndex(7);
	m_MaterialData->Texture8 = GetTextureDescriptorIndex(8);
	m_MaterialData->Texture9 = GetTextureDescriptorIndex(9);
	m_MaterialData->Texture10 = GetTextureDescriptorIndex(10);
	m_MaterialData->Texture11 = GetTextureDescriptorIndex(11);

	m_MaterialData->Texture12 = GetTextureDescriptorIndex(12);
	m_MaterialData->Texture13 = GetTextureDescriptorIndex(13);
	m_MaterialData->Texture14 = GetTextureDescriptorIndex(14);
	m_MaterialData->Texture15 = GetTextureDescriptorIndex(15);
	m_MaterialData->Texture16 = GetTextureDescriptorIndex(16);
	m_MaterialData->Texture17 = GetTextureDescriptorIndex(17);

	m_MaterialData->Texture18 = GetTextureDescriptorIndex(18);
	m_MaterialData->Texture19 = GetTextureDescriptorIndex(19);

	m_MaterialData->AlphaFlags = static_cast<uint16_t>(alphaFlags);
	m_MaterialData->ShaderType = GetShaderType();
	m_MaterialData->Feature = static_cast<uint16_t>(feature);
	m_MaterialData->PBRFlags = pbrFlags.underlying();
	m_MaterialData->ShaderFlags = GetShaderFlags();

	MaterialData* materialData = m_MaterialData.get();
	MaterialData* prevMaterialData = m_PrevMaterialData.get();

	if (*materialData == *prevMaterialData)
		return;

	commandList->writeBuffer(buffer, GetData(), sizeof(MaterialData));

	*m_PrevMaterialData.get() = *m_MaterialData.get();
}

MaterialData* Material::GetData() const
{
	return m_MaterialData.get();
}
#endif