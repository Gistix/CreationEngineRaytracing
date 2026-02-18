#pragma once

#include "Constants.h"

#include "Utils/Geometry.h"

namespace Util
{
	bool IsPlayerFormID(RE::FormID formID);

	bool IsPlayer(RE::TESForm* form);

	std::string WStringToString(const std::wstring& wideString);

	std::wstring StringToWString(const std::string& str);

	float3 Normalize(float3 vector);

	template <typename T>
	std::string GetFlagsString(auto value);

	DirectX::XMMATRIX GetXMFromNiTransform(const RE::NiTransform& Transform);
}