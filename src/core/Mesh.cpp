#include "Mesh.h"
#include "Util.h"
#include "ubyte4.hlsli"

#include "Scene.h"
#include "Renderer.h"

void Mesh::BuildMesh(RE::BSGraphics::TriShape* rendererData, const uint32_t& vertexCountIn, const uint32_t& triangleCountIn, const uint16_t& bonesPerVertex)
{
	auto vertexDesc = rendererData->vertexDesc;

	vertexFlags = vertexDesc.GetFlags();

	bool hasNormal = vertexFlags & RE::BSGraphics::Vertex::VF_NORMAL;
	bool hasBitangent = vertexFlags & RE::BSGraphics::Vertex::VF_TANGENT;

	// Vertices
	{
		bool dynamic = false;
		bool skinned = flags.any(Flags::Skinned);

		if (flags.any(Flags::Dynamic)) {
			geometry.dynamicPosition.resize(vertexCountIn);

			static REL::Relocation<const RE::NiRTTI*> dynamicTriShapeRTTI{ RE::BSDynamicTriShape::Ni_RTTI };

			if (bsGeometryPtr->GetRTTI() == dynamicTriShapeRTTI.get()) {
				auto* pDynamicTriShape = reinterpret_cast<RE::BSDynamicTriShape*>(bsGeometryPtr);

				if (pDynamicTriShape) {
					auto& dynTriShapeRuntime = pDynamicTriShape->GetDynamicTrishapeRuntimeData();

					dynTriShapeRuntime.lock.Lock();
					std::memcpy(geometry.dynamicPosition.data(), dynTriShapeRuntime.dynamicData, dynTriShapeRuntime.dataSize);
					dynTriShapeRuntime.lock.Unlock();

					dynamic = true;
				}
			}

			// Clear Dynamic flag if geometry is not a valid BSDynamicTriShape.
			// Enforces the invariant that when Flags::Dynamic is set, geometry is always a BSDynamicTriShape.
			if (!dynamic)
				flags.reset(Flags::Dynamic);
		}

		geometry.vertices.resize(vertexCountIn);

		if (skinned)
			geometry.skinning.resize(vertexCountIn);

		auto vertexSize = Util::Geometry::GetSkyrimVertexSize(vertexFlags);
		auto vertexSize2 = Util::Geometry::GetStoredVertexSize(*reinterpret_cast<uint64_t*>(&vertexDesc));

		if (vertexSize != vertexSize2)
			logger::warn("[RT] Mesh::BuildMesh - Vertex size mismatch: {} != {}", vertexSize, vertexSize2);

		bool hasPosition = vertexFlags & RE::BSGraphics::Vertex::VF_VERTEX;

		uint32_t posOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_POSITION);
		uint32_t uvOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
		uint32_t normOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_NORMAL);
		uint32_t tangOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_BINORMAL);
		uint32_t colorOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_COLOR);
		uint32_t skinOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_SKINNING);
		uint32_t landOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_LANDDATA);

		uint32_t boneIDOffset = sizeof(uint16_t) * bonesPerVertex;

		eastl::vector<half> weights;
		eastl::vector<uint8_t> boneIds;

		if (skinned) {
			weights.resize(bonesPerVertex);
			boneIds.resize(bonesPerVertex);
		}

		float3 min(FLT_MAX), max(-FLT_MAX);

		for (uint16_t i = 0; i < vertexCountIn; i++) {
			uint8_t* vtx = rendererData->rawVertexData + i * vertexSize;

			Vertex vertexData{};

			float4 pos;

			if (hasPosition) {
				std::memcpy(&pos, vtx + posOffset, sizeof(float4));
			}
			else if (dynamic) {
				pos = geometry.dynamicPosition[i];
			}

			min = float3::Min(min, float3(pos));
			max = float3::Max(max, float3(pos));

			if (hasPosition || dynamic) {
				vertexData.Position = { pos.x, pos.y, pos.z };
			}

			if (vertexFlags & RE::BSGraphics::Vertex::VF_UV) {
				std::memcpy(&vertexData.Texcoord0, vtx + uvOffset, sizeof(half2));
			}

			if (hasNormal) {
				ubyte4f normalPacked;
				std::memcpy(&normalPacked, vtx + normOffset, sizeof(ubyte4f));
				auto normal = normalPacked.unpack();

				vertexData.Normal = Util::Normalize({ normal.x, normal.y, normal.z });

				if (hasBitangent) {
					ubyte4f bitangentPacked;
					std::memcpy(&bitangentPacked, vtx + tangOffset, sizeof(ubyte4f));
					auto bitangent = bitangentPacked.unpack();

					vertexData.Bitangent = Util::Normalize({ bitangent.x, bitangent.y, bitangent.z });

					float3 tangent = { pos.w, normal.w, bitangent.w };

					if (!hasPosition) {
						tangent.x = std::sqrt(std::max(0.0f, 1.0f - tangent.y * tangent.y - tangent.z * tangent.z));
					}

					vertexData.Handedness = (tangent.x * (vertexData.Bitangent.y * vertexData.Normal.z - vertexData.Bitangent.z * vertexData.Normal.y) +
						tangent.y * (vertexData.Bitangent.z * vertexData.Normal.x - vertexData.Bitangent.x * vertexData.Normal.z) +
						tangent.z * (vertexData.Bitangent.x * vertexData.Normal.y - vertexData.Bitangent.y * vertexData.Normal.x)) < 0 ?
						-1.0f : 1.0f;
				}
			}

			if (skinned) {
				if (vertexFlags & RE::BSGraphics::Vertex::VF_SKINNED) {
					std::memcpy(weights.data(), vtx + skinOffset, sizeof(half) * bonesPerVertex);
					std::memcpy(boneIds.data(), vtx + skinOffset + boneIDOffset, sizeof(uint8_t) * bonesPerVertex);

					float sum = 0.0f;
					for (float w : weights) {
						sum += w;
					}

					if (sum < 1.0f) {
						weights[0] += 1.0f - sum;
					}
					else if (sum > eastl::numeric_limits<float>::epsilon()) {
						float sumRcp = 1.0f / sum;

						for (half& w : weights) {
							w *= sumRcp;
						}
					}
					else {
						weights = { 1.0f };
					}
				}
				else {
					weights = { 1.0f };
					boneIds = { 0 };
				}

				auto fillSkinningData = []<typename T>(eastl::vector<T>&vector) {
					auto currSize = vector.size();

					if (currSize < 4) {
						vector.insert(vector.end(), 4 - currSize, 0);
					}
				};

				fillSkinningData(weights);
				fillSkinningData(boneIds);

				geometry.skinning[i] = Skinning(weights, boneIds);
			}

			if (vertexFlags & RE::BSGraphics::Vertex::VF_LANDDATA) {
				std::memcpy(&vertexData.LandBlend0, vtx + landOffset, sizeof(uint32_t));
				std::memcpy(&vertexData.LandBlend1, vtx + landOffset + sizeof(uint32_t), sizeof(uint32_t));
			}

			if (vertexFlags & RE::BSGraphics::Vertex::VF_COLORS) {
				std::memcpy(&vertexData.Color, vtx + colorOffset, sizeof(uint32_t));
			}
			else {
				vertexData.Color.pack({ 1.0f, 1.0f, 1.0f, 1.0f });
			}

			geometry.vertices[i] = vertexData;
		}

		vertexCount = vertexCountIn;
	}

	// Triangles
	{
		// Landscape contains no triangles, so we build them ourselves
		if (flags.any(Flags::Landscape)) {
			geometry.triangles.reserve(triangleCountIn);

			constexpr uint16_t GRID_SIZE = 16;
			constexpr uint16_t VERTICES = GRID_SIZE + 1;

			for (uint16_t y = 0; y < GRID_SIZE; y++) {
				for (uint16_t x = 0; x < GRID_SIZE; x++) {
					uint16_t v0 = y * VERTICES + x;
					uint16_t v1 = v0 + 1;
					uint16_t v2 = v0 + VERTICES;
					uint16_t v3 = v2 + 1;

					if (v0 >= vertexCount || v1 >= vertexCount || v2 >= vertexCount)
						logger::critical("[RT] Quad {} {} vertex overflow: [{}, {}, {}]", x, y, v0, v1, v2);

					// First triangle
					geometry.triangles.emplace_back(v0, v1, v2);

					// Second triangle
					geometry.triangles.emplace_back(v1, v3, v2);
				}
			}
		}
		else {
			geometry.triangles.resize(triangleCountIn);
			std::memcpy(geometry.triangles.data(), rendererData->rawIndexData, sizeof(Triangle) * triangleCountIn);
		}

		triangleCount = triangleCountIn;
	}

	if (!hasNormal || !hasBitangent) {
		CalculateVectors(!hasNormal);
	}
}

