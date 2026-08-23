#include "Shader.h"

#include "Types/InstanceMask.h"
#include "Constants.h"

namespace Util
{
	namespace Shader
	{
		eastl::vector<ShaderDefine> GetRaytracingDefines(const Settings& settings, bool sharc, bool sharcUpdate)
		{
			const bool sharcEnabled = sharc && settings.SHaRCSettings.Enabled;

			eastl::vector<ShaderDefine> defines = {
				{ L"MAX_BOUNCES", settings.RaytracingSettings.Bounces },
				{ L"MAX_SAMPLES", settings.RaytracingSettings.SamplesPerPixel },
				{ L"RUSSIAN_ROULETTE", static_cast<uint32_t>(settings.RaytracingSettings.RussianRoulette) },
				{ L"SHARC_UPDATE", sharcUpdate ? L"1" : L"0" },
				{ L"SHARC_RESOLVE", L"0" },
				{ L"SHARC_DEBUG", L"0" },
				{ L"SKIN_DETAIL_NORMAL", L"1" },
				{ L"DEBUG_TRACE_HEATMAP", L"0" }
			};

			if (settings.AdvancedSettings.GGXEnergyConservation)
				defines.emplace_back(L"GGX_ENERGY_CONSERVATION");

			if (settings.AdvancedSettings.RIS.Enabled) {
				defines.emplace_back(L"RIS");
				defines.emplace_back(L"RIS_MAX_CANDIDATES", settings.AdvancedSettings.RIS.MaxCandidates);
			}

			defines.emplace_back(L"DIFFUSE_MODE", static_cast<int>(settings.AdvancedSettings.DiffuseBRDF));

			if (settings.ExperimentalSettings.GlobalLights)
				defines.emplace_back(L"GLOBAL_LIGHTS", L"1");

			if (sharcEnabled)
				defines.emplace_back(L"SHARC");

			return defines;
		}

		eastl::vector<ShaderDefine> GetPathTracingDefines(const Settings& settings, bool sharc, bool sharcUpdate)
		{
			eastl::vector<ShaderDefine> defines = GetRaytracingDefines(settings, sharc, sharcUpdate);

			defines.emplace_back(L"THREAD_GROUP_SIZE", Constants::PT_DISPATCH_THREADS);

			defines.emplace_back(L"HAS_PREV_POSITIONS", L"1");
			
			defines.emplace_back(L"HAIR_MODE", static_cast<int>(settings.AdvancedSettings.HairBSDF));

			if (settings.AdvancedSettings.SSSSettings.Enabled)
				defines.emplace_back(L"SUBSURFACE_SCATTERING");

			if (!sharc || (sharc && !sharcUpdate)) {
				const bool reblur = settings.GeneralSettings.Denoiser == Denoiser::NRD_Reblur;
				const bool relax = settings.GeneralSettings.Denoiser == Denoiser::NRD_Relax;

				if (reblur) {
					defines.emplace_back(L"NRD", L"1");
					defines.emplace_back(L"NRD_REBLUR", L"1");
				} else if (relax) {
					defines.emplace_back(L"NRD", L"1");
					defines.emplace_back(L"NRD_RELAX", L"1");
				} else if (settings.GeneralSettings.Denoiser == Denoiser::DLSS_RR)
					defines.emplace_back(L"DLSS_RR", L"1");

				if (settings.GeneralSettings.ShadowDenoiser == ShadowDenoiser::NRD_Sigma && (reblur || relax)) {
					defines.emplace_back(L"NRD_SIGMA", L"1");
				}

				if (settings.AdvancedSettings.StablePlanes)
					defines.emplace_back(L"STABLE_PLANES");

				if (settings.ReSTIRGI.Enabled)
					defines.emplace_back(L"RESTIR_GI");
			}

			return defines;
		}

		eastl::vector<ShaderDefine> GetGlobalIlluminationDefines(const Settings& settings, bool sharc, bool sharcUpdate)
		{
			eastl::vector<ShaderDefine> defines = GetRaytracingDefines(settings, sharc, sharcUpdate);

			defines.emplace_back(L"THREAD_GROUP_SIZE", Constants::GI_DISPATCH_THREADS);

			// No water in GI
			auto instanceMask = InstanceMask::All & ~InstanceMask::Water;
			defines.emplace_back(L"INSTANCE_MASK", instanceMask);

			if (!sharc || (sharc && !sharcUpdate)) {
				if (settings.GeneralSettings.Denoiser == Denoiser::NRD_Reblur) {
					defines.emplace_back(L"RAW_RADIANCE", L"1");
					defines.emplace_back(L"NRD", L"1");
					defines.emplace_back(L"NRD_REBLUR", L"1");
				} else if (settings.GeneralSettings.Denoiser == Denoiser::NRD_Relax) {
					defines.emplace_back(L"RAW_RADIANCE", L"1");
					defines.emplace_back(L"NRD", L"1");
					defines.emplace_back(L"NRD_RELAX", L"1");
				} else if (settings.GeneralSettings.Denoiser == Denoiser::DLSS_RR)
					defines.emplace_back(L"DLSS_RR", L"1");
			}

			return defines;
		}

		eastl::vector<ShaderDefine> GetDebugDefines(const Settings& settings)
		{
			eastl::vector<ShaderDefine> defines = GetRaytracingDefines(settings, false, false);

			defines.emplace_back(L"THREAD_GROUP_SIZE", Constants::DEBUG_DISPATCH_THREADS);

			return defines;
		}

		eastl::vector<DxcDefine> GetDXCDefines(const eastl::vector<ShaderDefine>& defines) {
			auto numDefines = defines.size();

			eastl::vector<DxcDefine> dxcDefines(numDefines);

			for (size_t i = 0; i < numDefines; i++)
			{
				auto& define = defines[i];
				dxcDefines[i] = { define.name.c_str(), define.value.c_str() };
			}

			return dxcDefines;
		}
	}
}
