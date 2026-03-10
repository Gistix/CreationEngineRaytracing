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

	int32_t GetJitterPhaseCount(int32_t renderWidth, int32_t displayWidth)
	{
		const float basePhaseCount = 8.0f;
		const int32_t jitterPhaseCount = int32_t(basePhaseCount * pow((float(displayWidth) / renderWidth), 2.0f));
		return jitterPhaseCount;
	}

	// Calculate halton number for index and base.
	float Halton(int32_t index, int32_t base)
	{
		float f = 1.0f, result = 0.0f;

		for (int32_t currentIndex = index; currentIndex > 0;) {
			f /= (float)base;
			result = result + f * (float)(currentIndex % base);
			currentIndex = (uint32_t)(floorf((float)(currentIndex) / (float)(base)));
		}

		return result;
	}

	void GetJitterOffset(float* outX, float* outY, int32_t index, int32_t phaseCount)
	{
		const float x = Halton((index % phaseCount) + 1, 2) - 0.5f;
		const float y = Halton((index % phaseCount) + 1, 3) - 0.5f;

		*outX = x;
		*outY = y;
	}
}