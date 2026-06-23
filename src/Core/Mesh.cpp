#include "Mesh.h"
#include "Util.h"
#include "byte4.hlsli"

#include "Scene.h"
#include "Renderer.h"
#include "SceneGraph.h"

#include "Utils/CalcTangents.h"
#include "Core/D3D12Texture.h"

Mesh::VertexData Mesh::BuildVertices(CESEAdapter::REX::EnumSet<Flags>& flags, [[maybe_unused]] RE::BSGeometry* geometry, RE::BSGraphics::TriShape* rendererData, const uint32_t& vertexCountIn, const uint16_t& bonesPerVertex)
{
	VertexData vertexData{};

	auto vertexDesc = rendererData->vertexDesc;

	auto vertexFlags = vertexDesc.GetFlags();

	bool hasNormal = vertexFlags & RE::BSGraphics::Vertex::VF_NORMAL;
	bool hasTangent = vertexFlags & RE::BSGraphics::Vertex::VF_TANGENT;

	bool dynamic = false;
	bool skinned = flags.all(Flags::Skinned);

	if (flags.all(Flags::Dynamic)) {
		vertexData.dynamicPosition.resize(vertexCountIn);

#if defined(SKYRIM)
		static REL::Relocation<const RE::NiRTTI*> dynamicTriShapeRTTI{ NiRTTI(BSDynamicTriShape) };

		if (geometry->GetRTTI() == dynamicTriShapeRTTI.get()) {
			auto* pDynamicTriShape = reinterpret_cast<RE::BSDynamicTriShape*>(geometry);

			if (pDynamicTriShape) {
				auto& dynTriShapeRuntime = pDynamicTriShape->GetDynamicTrishapeRuntimeData();

				dynTriShapeRuntime.lock.Lock();
				std::memcpy(vertexData.dynamicPosition.data(), dynTriShapeRuntime.dynamicData, dynTriShapeRuntime.dataSize);
				dynTriShapeRuntime.lock.Unlock();

				dynamic = true;
			}
		}
#endif

		// Clear Dynamic flag if geometry is not a valid BSDynamicTriShape.
		// Enforces the invariant that when Flags::Dynamic is set, geometry is always a BSDynamicTriShape.
		if (!dynamic)
			flags.reset(Flags::Dynamic);
	}

	vertexData.vertices.resize(vertexCountIn);

	if (skinned)
		vertexData.skinning.resize(vertexCountIn);

	if (skinned || dynamic)
		vertexData.position.resize(vertexCountIn);

	auto vertexSize = Util::Geometry::GetSkyrimVertexSize(vertexFlags);
	auto vertexSize2 = Util::Geometry::GetStoredVertexSize(vertexDesc);

	if (vertexSize != vertexSize2)
		logger::warn("Mesh::BuildVertices - Vertex size mismatch: {} != {}", vertexSize, vertexSize2);

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

	for (uint32_t i = 0; i < vertexCountIn; i++) {
		uint8_t* vtx = Util::Adapter::GetVertexData(rendererData) + i * vertexSize;

		Vertex vertex{};

		float4 pos;

		if (hasPosition) {
			std::memcpy(&pos, vtx + posOffset, sizeof(float4));
		}
		else if (dynamic) {
			pos = vertexData.dynamicPosition[i];
		}

		if (hasPosition || dynamic) {
			const float3 position = { pos.x, pos.y, pos.z };
			vertex.Position = position;

			// Used to populate 'prevPositionBuffer' for motion vectors
			if (skinned || dynamic) {
				vertexData.position[i] = vertex.Position;
			}
		}

		if (vertexFlags & RE::BSGraphics::Vertex::VF_UV) {
			std::memcpy(&vertex.Texcoord0, vtx + uvOffset, sizeof(half2));
		}

		if (hasNormal) {
			byte4f normalPacked;
			std::memcpy(&normalPacked, vtx + normOffset, sizeof(byte4f));
			auto normal = normalPacked.unpack();

			float3 N = Util::Math::Normalize({ normal.x, normal.y, normal.z });
			vertex.Normal = N;

			if (hasTangent) {
				byte4f bitangentPacked;
				std::memcpy(&bitangentPacked, vtx + tangOffset, sizeof(byte4f));
				auto bitangent = bitangentPacked.unpack();

				float3 B = { bitangent.x, bitangent.y, bitangent.z };
				B = Util::Math::Normalize(B - N * N.Dot(B));

				float3 T = { pos.w, normal.w, bitangent.w };

				// Dynamic TriShapes (Blendshape/Morphtarget) do not have vertex position
				if (!hasPosition) {
					float sign = B.Cross(N).x < 0 ? -1.0f : 1.0f;
					T.x = std::sqrt(std::max(0.0f, 1.0f - bitangent.y * bitangent.y - bitangent.z * bitangent.z)) * sign;
				}

				T = Util::Math::Normalize(T - N * N.Dot(T));

				vertex.Tangent = Util::Math::Normalize(T);

				vertex.Handedness = -(N.Cross(T).Dot(B) < 0 ? -1.0f : 1.0f);
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

			vertexData.skinning[i] = Skinning(weights, boneIds);
		}

		if (vertexFlags & RE::BSGraphics::Vertex::VF_LANDDATA) {
			std::memcpy(&vertex.LandBlend0, vtx + landOffset, sizeof(uint32_t));
			std::memcpy(&vertex.LandBlend1, vtx + landOffset + sizeof(uint32_t), sizeof(uint32_t));
		}

		if (vertexFlags & RE::BSGraphics::Vertex::VF_COLORS) {
			std::memcpy(&vertex.Color, vtx + colorOffset, sizeof(uint32_t));
		}
		else {
			vertex.Color.pack({ 1.0f, 1.0f, 1.0f, 1.0f });
		}

		vertexData.vertices[i] = vertex;
	}

	vertexData.count = vertexCountIn;

	return vertexData;
}

Mesh::TriangleData Mesh::BuildTriangles(Mesh::Flags flags, RE::BSGraphics::TriShape* rendererData, const uint32_t& triangleCountIn)
{
	TriangleData triangleData{};

	// Landscape contains no triangles, so we copy from a pre-built vector
	if ((flags & Flags::Landscape) != Flags::None) {
		triangleData.triangles = GetLandscapeTriangles();
	}
	else {
		triangleData.triangles.resize(triangleCountIn);
		std::memcpy(triangleData.triangles.data(), Util::Adapter::GetIndexData(rendererData), sizeof(Triangle) * triangleCountIn);
	}

	triangleData.count = triangleCountIn;

	return triangleData;
}

void Mesh::ClearUnusedVertices()
{
	eastl::vector<uint16_t> vertices;
	vertices.reserve(triangleData.count * 3);

	for (const auto& tri: triangleData.triangles)
	{
		vertices.push_back(tri.x);
		vertices.push_back(tri.y);
		vertices.push_back(tri.z);
	}

	eastl::sort(vertices.begin(), vertices.end());
	vertices.erase(eastl::unique(vertices.begin(), vertices.end()), vertices.end());

	auto vertexCount = static_cast<uint32_t>(vertices.size());

	if (vertexCount == vertexData.count)
		return;

	// Remaps old vertices -> new
	vertexData.remap.resize(vertexData.count);

	// Rebuild vertices and remap vector
	auto cleanVertexData = VertexData{};

	bool dynamic = flags.all(Flags::Dynamic);

	cleanVertexData.count = vertexCount;

	if (dynamic) {
		cleanVertexData.dynamicPosition = vertexData.dynamicPosition;
		cleanVertexData.dynamicPositionRemapped.resize(vertexCount);
	}

	cleanVertexData.vertices.resize(vertexCount);
	cleanVertexData.skinning.resize(vertexCount);
	cleanVertexData.remap.resize(vertexCount);

	uint16_t i = 0;
	for (const auto& v : vertices)
	{
		if (dynamic)
			cleanVertexData.dynamicPositionRemapped[i] = vertexData.dynamicPosition[v];
	
		cleanVertexData.vertices[i] = vertexData.vertices[v];
		cleanVertexData.skinning[i] = vertexData.skinning[v];

		// New -> Old
		cleanVertexData.remap[i] = v;

		// Old -> new
		vertexData.remap[v] = i;

		i++;
	}

	// Remap Triangles
	auto& remap = vertexData.remap;

	for (auto& triangle: triangleData.triangles)
	{
		triangle.x = remap[triangle.x];
		triangle.y = remap[triangle.y];
		triangle.z = remap[triangle.z];
	}

	vertexData = cleanVertexData;

	flags.set(Flags::Remapped);
}

void Mesh::BuildMesh(RE::BSGraphics::TriShape* rendererData, const uint32_t& vertexCountIn, const uint32_t& triangleCountIn, const uint16_t& bonesPerVertex)
{
	vertexData = BuildVertices(flags, bsGeometryPtr.get(), rendererData, vertexCountIn, bonesPerVertex);
	triangleData = BuildTriangles(flags.get(), rendererData, triangleCountIn);

	// Clear unused partition vertices
	/*if (flags.all(Mesh::Flags::Skinned))
		ClearUnusedVertices();

	if (flags.none(Flags::Landscape, Flags::Water) && Util::Geometry::HasDoubleSidedGeom(this))
		flags.set(Mesh::Flags::DoubleSidedGeom);*/

	auto vertexDesc = rendererData->vertexDesc;

	bool hasNormal = vertexDesc.GetFlags() & RE::BSGraphics::Vertex::VF_NORMAL;
	bool hasTangent = vertexDesc.GetFlags() & RE::BSGraphics::Vertex::VF_TANGENT;

	if (!hasNormal)
		CalculateNormals();

	if (!hasTangent)
		Util::CalcTangents(this);
}

void Mesh::BuildMesh(VertexData a_VertexData, TriangleData a_TriangleData, RE::BSGraphics::VertexDesc vertexDesc)
{
	vertexData = a_VertexData;
	triangleData = a_TriangleData;

	// Clear unused partition vertices
	/*if (flags.all(Mesh::Flags::Skinned))
		ClearUnusedVertices();

	if (flags.none(Flags::Landscape, Flags::Water) && Util::Geometry::HasDoubleSidedGeom(this))
		flags.set(Mesh::Flags::DoubleSidedGeom);*/

	bool hasNormal = vertexDesc.GetFlags() & RE::BSGraphics::Vertex::VF_NORMAL;
	bool hasTangent = vertexDesc.GetFlags() & RE::BSGraphics::Vertex::VF_TANGENT;

	if (!hasNormal)
		CalculateNormals();

	if (!hasTangent)
		Util::CalcTangents(this);
}

eastl::vector<Triangle> Mesh::GetLandscapeTriangles()
{
	static const eastl::vector<Triangle> triangles = [] {
		eastl::vector<Triangle> t;

		constexpr uint16_t GRID_SIZE = 16;
		constexpr uint16_t VERTICES = GRID_SIZE + 1;

		t.reserve(GRID_SIZE * GRID_SIZE * 2);

		for (uint16_t y = 0; y < GRID_SIZE; y++) {
			for (uint16_t x = 0; x < GRID_SIZE; x++) {
				uint16_t v0 = y * VERTICES + x;
				uint16_t v1 = v0 + 1;
				uint16_t v2 = v0 + VERTICES;
				uint16_t v3 = v2 + 1;

				if ((y ^ x) & 1) {
					t.emplace_back(v2, v0, v1);  // BL, TL, TR
					t.emplace_back(v1, v3, v2);  // TR, BR, BL
				}
				else {
					t.emplace_back(v3, v2, v0);  // BR, BL, TL
					t.emplace_back(v0, v1, v3);  // TL, TR, BR
				}
			}
		}

		return t;
	}();

	return triangles;
}

void Mesh::BuildMaterial(const GeometryRuntimeData& runtimeData, RE::FormID formID)
{
	material = eastl::make_unique<Material>(m_Name, runtimeData, formID);

	// Attempt to clear up fake positives
	if (material->shaderFlags.all(RE::BSShaderProperty::EShaderPropertyFlag::kTwoSided))
		flags.reset(Mesh::Flags::DoubleSidedGeom);

	geometryDesc.flags = (material->alphaFlags == Material::AlphaFlags::None) ? nvrhi::rt::GeometryFlags::Opaque : nvrhi::rt::GeometryFlags::None;
}

eastl::unique_ptr<Mesh> Mesh::Clone(RE::NiAVObject* rootNode, RE::FormID formID) const
{
	RE::BSGeometry* foundGeom = nullptr;

	Util::Traversal::ScenegraphRTGeometries(rootNode, nullptr, [&](RE::BSGeometry* pGeometry) -> CESEAdapter::RE::BSVisitControl {
		if (eastl::string(pGeometry->name.c_str()) == m_Name) {
			foundGeom = pGeometry;
			return CESEAdapter::RE::BSVisitControl::kStop;
		}
		return CESEAdapter::RE::BSVisitControl::kContinue;
	});

	if (!foundGeom)
		return nullptr;

	auto rootWorldInverse = rootNode->world.Invert();
	const bool isRootOrigin = rootNode->world.translate == Util::Adapter::GetNiPoint3Zero();
	const bool isOrigin = foundGeom->world.translate == Util::Adapter::GetNiPoint3Zero();

	auto cloneFlags = flags;
	cloneFlags.reset(Mesh::Flags::Origin);

	float3x4 localToRoot;
	if (!isOrigin || (isOrigin && isRootOrigin)) {
		localToRoot = Util::Math::ComputeLocalToRoot(rootWorldInverse, foundGeom->world);
	} else {
		localToRoot = float3x4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f);
		cloneFlags.set(Mesh::Flags::Origin);
	}

	auto clone = eastl::make_unique<Mesh>(m_FormType, m_Type, cloneFlags.get(), m_Name.c_str(), foundGeom, localToRoot, m_Identifier);

	clone->vertexData = vertexData;
	clone->triangleData = triangleData;
	clone->vertexFlags = vertexFlags;
	clone->m_BoneMatrices = m_BoneMatrices;
	clone->m_PrevLocalToRoot = localToRoot;

	const auto& runtimeData = Util::Adapter::GetGeometryRuntimeData(foundGeom);
	clone->BuildMaterial(runtimeData, formID);

	clone->m_FrameID = 0;
	clone->m_State = State::None;

	return clone;
}

