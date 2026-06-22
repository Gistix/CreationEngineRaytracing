#pragma once

class BaseMesh
{
public:
	enum class State : uint8_t
	{
		None = 0,
		Hidden = 1 << 0,
		Detached = 1 << 1,
		DismemberHidden = 1 << 2,
		SubIndexHidden = 1 << 3,
		Destroyed = 1 << 4
	};

	virtual ~BaseMesh() = default;

	// Constructs the appropriate mesh type (DirectMesh or SkinnedMesh) for the given geometry.
	static eastl::unique_ptr<BaseMesh> Create(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	void SetHidden(bool hidden);

	bool IsHidden() const;

	nvrhi::rt::IAccelStruct* GetBLAS() const;

	virtual void Update([[maybe_unused]] nvrhi::ICommandList* commandList) {}

protected:
	static eastl::string MakeDebugName(RE::BSTriShape* bsTriShape);

	static bool ValidateCounts(uint16_t numTriangles, uint32_t numVertices, RE::BSGraphics::TriShape* triShape);

	static nvrhi::BufferHandle CreateIndexBuffer(RE::BSGraphics::TriShape* triShape);

	static nvrhi::BufferHandle CreateVertexBuffer(RE::BSGraphics::TriShape* triShape);

	static nvrhi::rt::GeometryDesc MakeGeometryDesc(nvrhi::IBuffer* indexBuffer, uint32_t indexCount, nvrhi::IBuffer* vertexBuffer, uint16_t vertexStride, uint32_t vertexCount);

	void BuildBLAS(nvrhi::ICommandList* commandList, const eastl::vector<nvrhi::rt::GeometryDesc>& geometryDescs);

	eastl::string m_Name;

	nvrhi::rt::AccelStructHandle m_BLAS;

	CESEAdapter::REX::EnumSet<State> m_State = State::None;
};
