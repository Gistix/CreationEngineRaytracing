#include "Scene.h"
#include "Renderer.h"
#include "Util.h"
#include "Plugin.h"

#define DLLEXPORT __declspec(dllexport)

std::list<std::string> errors;

bool Load();

void InitializeLog([[maybe_unused]] spdlog::level::level_enum a_level = spdlog::level::info)
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();

	*path /= std::format("{}.log"sv, Plugin::NAME);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
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

void MessageHandler(SKSE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case SKSE::MessagingInterface::kPostPostLoad:
	{
		if (errors.empty()) {
			Renderer::GetSingleton()->PostPostLoad();
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
			Renderer::GetSingleton()->DataLoaded();
		}

		break;
	}
	}
}

bool Load()
{
	auto messaging = SKSE::GetMessagingInterface();
	messaging->RegisterListener("SKSE", MessageHandler);

	// Creates scene and renderer
	//Scene::GetSingleton();
	auto* renderer = Renderer::GetSingleton();

	auto log = spdlog::default_logger();
	log->set_level(renderer->GetLogLevel());

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
		renderer->Load();
	}

	return true;
}
#endif