#ifndef INTEROP_MESH_SLOT_REMAP_HLSLI
#define INTEROP_MESH_SLOT_REMAP_HLSLI

// Maps (Instance.FirstGeometryID + GeometryIndex()) → geometry slot + instance ID.
// Stored as two uint32_t values in a ByteAddressBuffer.
// x = geometrySlot (per-geometry index from AllocateGeometryIndex, index into Meshes)
// y = instanceID (TLAS instance index for this cluster)
// Transforms/Properties are indexed by mesh slot from Meshes[geometrySlot].MeshSlot.
#define MESH_SLOT_REMAP_STRIDE 8

#endif // INTEROP_MESH_SLOT_REMAP_HLSLI
