#pragma once

#include "Constants.h"

namespace Utils 
{
	bool IsPlayerFormID(RE::FormID formID);

	bool IsPlayer(RE::TESForm* form);

	std::string WStringToString(const std::wstring& wideString);
}