bool Mesh::Updatable() const
{
	return flags.any(Flags::Dynamic, Flags::Skinned) || m_Type == Type::LandLOD;
}

void Mesh::CreateBuffers(SceneGraph* sceneGraph, nvrhi::ICommandList* commandList)
{
	auto device = Renderer::GetSingleton()->GetDevice();

	bool updatable = Updatable();

	logger::debug("Mesh::CreateBuffers - {}", m_Name);

	// Triangle Buffer
	{
		const size_t size = sizeof(Triangle) * triangleData.count;

		logger::debug("Mesh::CreateBuffers - Triangle Count: {}, Buffer Size: {}", triangleData.count, size);

		auto& triangleBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(Triangle))
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setIsAccelStructBuildInput(true)
			.setDebugName(std::format("{} (Triangle Buffer)", m_Name.c_str()));

		buffers.triangleBuffer = device->createBuffer(triangleBufferDesc);

		commandList->writeBuffer(buffers.triangleBuffer, triangleData.triangles.data(), size);

		{
			// Create SRV binding for triangles
			auto triangleBindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(0, buffers.triangleBuffer);
			// Register descriptor, get handle within heap and writes the SRV
			m_DescriptorHandle = sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->CreateDescriptorHandle(triangleBindingSet);
		}
	}

	const auto descriptorIndex = m_DescriptorHandle.Get();

	if (flags.all(Flags::Dynamic)) {
		const size_t size = sizeof(float4) * vertexData.count;

		logger::debug("Mesh::CreateBuffers - Dynamic Buffer Size: {}", size);

		auto& dynamicBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(float4))
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setDebugName(std::format("{} (Dynamic Position Buffer)", m_Name.c_str()));

		buffers.dynamicPositionBuffer = device->createBuffer(dynamicBufferDesc);

		UpdateUploadDynamicBuffers(commandList);

		{
			auto bindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffers.dynamicPositionBuffer);
			device->writeDescriptorTable(sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(), bindingSet);
		}

	}

	// Vertex Buffer
	{
		const size_t size = sizeof(Vertex) * vertexData.count;

		logger::debug("Mesh::CreateBuffers - Vertex Count: {}, Buffer Size: {}", vertexData.count, size);

		auto& vertexBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(Vertex))
			.setCanHaveUAVs(updatable)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setIsAccelStructBuildInput(true)
			.setDebugName(std::format("{} (Vertex Buffer)", m_Name.c_str()));

		buffers.vertexBuffer = device->createBuffer(vertexBufferDesc);

		commandList->writeBuffer(buffers.vertexBuffer, vertexData.vertices.data(), size);

		auto vertexBindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffers.vertexBuffer);
		//device->writeDescriptorTable(sceneGraph->GetVertexDescriptors()->m_DescriptorTable, vertexBindingSet);

		if (updatable) {
			auto uavBindingSet = nvrhi::BindingSetItem::StructuredBuffer_UAV(descriptorIndex, buffers.vertexBuffer);
			device->writeDescriptorTable(sceneGraph->GetVertexWriteDescriptors()->m_DescriptorTable, uavBindingSet);
		}
	}

	// Vertex Copy
	if (updatable) {
		const size_t size = sizeof(Vertex) * vertexData.count;

		auto& vertexCopyBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(Vertex))
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setDebugName(std::format("{} (Vertex Copy Buffer)", m_Name.c_str()));

		buffers.vertexCopyBuffer = device->createBuffer(vertexCopyBufferDesc);

		commandList->copyBuffer(buffers.vertexCopyBuffer, 0, buffers.vertexBuffer, 0, size);

		auto bindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffers.vertexCopyBuffer);
		device->writeDescriptorTable(sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable, bindingSet);
	}

	if (flags.all(Flags::Skinned)) {
		const size_t size = sizeof(Skinning) * vertexData.count;

		auto& skinningBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setStructStride(sizeof(Skinning))
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setDebugName(std::format("{} (Skinning Buffer)", m_Name.c_str()));

		buffers.skinningBuffer = device->createBuffer(skinningBufferDesc);

		commandList->writeBuffer(buffers.skinningBuffer, vertexData.skinning.data(), size);

		auto bindingSet = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffers.skinningBuffer);
		device->writeDescriptorTable(sceneGraph->GetSkinningDescriptors()->m_DescriptorTable, bindingSet);
	}

	// Previous position buffer for per-vertex motion vectors
	if (flags.any(Flags::Skinned, Flags::Dynamic)) {
		const size_t prevPosSize = sizeof(float3) * vertexData.count;

		auto& prevPositionBufferDesc = nvrhi::BufferDesc()
			.setByteSize(prevPosSize)
			.setStructStride(sizeof(float3))
			.setCanHaveUAVs(true)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::Common)
			.setDebugName(std::format("{} (Prev Position Buffer)", m_Name.c_str()));

		buffers.prevPositionBuffer = device->createBuffer(prevPositionBufferDesc);

		commandList->writeBuffer(buffers.prevPositionBuffer, vertexData.position.data(), prevPosSize);
		
		auto prevPosSrvBinding = nvrhi::BindingSetItem::StructuredBuffer_SRV(descriptorIndex, buffers.prevPositionBuffer);
		device->writeDescriptorTable(sceneGraph->GetPrevPositionDescriptors()->m_DescriptorTable, prevPosSrvBinding);

		auto prevPosUavBinding = nvrhi::BindingSetItem::StructuredBuffer_UAV(descriptorIndex, buffers.prevPositionBuffer);
		device->writeDescriptorTable(sceneGraph->GetPrevPositionWriteDescriptors()->m_DescriptorTable, prevPosUavBinding);
	}

	// Material Buffer
	material->CreateBuffer(m_Name, descriptorIndex);

	// Geometry description
	auto& geometryTriangles = geometryDesc.geometryData.triangles;

	geometryTriangles.indexBuffer = buffers.triangleBuffer;
	geometryTriangles.indexOffset = 0;
	geometryTriangles.indexFormat = nvrhi::Format::R16_UINT;
	geometryTriangles.indexCount = triangleData.count * 3;

	geometryTriangles.vertexBuffer = buffers.vertexBuffer;
	geometryTriangles.vertexOffset = 0;
	geometryTriangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
	geometryTriangles.vertexStride = sizeof(Vertex);
	geometryTriangles.vertexCount = vertexData.count;

	geometryDesc.setTransform(m_LocalToRoot.f);
}

