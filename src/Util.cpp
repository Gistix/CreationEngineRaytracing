#pragma once

#include "Util.h"
#include "Constants.h"

namespace Util
{
	bool IsPlayerFormID(RE::FormID formID)
	{
		return formID == Constants::PLAYER_REFR_FORMID;
	};

	bool IsPlayer(RE::TESForm* form)
	{
		return IsPlayerFormID(form->GetFormID());
	};

	std::string WStringToString(const std::wstring& wideString)
	{
		std::string result;
		std::transform(wideString.begin(), wideString.end(), std::back_inserter(result), [](wchar_t c) {
			return (char)c;
			});
		return result;
	}

	std::wstring StringToWString(const std::string& str)
	{
		if (str.empty())
			return std::wstring();

		int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
			(int)str.size(), nullptr, 0);
		std::wstring wstr(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
			(int)str.size(), &wstr[0], size_needed);
		return wstr;
	}

	float3 Normalize(float3 vector)
	{
		vector.Normalize();
		return vector;
	}

	DirectX::XMMATRIX GetXMFromNiTransform(const RE::NiTransform& Transform)
	{
		DirectX::XMMATRIX temp;

		const RE::NiMatrix3& m = Transform.rotate;
		const float scale = Transform.scale;

		temp.r[0] = DirectX::XMVectorScale(DirectX::XMVectorSet(
			m.entry[0][0],
			m.entry[1][0],
			m.entry[2][0],
			0.0f),
			scale);

		temp.r[1] = DirectX::XMVectorScale(DirectX::XMVectorSet(
			m.entry[0][1],
			m.entry[1][1],
			m.entry[2][1],
			0.0f),
			scale);

		temp.r[2] = DirectX::XMVectorScale(DirectX::XMVectorSet(
			m.entry[0][2],
			m.entry[1][2],
			m.entry[2][2],
			0.0f),
			scale);

		temp.r[3] = DirectX::XMVectorSet(
			Transform.translate.x,
			Transform.translate.y,
			Transform.translate.z,
			1.0f);

		return temp;
	}

	uint2 GetDispatchCount(uint2 resolution, float threads)
	{
		uint dispatchX = static_cast<uint>(std::ceil(resolution.x / threads));
		uint dispatchY = static_cast<uint>(std::ceil(resolution.y / threads));

		return { dispatchX, dispatchY };
	}
}