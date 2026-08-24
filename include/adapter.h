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

	inline const NiPoint3& NiPoint3_Zero()
	{
		return NiPoint3::ZERO;
	}
}

namespace REL
{
	using RelocationID = ID;
	using VariantOffset = Offset;
}
#endif

// Cross-engine form cast (replaces skyrim_cast)
#if defined(SKYRIM)
#	define ce_cast ::skyrim_cast
#elif defined(FALLOUT4)
template <class To, class From>
To ce_cast(From* a_from)
{
	return reinterpret_cast<To>(a_from);
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
		using FormType = ::RE::FormType;
#elif defined(FALLOUT4)
		using BSVisitControl = ::RE::BSVisitControl;
		using ShaderType = ::RE::BSShaderData::LightingShaderEnum;

		struct FormType
		{
			::RE::ENUM_FORM_ID value{ ::RE::ENUM_FORM_ID::kNONE };

			constexpr FormType() noexcept = default;
			constexpr FormType(::RE::ENUM_FORM_ID a_val) noexcept : value(a_val) {}
			constexpr operator ::RE::ENUM_FORM_ID() const noexcept { return value; }

			static constexpr auto None = ::RE::ENUM_FORM_ID::kNONE;
			static constexpr auto PluginInfo = ::RE::ENUM_FORM_ID::kTES4;
			static constexpr auto FormGroup = ::RE::ENUM_FORM_ID::kGRUP;
			static constexpr auto GameSetting = ::RE::ENUM_FORM_ID::kGMST;
			static constexpr auto Keyword = ::RE::ENUM_FORM_ID::kKYWD;
			static constexpr auto LocationRefType = ::RE::ENUM_FORM_ID::kLCRT;
			static constexpr auto Action = ::RE::ENUM_FORM_ID::kAACT;
			static constexpr auto Transform = ::RE::ENUM_FORM_ID::kTRNS;
			static constexpr auto Component = ::RE::ENUM_FORM_ID::kCMPO;
			static constexpr auto TextureSet = ::RE::ENUM_FORM_ID::kTXST;
			static constexpr auto MenuIcon = ::RE::ENUM_FORM_ID::kMICN;
			static constexpr auto Global = ::RE::ENUM_FORM_ID::kGLOB;
			static constexpr auto DamageType = ::RE::ENUM_FORM_ID::kDMGT;
			static constexpr auto Class = ::RE::ENUM_FORM_ID::kCLAS;
			static constexpr auto Faction = ::RE::ENUM_FORM_ID::kFACT;
			static constexpr auto HeadPart = ::RE::ENUM_FORM_ID::kHDPT;
			static constexpr auto Eyes = ::RE::ENUM_FORM_ID::kEYES;
			static constexpr auto Race = ::RE::ENUM_FORM_ID::kRACE;
			static constexpr auto Sound = ::RE::ENUM_FORM_ID::kSOUN;
			static constexpr auto AcousticSpace = ::RE::ENUM_FORM_ID::kASPC;
			static constexpr auto Skill = ::RE::ENUM_FORM_ID::kSKIL;
			static constexpr auto MagicEffect = ::RE::ENUM_FORM_ID::kMGEF;
			static constexpr auto Script = ::RE::ENUM_FORM_ID::kSCPT;
			static constexpr auto LandTexture = ::RE::ENUM_FORM_ID::kLTEX;
			static constexpr auto Enchantment = ::RE::ENUM_FORM_ID::kENCH;
			static constexpr auto Spell = ::RE::ENUM_FORM_ID::kSPEL;
			static constexpr auto Scroll = ::RE::ENUM_FORM_ID::kSCRL;
			static constexpr auto Activator = ::RE::ENUM_FORM_ID::kACTI;
			static constexpr auto TalkingActivator = ::RE::ENUM_FORM_ID::kTACT;
			static constexpr auto Armor = ::RE::ENUM_FORM_ID::kARMO;
			static constexpr auto Book = ::RE::ENUM_FORM_ID::kBOOK;
			static constexpr auto Container = ::RE::ENUM_FORM_ID::kCONT;
			static constexpr auto Door = ::RE::ENUM_FORM_ID::kDOOR;
			static constexpr auto Ingredient = ::RE::ENUM_FORM_ID::kINGR;
			static constexpr auto Light = ::RE::ENUM_FORM_ID::kLIGH;
			static constexpr auto Misc = ::RE::ENUM_FORM_ID::kMISC;
			static constexpr auto Static = ::RE::ENUM_FORM_ID::kSTAT;
			static constexpr auto StaticCollection = ::RE::ENUM_FORM_ID::kSCOL;
			static constexpr auto MovableStatic = ::RE::ENUM_FORM_ID::kMSTT;
			static constexpr auto Grass = ::RE::ENUM_FORM_ID::kGRAS;
			static constexpr auto Tree = ::RE::ENUM_FORM_ID::kTREE;
			static constexpr auto Flora = ::RE::ENUM_FORM_ID::kFLOR;
			static constexpr auto Furniture = ::RE::ENUM_FORM_ID::kFURN;
			static constexpr auto Weapon = ::RE::ENUM_FORM_ID::kWEAP;
			static constexpr auto Ammo = ::RE::ENUM_FORM_ID::kAMMO;
			static constexpr auto NPC = ::RE::ENUM_FORM_ID::kNPC_;
			static constexpr auto LeveledNPC = ::RE::ENUM_FORM_ID::kLVLN;
			static constexpr auto KeyMaster = ::RE::ENUM_FORM_ID::kKEYM;
			static constexpr auto AlchemyItem = ::RE::ENUM_FORM_ID::kALCH;
			static constexpr auto IdleMarker = ::RE::ENUM_FORM_ID::kIDLM;
			static constexpr auto Note = ::RE::ENUM_FORM_ID::kNOTE;
			static constexpr auto Projectile = ::RE::ENUM_FORM_ID::kPROJ;
			static constexpr auto Hazard = ::RE::ENUM_FORM_ID::kHAZD;
			static constexpr auto BendableSpline = ::RE::ENUM_FORM_ID::kBNDS;
			static constexpr auto SoulGem = ::RE::ENUM_FORM_ID::kSLGM;
			static constexpr auto Terminal = ::RE::ENUM_FORM_ID::kTERM;
			static constexpr auto LeveledItem = ::RE::ENUM_FORM_ID::kLVLI;
			static constexpr auto Weather = ::RE::ENUM_FORM_ID::kWTHR;
			static constexpr auto Climate = ::RE::ENUM_FORM_ID::kCLMT;
			static constexpr auto ShaderParticleGeometryData = ::RE::ENUM_FORM_ID::kSPGD;
			static constexpr auto ReferenceEffect = ::RE::ENUM_FORM_ID::kRFCT;
			static constexpr auto Region = ::RE::ENUM_FORM_ID::kREGN;
			static constexpr auto Navigation = ::RE::ENUM_FORM_ID::kNAVI;
			static constexpr auto Cell = ::RE::ENUM_FORM_ID::kCELL;
			static constexpr auto Reference = ::RE::ENUM_FORM_ID::kREFR;
			static constexpr auto ActorCharacter = ::RE::ENUM_FORM_ID::kACHR;
			static constexpr auto ProjectileMissile = ::RE::ENUM_FORM_ID::kPMIS;
			static constexpr auto ProjectileArrow = ::RE::ENUM_FORM_ID::kPARW;
			static constexpr auto ProjectileGrenade = ::RE::ENUM_FORM_ID::kPGRE;
			static constexpr auto ProjectileBeam = ::RE::ENUM_FORM_ID::kPBEA;
			static constexpr auto ProjectileFlame = ::RE::ENUM_FORM_ID::kPFLA;
			static constexpr auto ProjectileCone = ::RE::ENUM_FORM_ID::kPCON;
			static constexpr auto ProjectileBarrier = ::RE::ENUM_FORM_ID::kPBAR;
			static constexpr auto PlacedHazard = ::RE::ENUM_FORM_ID::kPHZD;
			static constexpr auto WorldSpace = ::RE::ENUM_FORM_ID::kWRLD;
			static constexpr auto Land = ::RE::ENUM_FORM_ID::kLAND;
			static constexpr auto NavMesh = ::RE::ENUM_FORM_ID::kNAVM;
			static constexpr auto TLOD = ::RE::ENUM_FORM_ID::kTLOD;
			static constexpr auto Dialogue = ::RE::ENUM_FORM_ID::kDIAL;
			static constexpr auto Info = ::RE::ENUM_FORM_ID::kINFO;
			static constexpr auto Quest = ::RE::ENUM_FORM_ID::kQUST;
			static constexpr auto Idle = ::RE::ENUM_FORM_ID::kIDLE;
			static constexpr auto Package = ::RE::ENUM_FORM_ID::kPACK;
			static constexpr auto CombatStyle = ::RE::ENUM_FORM_ID::kCSTY;
			static constexpr auto LoadScreen = ::RE::ENUM_FORM_ID::kLSCR;
			static constexpr auto LeveledSpell = ::RE::ENUM_FORM_ID::kLVSP;
			static constexpr auto AnimatedObject = ::RE::ENUM_FORM_ID::kANIO;
			static constexpr auto Water = ::RE::ENUM_FORM_ID::kWATR;
			static constexpr auto EffectShader = ::RE::ENUM_FORM_ID::kEFSH;
			static constexpr auto TOFT = ::RE::ENUM_FORM_ID::kTOFT;
			static constexpr auto Explosion = ::RE::ENUM_FORM_ID::kEXPL;
			static constexpr auto Debris = ::RE::ENUM_FORM_ID::kDEBR;
			static constexpr auto ImageSpace = ::RE::ENUM_FORM_ID::kIMGS;
			static constexpr auto ImageAdapter = ::RE::ENUM_FORM_ID::kIMAD;
			static constexpr auto FormList = ::RE::ENUM_FORM_ID::kFLST;
			static constexpr auto Perk = ::RE::ENUM_FORM_ID::kPERK;
			static constexpr auto BodyPartData = ::RE::ENUM_FORM_ID::kBPTD;
			static constexpr auto AddonNode = ::RE::ENUM_FORM_ID::kADDN;
			static constexpr auto ActorValueInfo = ::RE::ENUM_FORM_ID::kAVIF;
			static constexpr auto CameraShot = ::RE::ENUM_FORM_ID::kCAMS;
			static constexpr auto CameraPath = ::RE::ENUM_FORM_ID::kCPTH;
			static constexpr auto VoiceType = ::RE::ENUM_FORM_ID::kVTYP;
			static constexpr auto MaterialType = ::RE::ENUM_FORM_ID::kMATT;
			static constexpr auto Impact = ::RE::ENUM_FORM_ID::kIPCT;
			static constexpr auto ImpactDataSet = ::RE::ENUM_FORM_ID::kIPDS;
			static constexpr auto Armature = ::RE::ENUM_FORM_ID::kARMA;
			static constexpr auto EncounterZone = ::RE::ENUM_FORM_ID::kECZN;
			static constexpr auto Location = ::RE::ENUM_FORM_ID::kLCTN;
			static constexpr auto Message = ::RE::ENUM_FORM_ID::kMESG;
			static constexpr auto Ragdoll = ::RE::ENUM_FORM_ID::kRGDL;
			static constexpr auto DefaultObject = ::RE::ENUM_FORM_ID::kDOBJ;
			static constexpr auto DefaultObjectForm = ::RE::ENUM_FORM_ID::kDFOB;
			static constexpr auto LightingMaster = ::RE::ENUM_FORM_ID::kLGTM;
			static constexpr auto MusicType = ::RE::ENUM_FORM_ID::kMUSC;
			static constexpr auto Footstep = ::RE::ENUM_FORM_ID::kFSTP;
			static constexpr auto FootstepSet = ::RE::ENUM_FORM_ID::kFSTS;
			static constexpr auto StoryManagerBranchNode = ::RE::ENUM_FORM_ID::kSMBN;
			static constexpr auto StoryManagerQuestNode = ::RE::ENUM_FORM_ID::kSMQN;
			static constexpr auto StoryManagerEventNode = ::RE::ENUM_FORM_ID::kSMEN;
			static constexpr auto DialogueBranch = ::RE::ENUM_FORM_ID::kDLBR;
			static constexpr auto MusicTrack = ::RE::ENUM_FORM_ID::kMUST;
			static constexpr auto DialogueView = ::RE::ENUM_FORM_ID::kDLVW;
			static constexpr auto WordOfPower = ::RE::ENUM_FORM_ID::kWOOP;
			static constexpr auto Shout = ::RE::ENUM_FORM_ID::kSHOU;
			static constexpr auto EquipSlot = ::RE::ENUM_FORM_ID::kEQUP;
			static constexpr auto Relationship = ::RE::ENUM_FORM_ID::kRELA;
			static constexpr auto Scene = ::RE::ENUM_FORM_ID::kSCEN;
			static constexpr auto AssociationType = ::RE::ENUM_FORM_ID::kASTP;
			static constexpr auto Outfit = ::RE::ENUM_FORM_ID::kOTFT;
			static constexpr auto ArtObject = ::RE::ENUM_FORM_ID::kARTO;
			static constexpr auto MaterialObject = ::RE::ENUM_FORM_ID::kMATO;
			static constexpr auto MovementType = ::RE::ENUM_FORM_ID::kMOVT;
			static constexpr auto SoundRecord = ::RE::ENUM_FORM_ID::kSNDR;
			static constexpr auto DualCastData = ::RE::ENUM_FORM_ID::kDUAL;
			static constexpr auto SoundCategory = ::RE::ENUM_FORM_ID::kSNCT;
			static constexpr auto SoundOutputModel = ::RE::ENUM_FORM_ID::kSOPM;
			static constexpr auto CollisionLayer = ::RE::ENUM_FORM_ID::kCOLL;
			static constexpr auto ColorForm = ::RE::ENUM_FORM_ID::kCLFM;
			static constexpr auto ReverbParam = ::RE::ENUM_FORM_ID::kREVB;
			static constexpr auto PackIn = ::RE::ENUM_FORM_ID::kPKIN;
			static constexpr auto ReferenceGroup = ::RE::ENUM_FORM_ID::kRFGP;
			static constexpr auto AimModel = ::RE::ENUM_FORM_ID::kAMDL;
			static constexpr auto Layer = ::RE::ENUM_FORM_ID::kLAYR;
			static constexpr auto ConstructibleObject = ::RE::ENUM_FORM_ID::kCOBJ;
			static constexpr auto Mod = ::RE::ENUM_FORM_ID::kOMOD;
			static constexpr auto MaterialSwap = ::RE::ENUM_FORM_ID::kMSWP;
			static constexpr auto ZoomData = ::RE::ENUM_FORM_ID::kZOOM;
			static constexpr auto InstanceNamingRules = ::RE::ENUM_FORM_ID::kINNR;
			static constexpr auto SoundKeywordMapping = ::RE::ENUM_FORM_ID::kKSSM;
			static constexpr auto AudioEffectChain = ::RE::ENUM_FORM_ID::kAECH;
			static constexpr auto ScaleformColor = ::RE::ENUM_FORM_ID::kSCCO;
			static constexpr auto AttractionRule = ::RE::ENUM_FORM_ID::kAORU;
			static constexpr auto SoundCategorySnapshot = ::RE::ENUM_FORM_ID::kSCSN;
			static constexpr auto SoundTagSet = ::RE::ENUM_FORM_ID::kSTAG;
			static constexpr auto NavigationObstacleManager = ::RE::ENUM_FORM_ID::kNOCM;
			static constexpr auto LensFlare = ::RE::ENUM_FORM_ID::kLENS;
			static constexpr auto LensSprite = ::RE::ENUM_FORM_ID::kLSPR;
			static constexpr auto GodRays = ::RE::ENUM_FORM_ID::kGDRY;
			static constexpr auto ObjectVisibility = ::RE::ENUM_FORM_ID::kOVIS;
			static constexpr auto Total = ::RE::ENUM_FORM_ID::kTotal;
			static constexpr auto Max = ::RE::ENUM_FORM_ID::kTotal;
			static constexpr auto ActiveEffect = ::RE::ENUM_FORM_ID::kActiveEffect;
		};

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

