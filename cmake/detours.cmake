find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")

find_library(DETOURS_LIBRARY_RELEASE detours
    PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
    NO_DEFAULT_PATH
    REQUIRED
)
find_library(DETOURS_LIBRARY_DEBUG detours
    PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
    NO_DEFAULT_PATH
    REQUIRED
)

include(SelectLibraryConfigurations)
select_library_configurations(DETOURS)