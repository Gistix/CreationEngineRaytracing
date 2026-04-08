#pragma once

#include <dxcapi.h>
#include "Types/Settings.h"
#include "Types/ShaderDefine.h"

namespace Util
{
	namespace Shader
	{
		eastl::vector<ShaderDefine> GetRaytracingDefines(const Settings& settings, bool sharc, bool sharcUpdate);
		eastl::vector<ShaderDefine> GetPathTracingDefines(const Settings& settings, bool sharc, bool sharcUpdate);
		eastl::vector<ShaderDefine> GetGlobalIlluminationDefines(const Settings& settings, bool sharc, bool sharcUpdate);

		eastl::vector<DxcDefine> GetDXCDefines(const eastl::vector<ShaderDefine>& defines);
	}
}