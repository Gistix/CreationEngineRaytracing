#include "Scene.h"
#include "Renderer.h"
#include "Util.h"
#include "Plugin.h"

#if defined(FALLOUT4)
#	include "REX/W32/SHELL32.h"
#	include "REX/W32/OLE32.h"
#endif

#define DLLEXPORT __declspec(dllexport)

std::list<std::string> errors;

bool Load();

void InitializeLog([[maybe_unused]] spdlog::level::level_enum a_level = spdlog::level::info)
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	std::filesystem::path path;
#ifdef SKYRIM
	path = *logger::log_directory();
#elif defined(FALLOUT4)
	{
		wchar_t* documentsPath = nullptr;
		REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, &documentsPath);
		if (documentsPath) {
			path = documentsPath;
			REX::W32::CoTaskMemFree(documentsPath);
		}
		path /= "My Games/Fallout4/F4SE";
	}
#endif
	path /= std::format("{}.log", Plugin::NAME);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
#endif

#ifndef NDEBUG
	const auto level = spdlog::level::trace;
#else
	const auto level = a_level;
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(level);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
}

#if defined(SKYRIM)
void MessageHandler(SKSE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case SKSE::MessagingInterface::kPostPostLoad:
	{
		if (errors.empty()) {
			Scene::GetSingleton()->PostPostLoad();
		}

		break;
	}
	case SKSE::MessagingInterface::kDataLoaded:
	{
		for (auto it = errors.begin(); it != errors.end(); ++it) {
			auto& errorMessage = *it;
			RE::DebugMessageBox(std::format("Creation Engine Renderer\n{}, will disable all hooks and features", errorMessage).c_str());
		}

		if (errors.empty()) {
			Scene::GetSingleton()->DataLoaded();
		}

		break;
	}
	}
}

bool Load()
{
	auto messaging = SKSE::GetMessagingInterface();
	messaging->RegisterListener("SKSE", MessageHandler);

	auto* scene = Scene::GetSingleton();

	auto log = spdlog::default_logger();
	log->set_level(scene->GetLogLevel());

	const std::array requiredDLLs = {
		L"Data/SKSE/Plugins/EngineFixes.dll",
		L"Data/SKSE/Plugins/CrashLogger.dll",
		L"Data/SKSE/Plugins/CommunityShaders.dll"
	};

	for (const auto dll : requiredDLLs) {
		if (!LoadLibraryW(dll)) {
			auto errorMessage = std::format("Required DLL {} was missing", stl::utf16_to_utf8(dll).value_or("<unicode conversion error>"s));
			logger::error("{}", errorMessage);
			errors.push_back(errorMessage);
		}
	}

	if (errors.empty()) {
		scene->Load();
	}

	return true;
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
#ifndef NDEBUG
	while (!REX::W32::IsDebuggerPresent()) {};
#endif
	InitializeLog();
	logger::info("Loaded {} {}", Plugin::NAME, Plugin::VERSION.string());
	SKSE::Init(a_skse);
	return Load();
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName(Plugin::NAME.data());
	v.PluginVersion(Plugin::VERSION);
	v.UsesAddressLibrary();
	v.UsesNoStructs();
	return v;
	}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
{
	pluginInfo->name = SKSEPlugin_Version.pluginName;
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->version = SKSEPlugin_Version.pluginVersion;
	return true;
}

#elif defined(FALLOUT4)
void MessageHandler(F4SE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case F4SE::MessagingInterface::kPostPostLoad:
	{
		if (errors.empty()) {
			Scene::GetSingleton()->PostPostLoad();
		}

		break;
	}
	case F4SE::MessagingInterface::kGameDataReady:
	{
		for (auto it = errors.begin(); it != errors.end(); ++it) {
			auto& errorMessage = *it;
			logger::error("Creation Engine Renderer\n{}, will disable all hooks and features", errorMessage);
		}

		if (errors.empty()) {
			Scene::GetSingleton()->DataLoaded();
		}

		break;
	}
	}
}

bool Load()
{
	auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(MessageHandler);

	auto* scene = Scene::GetSingleton();

	auto log = spdlog::default_logger();
	log->set_level(scene->GetLogLevel());

	scene->Load();

	return true;
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se, [[maybe_unused]] bool a_isEditor)
{
#ifndef NDEBUG
	while (!REX::W32::IsDebuggerPresent()) {};
#endif
	InitializeLog();
	logger::info("Loaded {} {}", Plugin::NAME, Plugin::VERSION.string());
	F4SE::Init(a_f4se);
	return Load();
}

extern "C" DLLEXPORT constinit auto F4SEPlugin_Version = []() noexcept {
	F4SE::PluginVersionData v{};
	v.PluginName(Plugin::NAME);
	v.PluginVersion(Plugin::VERSION);
	v.AuthorName("Unknown");
	v.UsesAddressLibraryNG(true);
	v.HasNoStructUse(true);
	return v;
	}();

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface*, F4SE::PluginInfo* pluginInfo)
{
	pluginInfo->name = F4SEPlugin_Version.pluginName;
	pluginInfo->infoVersion = F4SE::PluginInfo::kVersion;
	pluginInfo->version = F4SEPlugin_Version.pluginVersion;
	return true;
}
#endif