bool Mesh::UpdateDynamicPosition()
{
#if defined(SKYRIM)
	auto* dynamicTriShape = reinterpret_cast<RE::BSDynamicTriShape*>(bsGeometryPtr.get());
	auto& runtimeData = dynamicTriShape->GetDynamicTrishapeRuntimeData();

	if (!runtimeData.dynamicData)
		return false;

	auto& dataSize = runtimeData.dataSize;

	// Is this even a possibility?
	if (dataSize == 0)
		return false;

	runtimeData.lock.Lock();

	// Has dynamic position changed?
	if (std::memcmp(vertexData.dynamicPosition.data(), runtimeData.dynamicData, dataSize) == 0) {
		runtimeData.lock.Unlock();
		return false;
	}

	std::memcpy(vertexData.dynamicPosition.data(), runtimeData.dynamicData, dataSize);
	runtimeData.lock.Unlock();

	return true;
#else
	return false;
#endif
}

void Mesh::UpdateUploadDynamicBuffers(nvrhi::ICommandList* commandList)
{
	if (flags.none(Flags::Dynamic))
		return;
	
	if (flags.all(Flags::Remapped)) {
		// Remap original vertices to remapped partition vertices
		for (size_t i = 0; i < vertexData.count; i++)
		{
			const auto& v = vertexData.remap[i];
			vertexData.dynamicPositionRemapped[i] = vertexData.dynamicPosition[v];
		}

		commandList->writeBuffer(buffers.dynamicPositionBuffer, vertexData.dynamicPositionRemapped.data(), sizeof(float4) * vertexData.count);
	}
	else
		commandList->writeBuffer(buffers.dynamicPositionBuffer, vertexData.dynamicPosition.data(), sizeof(float4) * vertexData.count);
}

