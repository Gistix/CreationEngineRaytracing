#include "Shader.h"

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
				{ L"SHARC_UPDATE", sharcUpdate ? L"1" : L"0" },
				{ L"SHARC_RESOLVE", L"0" },
				{ L"SHARC_DEBUG", L"0" },
				{ L"DEBUG_TRACE_HEATMAP", L"0" },
				{ L"ALT_PBR_CONV_ROUGHNESS", L"0" },
				{ L"ALT_PBR_CONV_METALLIC", L"0" }
			};

			if (settings.AdvancedSettings.GGXEnergyConservation)
				defines.emplace_back(L"GGX_ENERGY_CONSERVATION");

			if (settings.AdvancedSettings.PerLightTLAS)
				defines.emplace_back(L"USE_LIGHT_TLAS", L"1");

			if (settings.AdvancedSettings.RIS.Enabled) {
				defines.emplace_back(L"RIS");
				defines.emplace_back(L"RIS_MAX_CANDIDATES", settings.AdvancedSettings.RIS.MaxCandidates);
			}

			defines.emplace_back(L"DIFFUSE_MODE", static_cast<int>(settings.AdvancedSettings.DiffuseBRDF));

			if (sharcEnabled)
				defines.emplace_back(L"SHARC");

			if (!sharcUpdate) {
				if (settings.GeneralSettings.Denoiser == Denoiser::NRD_REBLUR) {
					defines.emplace_back(L"RAW_RADIANCE", L"1");
					
					defines.emplace_back(L"NRD", L"1");
					defines.emplace_back(L"NRD_REBLUR", L"1");
				}

				if (settings.GeneralSettings.Denoiser == Denoiser::DLSS_RR)
					defines.emplace_back(L"DLSS_RR", L"1");
			}

			return defines;
		}

		eastl::vector<ShaderDefine> GetPathTracingDefines(const Settings& settings, bool sharc, bool sharcUpdate)
		{
			eastl::vector<ShaderDefine> defines = GetRaytracingDefines(settings, sharc, sharcUpdate);

			defines.emplace_back(L"HAIR_MODE", static_cast<int>(settings.AdvancedSettings.HairBSDF));

			if (settings.AdvancedSettings.SSSSettings.Enabled)
				defines.emplace_back(L"SUBSURFACE_SCATTERING");

			if (!sharc || (sharc && !sharcUpdate)) {
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