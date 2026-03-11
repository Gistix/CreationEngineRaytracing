#include "Shader.h"

namespace Util
{
	namespace Shader
	{
		eastl::vector<ShaderDefine> GetRaytracingDefines(const Settings& settings, bool sharc, bool sharcUpdate)
		{
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

			/*if (lightTLAS)
				defines.emplace_back(L"USE_LIGHT_TLAS", L"0");*/

			if (sharc)
				defines.emplace_back(L"SHARC", L"");

			if (settings.GeneralSettings.Denoiser == Denoiser::DLSS_RR)
				defines.emplace_back(L"DLSS_RR", L"1");
			
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