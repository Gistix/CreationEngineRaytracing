include(FetchContent)

FetchContent_Declare(
    nvapi
    GIT_REPOSITORY https://github.com/NVIDIA/nvapi
    GIT_TAG cd6918f60b3c9a0476fdfe7e89bb32330602049d
)

FetchContent_MakeAvailable(nvapi)

set(NVAPI_INCLUDE_DIR ${nvapi_SOURCE_DIR})
set(NVAPI_LIBRARY "${nvapi_SOURCE_DIR}/amd64/nvapi64.lib")