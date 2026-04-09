#include "WeakBSGeometry.h"

eastl::unordered_set<RE::BSGeometry*> WeakBSGeometry::alive{};
std::shared_mutex WeakBSGeometry::mutex{};