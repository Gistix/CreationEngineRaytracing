include(FetchContent)

FetchContent_Declare(
    nvapi
    GIT_REPOSITORY https://github.com/NVIDIA/nvapi
    GIT_TAG 9296d671e71608d6d6b7749ed93989af4ada8858
)

FetchContent_MakeAvailable(nvapi)

set(NVAPI_INCLUDE_DIR ${nvapi_SOURCE_DIR})
set(NVAPI_LIBRARY "${nvapi_SOURCE_DIR}/amd64/nvapi64.lib")