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
}