bool Mesh::UpdateSkinning([[maybe_unused]] bool isPlayer)
{
	// Update Bone matrices
	auto* skinInstance = Util::Adapter::GetSkinInstance(bsGeometryPtr.get());

	// RaceMenu crash fix
	if (!skinInstance)
		return false;

#if defined(SKYRIM)
	auto* scene = Scene::GetSingleton();
	const bool isPathTracing = scene->IsPathTracingActive();
	const bool isForceCulled = isPathTracing || (isPathTracing && (material->feature == RE::BSShaderMaterial::Feature::kEnvironmentMap || material->feature == RE::BSShaderMaterial::Feature::kEye));
	
	bool isVisible = false;
	if (isForceCulled) {	
		isVisible = scene->GetSceneGraph()->GetCamera()->NodeInFrustum(bsGeometryPtr.get());
	}

	const auto frameID = skinInstance->frameID;

	if (!isVisible && frameID == Constants::INVALID_FRAME_ID)
		return false;

	auto* rootParent = skinInstance->rootParent;

	// UBE crash fix
	if (!rootParent)
		return false;

	// Mostly for COtR, head geometry becomes unparented after the first few frames
	const bool unparentedPlayer = isPlayer && !rootParent->parent;

	// Only update if the game has updated the animation
	// Part 1 of COtR fix
	if (!isVisible && !unparentedPlayer && m_FrameID == frameID)
		return false;

	m_FrameID = frameID;

	auto* skinData = skinInstance->skinData.get();

	if (!skinData)
		return false;

	if (!isForceCulled && skinInstance->numMatrices != skinData->bones)
		logger::warn("Mesh::UpdateSkinning - Num Matrices: {}, Num Bones: {}", skinInstance->numMatrices, skinData->bones);

	if (skinData->bones == 0)
		return false;

	auto geometryWorldInverse = bsGeometryPtr->world.Invert();

	if (m_BoneMatrices.empty() || skinData->bones != m_BoneMatrices.size())
		m_BoneMatrices.resize(skinData->bones);

	for (uint i = 0; i < skinData->bones; i++) {
		auto boneWorld = *skinInstance->boneWorldTransforms[i];
		auto boneMatrix = boneWorld * skinData->boneData[i].skinToBone;
		XMStoreFloat3x4(&m_BoneMatrices[i], Util::Math::GetXMFromNiTransform(geometryWorldInverse * boneMatrix));
	}

	return true;
#else
	return false;
#endif
}

