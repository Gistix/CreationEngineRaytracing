cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0116 NEW)
set(CMAKE_POLICY_WARNING_CMP0116 OFF)

project(
    CreationEngineRaytracing
    VERSION 0.0.1
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

set(DEPLOY_DIR $ENV{DEPLOY_DIR})

# ########################################################################################################################
# ## Build options
# ########################################################################################################################
message("Options:")

option(
    AUTO_DEPLOYMENT
    "Copy the build output and addons to env:DEPLOY_DIR"
    OFF
)

option(
    NVRHI_WITH_NVAPI
    "Builds NVRHI with NVAPI for runtime shader profiling"
    OFF
)

if (NVRHI_WITH_NVAPI)
    add_compile_definitions(NVAPI)
endif()

# #######################################################################################################################
# # Add CMake features
# #######################################################################################################################
include(XSEPlugin)

# #######################################################################################################################
# # Find dependencies
# #######################################################################################################################
find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS "BS_thread_pool.hpp")
find_package(magic_enum CONFIG REQUIRED)
find_package(xbyak CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(EASTL CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
find_path(CLIB_UTIL_INCLUDE_DIRS "ClibUtil/utils.hpp")
find_package(cppwinrt CONFIG REQUIRED)
find_package(directx-dxc CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)

find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
	Microsoft::DirectXTK
	Microsoft::DirectX-Headers
    Microsoft::DirectXShaderCompiler
    EASTL
	d3d12.lib
	magic_enum::magic_enum
    xbyak::xbyak
    mikktspace::mikktspace
	d3dcompiler.lib
    ${DETOURS_LIBRARY}
)

include(NVRHI)

include(NRD)

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE 
    CERT_EXPORTS
)

if(AUTO_DEPLOYMENT)
    message(STATUS "Auto deployment enabled. Build output and addons will be copied to ${DEPLOY_DIR}")

    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${DEPLOY_DIR}/SKSE/Plugins)

    install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION ${DEPLOY_DIR}/SKSE/Plugins OPTIONAL)

    install(FILES $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> DESTINATION ${DEPLOY_DIR}/SKSE/Plugins OPTIONAL)

    install(FILES "$<TARGET_FILE_DIR:${PROJECT_NAME}>/GFSDK_Aftermath_Lib.x64.dll" DESTINATION ${DEPLOY_DIR}/SKSE/Plugins OPTIONAL)

    install(DIRECTORY ${CMAKE_SOURCE_DIR}/shaders/ DESTINATION ${DEPLOY_DIR}/Shaders)

    install(DIRECTORY ${CMAKE_SOURCE_DIR}/interop/ DESTINATION ${DEPLOY_DIR}/Shaders/Interop)
endif()