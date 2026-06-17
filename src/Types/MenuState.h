#pragma once

enum class MenuState
{
	None = 0,
	MainMenu = 1 << 0,
	LoadingMenu = 1 << 1,
	MapMenu = 1 << 2
};