bool Mesh::UpdateTransform(RE::NiAVObject* object)
{
	if (flags.any(Flags::Origin))
		return false;

	// Update previous transform
	m_PrevLocalToRoot = m_LocalToRoot;

	float3x4 localToRoot;
	XMStoreFloat3x4(&localToRoot, Util::Math::GetXMFromNiTransform(object->world.Invert() * bsGeometryPtr->world));

	if (Util::Math::MatrixNearEqual(localToRoot, m_LocalToRoot))
		return false;

	// Update transform
	m_LocalToRoot = localToRoot;

	geometryDesc.setTransform(m_LocalToRoot.f);

	return true;
}

bool Mesh::GetDismemberHidden() const
{
	auto* skinInstance = Util::Adapter::GetSkinInstance(bsGeometryPtr.get());
	if (!skinInstance)
		return false;

#if defined(SKYRIM)
	static REL::Relocation<const RE::NiRTTI*> dismemberRTTI{ RE::BSDismemberSkinInstance::Ni_RTTI };
	if (skinInstance->GetRTTI() != dismemberRTTI.get())
		return false;

	auto& dismemberRuntime = reinterpret_cast<RE::BSDismemberSkinInstance*>(skinInstance)->GetRuntimeData();
	if (dismemberRuntime.numPartitions == 0)
		return false;

	auto& partition = dismemberRuntime.partitions[m_Identifier];

	return !partition.editorVisible;
#else
	return false;
#endif
}

