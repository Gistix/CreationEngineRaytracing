#pragma once

#include "Utils.h"
#include "Constants.h"

namespace Utils 
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
}