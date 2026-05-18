#pragma once

template <typename T>
class TiledBuffer
{
public:
    nvrhi::BufferHandle buffer;
    nvrhi::HeapHandle heap;

    uint64_t elementSize = sizeof(T);
    uint64_t virtualSize = 0;
    uint64_t heapSize = 0;

    uint32_t totalTiles = 0;
    uint32_t mappedTiles = 0;

    static constexpr uint64_t kTileSize = 64ull * 1024ull;

public:
    TiledBuffer(
        nvrhi::IDevice* device,
        uint64_t minElements,
        uint64_t maxElements,
        const char* debugName)
    {
        virtualSize = elementSize * maxElements;
        const uint64_t initialBytes = elementSize * minElements;

        // --- create buffer ---
        nvrhi::BufferDesc bufferDesc;
        bufferDesc.setByteSize(virtualSize)
            .setStructStride(static_cast<uint32_t>(elementSize))
            .setIsTiled(true)
            .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
            .setDebugName(debugName);

        buffer = device->createBuffer(bufferDesc);

        // --- initial heap ---
        createHeap(device, initialBytes, debugName);

        device->getBufferTiling(buffer, &totalTiles);

        mapInitial(device);
    }

    // -------------------------
    // Resize heap (key feature)
    // -------------------------
    void resizeHeap(
        nvrhi::IDevice* device,
        uint64_t newCapacityBytes)
    {
        newCapacityBytes = align64K(newCapacityBytes);

        if (newCapacityBytes <= heapSize)
            return; // no-op if shrinking or equal

        // Save old heap + mapping info
        nvrhi::HeapHandle oldHeap = heap;

        // Create new heap
        nvrhi::HeapDesc heapDesc;
        heapDesc.debugName = "Resized Tiled Heap";
        heapDesc.type = nvrhi::HeapType::DeviceLocal;
        heapDesc.capacity = newCapacityBytes;

        heap = device->createHeap(heapDesc);
        heapSize = newCapacityBytes;

        // Re-map existing tiles into new heap
        uint32_t tilesToRemap =
            std::min(totalTiles, mappedTiles);

        if (tilesToRemap > 0)
        {
            uint64_t heapOffset = 0;

            nvrhi::TiledBufferRegion region{};
            region.startTileIndexInResource = 0;
            region.tilesNum = tilesToRemap;

            nvrhi::BufferTilesMapping mapping{};
            mapping.tiledBufferRegions = &region;
            mapping.byteOffsets = &heapOffset;
            mapping.numBufferRegions = 1;
            mapping.heap = heap;

            device->updateBufferTileMappings(
                buffer,
                &mapping,
                1,
                nvrhi::CommandQueue::Graphics);
        }

        // oldHeap is released automatically by ref-counting
    }

    // -------------------------
    // Tile mapping
    // -------------------------
    void mapTiles(
        nvrhi::IDevice* device,
        uint32_t startTile,
        uint32_t numTiles,
        uint64_t heapOffsetBytes)
    {
        nvrhi::TiledBufferRegion region{};
        region.startTileIndexInResource = startTile;
        region.tilesNum = numTiles;

        nvrhi::BufferTilesMapping mapping{};
        mapping.tiledBufferRegions = &region;
        mapping.byteOffsets = &heapOffsetBytes;
        mapping.numBufferRegions = 1;
        mapping.heap = heap;

        device->updateBufferTileMappings(
            buffer,
            &mapping,
            1,
            nvrhi::CommandQueue::Graphics);

        mappedTiles = std::max(mappedTiles, startTile + numTiles);
    }

    void mapInitial(nvrhi::IDevice* device)
    {
        uint32_t tilesToMap =
            std::min<uint32_t>(
                static_cast<uint32_t>((heapSize + kTileSize - 1) / kTileSize),
                totalTiles);

        mapTiles(device, 0, tilesToMap, 0);
        mappedTiles = tilesToMap;
    }

private:
    void createHeap(
        nvrhi::IDevice* device,
        uint64_t capacity,
        const char* debugName)
    {
        nvrhi::HeapDesc heapDesc;
        heapDesc.debugName = debugName;
        heapDesc.type = nvrhi::HeapType::DeviceLocal;
        heapDesc.capacity = align64K(capacity);

        heap = device->createHeap(heapDesc);
        heapSize = heapDesc.capacity;
    }

    static uint64_t align64K(uint64_t v)
    {
        return (v + 65535ull) & ~65535ull;
    }
};