bool Mesh::GetSubIndexHidden() const
{
	if (*Scene::GetSingleton()->g_BypassSubIndexVisibility)
		return false;

	auto* subIndex = Util::Adapter::AsSubIndexTriShape(bsGeometryPtr.get());

	if (!subIndex)
		return false;

	const auto startTri = static_cast<uint32_t>(m_Identifier >> 16);
	const auto numTris = static_cast<uint32_t>(m_Identifier & 0xFFFF);
	const auto endTri = startTri + numTris;

	auto& runtimeData = subIndex->GetSubIndexedTrishapeRuntimeData();
	for (size_t i = 0; i < runtimeData.numSegments; i++)
	{
		const auto& segment = runtimeData.segmentData[i];

		const bool visible = (segment.flags == 1u);

		if (!visible)
			continue;

		// Index to Triangle
		const auto segStartTri = segment.index / 3;
		const auto segEndTri = segStartTri + segment.numTris;

		if (startTri >= segStartTri && endTri <= segEndTri)
			return false;
	}

	return true;
}

Mesh::State Mesh::GetState(RE::NiAVObject* instanceRoot, Flags modelFlags) const
{
	const auto dynamic = flags.all(Mesh::Flags::Dynamic);
	const auto skinned = flags.all(Mesh::Flags::Skinned);
	const auto subIndexed = flags.all(Mesh::Flags::SubIndex);
	const auto water = flags.all(Mesh::Flags::Water);

	const bool dynamicModel = (modelFlags & Mesh::Flags::Dynamic) != Mesh::Flags::None;
	const bool skinnedModel = (modelFlags & Mesh::Flags::Skinned) != Mesh::Flags::None;

	State state = State::None;

	// Not all meshes can be hidden due to instancing
	if (dynamic || skinned || subIndexed || water || dynamicModel || skinnedModel)
		if (Util::Game::IsHidden(bsGeometryPtr.get(), instanceRoot))
			state |= State::Hidden;

	if (bsGeometryPtr->GetGeometryRuntimeData().shaderProperty->alpha <= std::numeric_limits<float>::epsilon())
		state |= State::Hidden;

	if (skinned && GetDismemberHidden())
		state |= State::DismemberHidden;

	if (subIndexed && GetSubIndexHidden())
		state |= State::SubIndexHidden;

	return state;
}

