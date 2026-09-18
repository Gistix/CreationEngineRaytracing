#include "Core/OmmManager.h"
#include "Renderer.h"
#include "Constants.h"
#include "Utils/Adapter.h"
#include <span>

namespace CERT
{
	OmmManager::OmmManager()
	{
		m_IsSupported = Renderer::GetSingleton()->SupportsFeature(nvrhi::Feature::RayTracingOpacityMicromap);
	}

	std::shared_ptr<OmmResource> OmmManager::GetOrCreate(RE::BSTriShape* triShape, nvrhi::ICommandList* commandList)
	{
		if (!m_IsSupported || !triShape || !commandList) {
			return nullptr;
		}

		auto* extraData = Util::Adapter::GetBinaryExtraData(triShape, Constants::ExtraData::OMMData);
		if (!extraData || !extraData->value || extraData->size < sizeof(uint32_t) * 2) {
			return nullptr;
		}

		std::span<const uint8_t> payloadSpan(static_cast<const uint8_t*>(extraData->value), extraData->size);

		uint32_t magic = 0;
		uint32_t version = 0;
		std::memcpy(&magic, payloadSpan.data(), sizeof(uint32_t));
		std::memcpy(&version, payloadSpan.data() + sizeof(uint32_t), sizeof(uint32_t));

		if (magic != OmmPayloadHeader::kMagic || (version != 1 && version != 2)) {
			return nullptr;
		}

		OmmPayloadHeader header{};
		std::span<const uint16_t> triangleIndices;
		std::span<const OmmDescEntry> descArray;
		std::span<const OmmUsageEntry> descHistogram;
		std::span<const OmmUsageEntry> indexHistogram;
		std::span<const uint8_t> ommBitstream;

		std::vector<OmmDescEntry> derivedDescs;
		std::vector<OmmUsageEntry> derivedDescHisto;
		std::vector<OmmUsageEntry> derivedIndexHisto;

		if (version == 2) {
			if (payloadSpan.size() < sizeof(OmmPayloadHeader)) {
				return nullptr;
			}

			std::memcpy(&header, payloadSpan.data(), sizeof(OmmPayloadHeader));
			if (!header.IsValid()) {
				return nullptr;
			}

			const size_t expectedSize = sizeof(OmmPayloadHeader)
				+ header.indexByteSize
				+ header.descArrayByteSize
				+ header.descHistogramByteSize
				+ header.indexHistogramByteSize
				+ header.ommArrayByteSize;

			if (payloadSpan.size() < expectedSize) {
				return nullptr;
			}

			// Fast cache check by 64-bit content hash
			if (header.contentHash != 0) {
				std::scoped_lock lock(m_CacheMutex);
				auto it = m_Cache.find(header.contentHash);
				if (it != m_Cache.end()) {
					return it->second;
				}
			}

			const uint8_t* ptr = payloadSpan.data() + sizeof(OmmPayloadHeader);

			if (header.indexByteSize > 0) {
				triangleIndices = std::span<const uint16_t>(reinterpret_cast<const uint16_t*>(ptr), header.indexByteSize / sizeof(uint16_t));
				ptr += header.indexByteSize;
			}

			if (header.descArrayByteSize > 0) {
				descArray = std::span<const OmmDescEntry>(reinterpret_cast<const OmmDescEntry*>(ptr), header.descArrayCount);
				ptr += header.descArrayByteSize;
			}

			if (header.descHistogramByteSize > 0) {
				descHistogram = std::span<const OmmUsageEntry>(reinterpret_cast<const OmmUsageEntry*>(ptr), header.descHistogramCount);
				ptr += header.descHistogramByteSize;
			}

			if (header.indexHistogramByteSize > 0) {
				indexHistogram = std::span<const OmmUsageEntry>(reinterpret_cast<const OmmUsageEntry*>(ptr), header.indexHistogramCount);
				ptr += header.indexHistogramByteSize;
			}

			if (header.ommArrayByteSize > 0) {
				ommBitstream = std::span<const uint8_t>(ptr, header.ommArrayByteSize);
			}
		} else { // Version 1 backward compatibility
			if (payloadSpan.size() < sizeof(OmmPayloadHeaderV1)) {
				return nullptr;
			}

			OmmPayloadHeaderV1 v1{};
			std::memcpy(&v1, payloadSpan.data(), sizeof(OmmPayloadHeaderV1));

			header.magic = v1.magic;
			header.version = v1.version;
			header.format = v1.format;
			header.maxSubdivisionLevel = v1.maxSubdivisionLevel;
			header.numTriangles = v1.numTriangles;
			header.indexByteSize = v1.indexByteSize;
			header.ommArrayByteSize = v1.ommArrayByteSize;

			const size_t expectedSize = sizeof(OmmPayloadHeaderV1) + v1.indexByteSize + v1.ommArrayByteSize;
			if (payloadSpan.size() < expectedSize) {
				return nullptr;
			}

			const uint8_t* ptr = payloadSpan.data() + sizeof(OmmPayloadHeaderV1);
			if (v1.indexByteSize > 0) {
				triangleIndices = std::span<const uint16_t>(reinterpret_cast<const uint16_t*>(ptr), v1.indexByteSize / sizeof(uint16_t));
				ptr += v1.indexByteSize;
			}

			if (v1.ommArrayByteSize > 0) {
				ommBitstream = std::span<const uint8_t>(ptr, v1.ommArrayByteSize);
			}

			// Derive micro-map descriptor array from uniform subdivision
			const uint32_t microTris = 1u << (2u * header.maxSubdivisionLevel);
			const uint32_t bytesPerOmm = (header.format == 2) ? (microTris * 2u / 8u) : (microTris / 8u);
			if (bytesPerOmm > 0 && header.ommArrayByteSize > 0) {
				const uint32_t numOmms = header.ommArrayByteSize / bytesPerOmm;
				derivedDescs.resize(numOmms);
				for (uint32_t i = 0; i < numOmms; ++i) {
					derivedDescs[i].byteOffset = i * bytesPerOmm;
					derivedDescs[i].subdivisionLevel = header.maxSubdivisionLevel;
					derivedDescs[i].format = header.format;
				}
				descArray = derivedDescs;

				derivedDescHisto.push_back({ numOmms, header.maxSubdivisionLevel, header.format });
				descHistogram = derivedDescHisto;

				// Count usage across indices (exclude special indices >= 65534)
				uint32_t validUsage = 0;
				for (uint16_t idx : triangleIndices) {
					if (idx < numOmms) {
						validUsage++;
					}
				}
				derivedIndexHisto.push_back({ validUsage, header.maxSubdivisionLevel, header.format });
				indexHistogram = derivedIndexHisto;
			}
		}

		if (triangleIndices.empty() || ommBitstream.empty() || descArray.empty() || descHistogram.empty()) {
			return nullptr;
		}

		auto* device = Renderer::GetSingleton()->GetDevice();
		if (!device) {
			return nullptr;
		}

		// 1. Allocate & Upload raw OMM Bitstream
		auto bitstreamDesc = nvrhi::BufferDesc()
			.setByteSize(ommBitstream.size_bytes())
			.setIsVolatile(false)
			.setInitialState(nvrhi::ResourceStates::OpacityMicromapBuildInput)
			.setKeepInitialState(true)
			.setDebugName("OMM_Bitstream");
		auto bitstreamBuffer = device->createBuffer(bitstreamDesc);
		if (!bitstreamBuffer) {
			logger::error("OmmManager: Failed to allocate bitstream buffer ({} bytes)", ommBitstream.size_bytes());
			return nullptr;
		}
		commandList->writeBuffer(bitstreamBuffer, ommBitstream.data(), ommBitstream.size_bytes());

		// 2. Allocate & Upload Per-OMM Descriptors
		auto descsDesc = nvrhi::BufferDesc()
			.setByteSize(descArray.size_bytes())
			.setIsVolatile(false)
			.setInitialState(nvrhi::ResourceStates::OpacityMicromapBuildInput)
			.setKeepInitialState(true)
			.setDebugName("OMM_PerOmmDescs");
		auto perOmmDescsBuffer = device->createBuffer(descsDesc);
		if (!perOmmDescsBuffer) {
			logger::error("OmmManager: Failed to allocate per-OMM descs buffer ({} bytes)", descArray.size_bytes());
			return nullptr;
		}
		commandList->writeBuffer(perOmmDescsBuffer, descArray.data(), descArray.size_bytes());

		// 3. Allocate & Upload Triangle Index Buffer
		auto indexDesc = nvrhi::BufferDesc()
			.setByteSize(triangleIndices.size_bytes())
			.setIsVolatile(false)
			.setInitialState(nvrhi::ResourceStates::AccelStructBuildInput)
			.setKeepInitialState(true)
			.setDebugName("OMM_IndexBuffer");
		auto indexBuffer = device->createBuffer(indexDesc);
		if (!indexBuffer) {
			logger::error("OmmManager: Failed to allocate index buffer ({} bytes)", triangleIndices.size_bytes());
			return nullptr;
		}
		commandList->writeBuffer(indexBuffer, triangleIndices.data(), triangleIndices.size_bytes());

		// 4. Build Opacity Micromap Array
		nvrhi::rt::OpacityMicromapDesc ommDesc;
		ommDesc.debugName = std::format("OMM_{:016X}", header.contentHash);
		ommDesc.flags = nvrhi::rt::OpacityMicromapBuildFlags::FastTrace;
		ommDesc.inputBuffer = bitstreamBuffer;
		ommDesc.inputBufferOffset = 0;
		ommDesc.perOmmDescs = perOmmDescsBuffer;
		ommDesc.perOmmDescsOffset = 0;

		for (const auto& u : descHistogram) {
			nvrhi::rt::OpacityMicromapUsageCount count;
			count.count = u.count;
			count.subdivisionLevel = u.subdivisionLevel;
			count.format = static_cast<nvrhi::rt::OpacityMicromapFormat>(u.format);
			ommDesc.counts.push_back(count);
		}

		auto opacityMicromap = device->createOpacityMicromap(ommDesc);
		if (!opacityMicromap) {
			logger::error("OmmManager: createOpacityMicromap failed for hash 0x{:016X}", header.contentHash);
			return nullptr;
		}

		commandList->buildOpacityMicromap(opacityMicromap, ommDesc);

		// 5. Build geometry attachment histogram
		std::vector<nvrhi::rt::OpacityMicromapUsageCount> indexHisto;
		indexHisto.reserve(indexHistogram.size());
		for (const auto& u : indexHistogram) {
			nvrhi::rt::OpacityMicromapUsageCount count;
			count.count = u.count;
			count.subdivisionLevel = u.subdivisionLevel;
			count.format = static_cast<nvrhi::rt::OpacityMicromapFormat>(u.format);
			indexHisto.push_back(count);
		}

		auto res = std::make_shared<OmmResource>();
		res->bitstreamBuffer = bitstreamBuffer;
		res->perOmmDescsBuffer = perOmmDescsBuffer;
		res->indexBuffer = indexBuffer;
		res->opacityMicromap = opacityMicromap;
		res->indexHistogram = std::move(indexHisto);

		if (header.contentHash != 0) {
			std::scoped_lock lock(m_CacheMutex);
			m_Cache[header.contentHash] = res;
		}

		logger::info("OmmManager: Built OMM for {} (hash 0x{:016X}, {} OMMs, {} tris)",
			triShape->name.c_str(), header.contentHash, descArray.size(), triangleIndices.size());

		return res;
	}

} // namespace CERT
