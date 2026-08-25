#pragma once

#include "RE/B/BSTriShape.h"
#include "RE/B/BSGraphics.h"
#include "RE/N/NiPoint3.h"
#include "RE/N/NiPoint4.h"
#include "REX/W32/BASE.h"

namespace RE
{
	namespace BSGraphics
	{
		struct StructuredBuffer
		{
			REX::W32::ID3D11Buffer*              buffer;          // 00
			REX::W32::ID3D11ShaderResourceView*  shaderResource;  // 08
			REX::W32::ID3D11UnorderedAccessView* unorderedAccess; // 10
			void*                                unk18;           // 18
			void*                                unk20;           // 20
			void*                                event;           // 28
			std::uint32_t                        refCount;        // 30
			std::uint32_t                        numElements;     // 34
			std::uint32_t                        type;            // 38
		};
	}

	class __declspec(novtable) BSMergeInstancedTriShape :
		public BSTriShape
	{
	public:
		static constexpr auto RTTI{ RTTI::BSMergeInstancedTriShape };
		static constexpr auto VTABLE{ VTABLE::BSMergeInstancedTriShape };
		static constexpr auto Ni_RTTI{ Ni_RTTI::BSMergeInstancedTriShape };

		struct ShapeData
		{
			NiPoint4      transform[3];  // 00 - 3x4 instance rotation matrix
			NiPoint4      bound;         // 30 - instance bounding center (xyz) and radius (w)
			float         materialAlpha; // 40 - material property/alpha (default 1.0)
			std::uint32_t pad44[3];      // 44
		};
		static_assert(sizeof(ShapeData) == 0x50);

		BSMergeInstancedTriShape() { REX::EMPLACE_VTABLE(this); }
		virtual ~BSMergeInstancedTriShape() = default;  // NOLINT(modernize-use-override) 00

		// override (BSTriShape)
		const NiRTTI*             GetRTTI() const override;                           // 02
		NiObject*                 CreateClone(NiCloningProcess& a_cloning) override;  // 1A
		void                      LoadBinary(NiStream& a_stream) override;            // 1B
		void                      LinkObject(NiStream& a_stream) override;            // 1C
		bool                      RegisterStreamables(NiStream& a_stream) override;   // 1D
		void                      SaveBinary(NiStream& a_stream) override;            // 1E
		bool                      IsEqual(NiObject* a_object) override;               // 1F
		void                      OnVisible(NiCullingProcess& a_culling) override;    // 39
		BSMergeInstancedTriShape* IsBSMergeInstancedTriShape() override;              // 3F
		std::uint32_t             GetRenderableTris(std::uint32_t a_lodIndex) override; // 41

		static NiObject* CreateObject();

		// add
		void          SetupShapeDataBuffer(ShapeData* a_data, std::uint32_t a_count);
		void          SetupShapeIdLookupTable(void* a_data, std::uint32_t a_size);
		void          InitVertexDescAndCounts(std::uint64_t a_vertexDesc, std::uint32_t a_instancesPerBatch);
		std::uint32_t GetStartIndex(std::uint32_t a_lodIndex) const;
		std::uint32_t GetNumTriangles(std::uint32_t a_lodIndex) const;

		// members
		BSGraphics::StructuredBuffer* shapeDataBuffer;       // 170 - Structured buffer containing ShapeData
		BSGraphics::Buffer*           shapeIdLookupTable;    // 178 - Byte buffer lookup table
		std::uint32_t                 normalOffset;          // 180 - Vertex element offset for normal
		std::uint32_t                 tangentOffset;         // 184 - Vertex element offset for tangent / binormal
		std::uint32_t                 uvOffset;              // 188 - Vertex element offset for UV
		std::uint32_t                 colorOffset;           // 18C - Vertex element offset for color
		std::uint32_t                 vertexSize;            // 190 - Stored vertex size in bytes
		std::uint32_t                 instancesPerBatch;     // 194 - Number of instances per batch (16 or 32)
		std::uint64_t                 unk198;                // 198
		std::uint32_t                 triCounts[4];          // 1A0 - Triangle / LOD sub-range counts
	};
	static_assert(sizeof(BSMergeInstancedTriShape) == 0x1B0);
}