void Mesh::InitState(RE::NiAVObject* instanceRoot, Flags modelFlags)
{
	m_State = GetState(instanceRoot, modelFlags);
}

DirtyFlags Mesh::Update(RE::NiAVObject* instanceRoot, bool isPlayer, Flags modelFlags)
{
#if defined(SKYRIM)
	// This should never be true, but it often is, meaning we missed some logic that removes this mesh or the entire instance
	if (bsGeometryPtr->GetRefCount() == 1) {
		logger::trace("Mesh::Update - Released BSGeometry being referenced in 0x{:08X} {}", reinterpret_cast<uintptr_t>(bsGeometryPtr.get()), m_Name);
		return DirtyFlags::None;
	}
#endif

	material->Update(Util::Adapter::GetGeometryRuntimeData(bsGeometryPtr.get()).shaderProperty);

	const auto dynamic = flags.all(Mesh::Flags::Dynamic);
	const auto skinned = flags.all(Mesh::Flags::Skinned);

	const bool skinnedModel = (modelFlags & Flags::Skinned) != Flags::None;

	const bool updateTransform = skinnedModel || (m_Type == Type::LandLOD) || (m_Type == Type::ObjectLOD);

	// Store previous hidden state
	bool wasHidden = IsHidden();

	// Update state
	m_State = GetState(instanceRoot, modelFlags);

	// Current hidden state
	bool isHidden = IsHidden();

	// Becomes hidden this frame
	if (!wasHidden && isHidden)
		return DirtyFlags::Visibility;

	// Nothing to update
	if (wasHidden && isHidden)
		return DirtyFlags::None;

	auto updateFlags = DirtyFlags::None;

	// Becomes visible this frame
	if (wasHidden && !isHidden)
		updateFlags |= DirtyFlags::Visibility;

	if (dynamic && UpdateDynamicPosition())
		updateFlags |= DirtyFlags::Vertex;

	if (skinned && UpdateSkinning(isPlayer))
		updateFlags |= DirtyFlags::Skin;

	if (updateTransform && UpdateTransform(instanceRoot))
		updateFlags |= DirtyFlags::Transform;

	return updateFlags;
}

