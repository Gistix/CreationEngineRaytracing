#pragma once

#include "RE/N/NiObject.h"
#include "RE/N/NiPointer.h"
#include "RE/N/NiTransform.h"
#include "RE/N/NiBound.h"
#include "REX/W32/BASE.h"

namespace RE
{
	class NiAVObject;
	class BSFlattenedBoneRef;

	namespace BSSkin
	{
		struct BoneTransforms
		{
			NiBound      boundingSphere;  // 00
			NiTransform  transform;       // 10
		};
		static_assert(sizeof(BoneTransforms) == 0x50);

		class __declspec(novtable) BoneData :
			public NiObject
		{
		public:
			static constexpr auto RTTI{ RTTI::BSSkin__BoneData };
			static constexpr auto VTABLE{ VTABLE::BSSkin__BoneData };
			static constexpr auto Ni_RTTI{ Ni_RTTI::BSSkin__BoneData };

			BoneData() { REX::EMPLACE_VTABLE(this); }
			virtual ~BoneData() = default;  // NOLINT(modernize-use-override) 00

			// override (NiObject)
			const NiRTTI* GetRTTI() const override;                           // 02
			NiObject*     CreateClone(NiCloningProcess& a_cloning) override;  // 17
			void          LoadBinary(NiStream& a_stream) override;            // 18
			void          LinkObject(NiStream& a_stream) override;            // 19
			bool          RegisterStreamables(NiStream& a_stream) override;   // 1A
			void          SaveBinary(NiStream& a_stream) override;            // 1B
			bool          IsEqual(NiObject* a_object) override;               // 1C

			static NiObject* CreateObject();

			// members
			BSTArray<BoneTransforms> transforms;  // 10
		};
		static_assert(sizeof(BoneData) == 0x28);

		class __declspec(novtable) Instance :
			public NiObject
		{
		public:
			static constexpr auto RTTI{ RTTI::BSSkin__Instance };
			static constexpr auto VTABLE{ VTABLE::BSSkin__Instance };
			static constexpr auto Ni_RTTI{ Ni_RTTI::BSSkin__Instance };

			Instance() { REX::EMPLACE_VTABLE(this); }
			virtual ~Instance() = default;  // NOLINT(modernize-use-override) 00

			// override (NiObject)
			const NiRTTI* GetRTTI() const override;                           // 02
			NiObject*     CreateClone(NiCloningProcess& a_cloning) override;  // 17
			void          LoadBinary(NiStream& a_stream) override;            // 18
			void          LinkObject(NiStream& a_stream) override;            // 19
			bool          RegisterStreamables(NiStream& a_stream) override;   // 1A
			void          SaveBinary(NiStream& a_stream) override;            // 1B
			bool          IsEqual(NiObject* a_object) override;               // 1C
			void          PostLinkObject(NiStream& a_stream) override;        // 1E

			static Instance* Create();

			// add
			void          UpdateModelBound(NiBound& a_bound);
			Instance*     MakeClone(NiCloningProcess& a_cloning);
			bool          GetFlattenedBoneReference(std::uint32_t a_index, BSFlattenedBoneRef& a_ref);
			void          MakeBonesReal();
			void          GetWorldToSkinTransform(NiTransform& a_transform);
			void          SetScales(const NiPoint3A* a_scales, std::uint32_t a_count);

			// members
			BSTArray<NiAVObject*>	bones;              // 10
			BSTArray<NiTransform*>  worldTransforms;    // 28
			NiPointer<BoneData>		boneData;           // 40
			NiAVObject*				rootParent;         // 48
			NiPoint3A*				boneScales;         // 50
			std::uint32_t			unk58;              // 58
			std::uint32_t			unk5C;              // 5C
			NiTransform				unkTransform;       // 60
			std::uint64_t			unkA0;              // A0
			std::uint64_t			unkA8;              // A8
			std::uint64_t			unkB0;              // B0
			std::uint32_t			unkB8;              // B8
			std::uint32_t			frameID;            // BC
		};
		static_assert(sizeof(Instance) == 0xC0);
	}
}
