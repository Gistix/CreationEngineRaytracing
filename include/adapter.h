#pragma once

#if defined(SKYRIM)
#	define NiRTTI(name) RE::NiRTTI_##name
#elif defined(FALLOUT4)
#	define NiRTTI(name) RE::Ni_RTTI::name
#endif

// Helper to adapt CommonLibF4 to CommonLibSSE-NG
#if defined(FALLOUT4)
namespace RE {
	using FormID = TESFormID;
	using FormType = ENUM_FORM_ID;
}

namespace REL
{
	using RelocationID = ID;
	using VariantOffset = Offset;
}
#endif

namespace CESEAdapter
{
	namespace REX 
	{
#if defined(SKYRIM)
		template<class E, class U = std::underlying_type_t<E>>
		using EnumSet = ::REX::EnumSet<E, U>;
#elif defined(FALLOUT4)
		template<class E, class U = std::underlying_type_t<E>>
		using EnumSet = ::REX::TEnumSet<E, U>;
#endif
	}

	namespace RE
	{
#if defined(SKYRIM)
		using BSVisitControl = ::RE::BSVisit::BSVisitControl;
		using ShaderType = ::RE::BSShader::Type;
		using NiAVObjectFlag = ::RE::NiAVObject::Flag;
#elif defined(FALLOUT4)
		using BSVisitControl = ::RE::BSVisitControl;
		using ShaderType = ::RE::BSShaderData::LightingShaderEnum;

		enum class NiAVObjectFlag
		{
			kNone = 0,
			kHidden = 1 << 0,
			kSelectiveUpdate = 1 << 1,
			kSelectiveUpdateTransforms = 1 << 2,
			kSelectiveUpdateController = 1 << 3,
			kSelectiveUpdateRigid = 1 << 4,
			kDisplayObject = 1 << 5,
			kDisableSorting = 1 << 6,
			kSelectiveUpdateTransformsOverride = 1 << 7,
			kSaveExternalGeometryData = 1 << 9,
			kNoDecals = 1 << 10,
			kAlwaysDraw = 1 << 11,
			kPreProcessedNode = 1 << 12,
			kFixedBound = 1 << 13,
			kTopFadeNode = 1 << 14,
			kIgnoreFade = 1 << 15,
			kNoAnimSyncX = 1 << 16,
			kNoAnimSyncY = 1 << 17,
			kNoAnimSyncZ = 1 << 18,
			kNoAnimSyncS = 1 << 19,
			kNotVisible = 1 << 20,
			kNoDismemberValidity = 1 << 21,
			kRenderUse = 1 << 22,
			kShadowReceiver = 1 << 23,
			kHighDetail = 1 << 24,
			kForceUpdate = 1 << 25,
			kAccumulated = 1 << 26,
			kMeshLOD = 1 << 27,
			kUnk28 = 1 << 28,
			kShadowCaster = 1 << 29
		};
#endif
	}
}