void Mesh::BuildMaterial([[maybe_unused]] const RE::BSGeometry::GEOMETRY_RUNTIME_DATA& geometryRuntimeData, [[maybe_unused]] const char* name, [[maybe_unused]] RE::FormID formID)
{

}

void Mesh::CreateBuffers(nvrhi::ICommandList* commandList, const std::string& name)
{
	auto device = Renderer::GetSingleton()->GetDevice();

	// Vertex Buffer
	{
		const size_t size = sizeof(Vertex) * vertexCount;

		auto vertexBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setIsVertexBuffer(true)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::VertexBuffer)
			.setIsAccelStructBuildInput(true)
			.setDebugName(name + " (Vertex Buffer)");

		buffers.vertexBuffer = device->createBuffer(vertexBufferDesc);

		commandList->writeBuffer(buffers.vertexBuffer.Get(), geometry.vertices.data(), size);
	}

	// Triangle Buffer
	{
		const size_t size = sizeof(Triangle) * triangleCount;

		auto triangleBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setIsIndexBuffer(true)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::IndexBuffer)
			.setIsAccelStructBuildInput(true)
			.setDebugName(name + " (Triangle Buffer)");

		buffers.triangleBuffer = device->createBuffer(triangleBufferDesc);

		commandList->writeBuffer(buffers.triangleBuffer.Get(), geometry.triangles.data(), size);
	}

	// Geometry description
	auto& geometryTriangles = geometryDesc.geometryData.triangles;

	geometryTriangles.indexBuffer = buffers.triangleBuffer;
	geometryTriangles.indexOffset = 0;
	geometryTriangles.indexFormat = nvrhi::Format::R16_UINT;
	geometryTriangles.indexCount = triangleCount * 3;

	geometryTriangles.vertexBuffer = buffers.vertexBuffer;
	geometryTriangles.vertexOffset = 0;
	geometryTriangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
	geometryTriangles.vertexStride = sizeof(Vertex);
	geometryTriangles.vertexCount = vertexCount;
}

