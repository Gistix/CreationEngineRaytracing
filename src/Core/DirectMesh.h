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

	eastl::string m_Name;

	nvrhi::BufferHandle m_IndexBuffer;
	nvrhi::BufferHandle m_VertexBuffer;

	nvrhi::rt::GeometryDesc m_GeometryDesc;

	nvrhi::rt::AccelStructHandle m_BLAS;
	CESEAdapter::REX::EnumSet<State> m_State = State::None;
public:
	DirectMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	void SetHidden(bool hidden);

	bool GetHidden() const;

	nvrhi::rt::IAccelStruct* GetBLAS() const;
};