void Mesh::UpdateData(nvrhi::ICommandList* commandList, float3 externalEmittance)
{

	material->UpdateData(commandList, externalEmittance);
}

MeshData Mesh::GetData()
{
	return MeshData();
}

bool Mesh::IsHidden() const
{
	return m_State.any(State::Hidden, State::DismemberHidden, State::SubIndexHidden);
}

void Mesh::CalculateNormals()
{
	eastl::vector<float3> normals;
	normals.resize(vertexData.count, float3(0, 0, 0));

	// Loop over triangles
	for (auto& t : triangleData.triangles) {
		Vertex& v0 = vertexData.vertices[t.x];
		Vertex& v1 = vertexData.vertices[t.y];
		Vertex& v2 = vertexData.vertices[t.z];

		float3 pos0 = v0.Position;
		float3 pos1 = v1.Position;
		float3 pos2 = v2.Position;

		float3 deltaPos1 = pos1 - pos0;
		float3 deltaPos2 = pos2 - pos0;

		float3 faceNormal = deltaPos1.Cross(deltaPos2);

		normals[t.x] += faceNormal;
		normals[t.y] += faceNormal;
		normals[t.z] += faceNormal;
	}

	// Normalize and orthogonalize
	for (size_t i = 0; i < vertexData.count; i++) {
		vertexData.vertices[i].Normal = Util::Math::Normalize(normals[i]);
	}
}