bool Mesh::UpdateDynamicPosition()
{
	return false;
}

bool Mesh::UpdateSkinning()
{
	return false;
}

Mesh::UpdateFlags Mesh::Update()
{
	const auto dynamic = flags.any(Mesh::Flags::Dynamic);
	const auto skinned = flags.any(Mesh::Flags::Skinned);

	// I don't know if kHidden is set on inner nodes for culling, so to be safe we check
	if (dynamic || skinned) {
		SetPendingState(State::Hidden, bsGeometryPtr->GetFlags().any(RE::NiAVObject::Flag::kHidden));
	}

	if (IsPendingHidden()) {
		return Mesh::UpdateFlags::None;
	}

	auto updateFlags = Mesh::UpdateFlags::None;

	if (dynamic && UpdateDynamicPosition()) {
		updateFlags |= Mesh::UpdateFlags::Vertices;
	}

	if (skinned && UpdateSkinning()) {
		updateFlags |= Mesh::UpdateFlags::Skinning;
	}

	return updateFlags;
}

bool Mesh::IsHidden() const
{
	return ((state & State::Hidden) != State::None) || ((state & State::DismemberHidden) != State::None);
}

bool Mesh::IsPendingHidden() const
{
	return ((pendingState & State::Hidden) != State::None) || ((pendingState & State::DismemberHidden) != State::None);
}

bool Mesh::IsDirtyState() const
{
	return pendingState != state;
}

// State is set as pending first, final state is updated after BLAS rebuild call
void Mesh::SetPendingState(State stateIn, bool activate)
{
	if (activate)
		pendingState |= stateIn;
	else
		pendingState &= ~stateIn;
}

void Mesh::UpdateDismember(bool enable)
{
	SetPendingState(State::DismemberHidden, !enable);
}

// Updates state from pending
void Mesh::UpdateState()
{
	state = pendingState;
}

void Mesh::CalculateVectors(bool calculateNormal)
{
	eastl::vector<float3> normals;

	if (calculateNormal)
		normals.resize(vertexCount, float3(0, 0, 0));

	eastl::vector<float3> tangents;
	tangents.resize(vertexCount, float3(0, 0, 0));

	// Loop over triangles
	for (auto& t : geometry.triangles) {
		Vertex& v0 = geometry.vertices[t.x];
		Vertex& v1 = geometry.vertices[t.y];
		Vertex& v2 = geometry.vertices[t.z];

		float3 pos0 = v0.Position;
		float3 pos1 = v1.Position;
		float3 pos2 = v2.Position;

		half2 uv0 = v0.Texcoord0;
		half2 uv1 = v1.Texcoord0;
		half2 uv2 = v2.Texcoord0;

		float3 deltaPos1 = pos1 - pos0;
		float3 deltaPos2 = pos2 - pos0;

		// Optionaly compute normals
		if (calculateNormal) {
			float3 faceNormal = deltaPos1.Cross(deltaPos2);

			normals[t.x] += faceNormal;
			normals[t.y] += faceNormal;
			normals[t.z] += faceNormal;
		}

		// Compute UV deltas
		float2 deltaUV1 = uv1 - uv0;
		float2 deltaUV2 = uv2 - uv0;

		float det = deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x;

		if (fabs(det) < 1e-8f)
			continue;

		float r = 1.0f / det;

		float3 tangent = r * (deltaUV2.y * deltaPos1 - deltaUV1.y * deltaPos2);


		// Accumulate per-vertex
		tangents[t.x] += tangent;
		tangents[t.y] += tangent;
		tangents[t.z] += tangent;
	}

	// Normalize and orthogonalize
	for (size_t i = 0; i < vertexCount; i++) {
		auto& v = geometry.vertices[i];

		float3 n = Util::Normalize(calculateNormal ? normals[i] : float3(v.Normal));

		float3 t = Util::Normalize(tangents[i] - n * n.Dot(tangents[i]));

		float3 b = n.Cross(t);
		float sign = (b.Dot(t.Cross(n)) < 0.0f) ? -1.0f : 1.0f;
		b *= sign;

		if (calculateNormal)
			v.Normal = n;

		v.Bitangent = b;

		v.Handedness = sign;
	}
}