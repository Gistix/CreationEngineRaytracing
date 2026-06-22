#pragma once

class DirectMesh
{
	enum class State : uint8_t
	{
		None = 0,
		Hidden = 1 << 0,
		Detached = 1 << 1,
		DismemberHidden = 1 << 2,
		SubIndexHidden = 1 << 3,
		Destroyed = 1 << 4
	};

	struct RendererData {
		nvrhi::BufferHandle m_IndexBuffer;
		nvrhi::BufferHandle m_VertexBuffer;
		nvrhi::rt::GeometryDesc m_GeometryDesc;
	};

	eastl::string m_Name;

	eastl::vector<RendererData> m_RendererData;

	nvrhi::rt::AccelStructHandle m_BLAS;
	CESEAdapter::REX::EnumSet<State> m_State = State::None;
public:
	DirectMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	void CreateGeometryDescs(RE::BSTriShape* bsTriShape, RE::BSGraphics::TriShape* triShape);

	void CreateSkinnedGeometryDescs(RE::NiSkinInstance* skinInstance);

	static bool CreateRendererData(uint16_t numTriangles, uint32_t numVertices, RE::BSGraphics::TriShape* triShape, RendererData& rendererData);

	void SetHidden(bool hidden);

	bool IsHidden() const;

	nvrhi::rt::IAccelStruct* GetBLAS() const;
};