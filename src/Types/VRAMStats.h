#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/utility.h>
#include <cstdint>

// Hierarchical VRAM metrics container:
// Category Name -> List of (Item Name, Byte Size / Count)
using VRAMExportData = eastl::vector<eastl::pair<eastl::string, eastl::vector<eastl::pair<eastl::string, uint64_t>>>>;
