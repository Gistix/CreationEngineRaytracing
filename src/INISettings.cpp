#include "INISettings.h"

void INISettings::Initialize()
{
	auto GetBool = [](RE::Setting* setting) {
#if defined(SKYRIM)
		return setting ? setting->GetBool() : false;
#elif defined(FALLOUT4)
		// FO4 RE::Setting doesn't have GetBool() helper, but it has GetType() == kBinary and GetUChar() reads from the same union.
		return setting ? (setting->GetUChar() != 0) : false;
#endif
	};

	// Display
	enableProjecteUVDiffuseNormals = GetBool(RE::GetINISetting("bEnableProjecteUVDiffuseNormals:Display"));
	enableProjecteUVDiffuseNormalsOnCubemap = GetBool(RE::GetINISetting("bEnableProjecteUVDiffuseNormalsOnCubemap:Display"));
	projectedUVDiffuseNormalTilingScale = RE::GetINISetting("fProjectedUVDiffuseNormalTilingScale:Display") ? RE::GetINISetting("fProjectedUVDiffuseNormalTilingScale:Display")->GetFloat() : 1.0f;
	projectedUVNormalDetailTilingScale = RE::GetINISetting("fProjectedUVNormalDetailTilingScale:Display") ? RE::GetINISetting("fProjectedUVNormalDetailTilingScale:Display")->GetFloat() : 1.0f;
}