set(OMM_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(OMM_BUILD_VIEWER OFF CACHE BOOL "" FORCE)
set(OMM_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(OMM_ENABLE_PRECOMPILED_SHADERS_SPIRV ON CACHE BOOL "" FORCE)
set(OMM_ENABLE_PRECOMPILED_SHADERS_DXIL ON CACHE BOOL "" FORCE)
set(OMM_LIB_TARGET_NAME omm-lib CACHE STRING "" FORCE)
set(OMM_LIB_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(extern/OMM EXCLUDE_FROM_ALL)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        omm-lib
        omm-gpu-nvrhi
)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/extern/OMM/libraries/omm-lib/include
        ${CMAKE_SOURCE_DIR}/extern/OMM/libraries/omm-gpu-nvrhi
)

if (MSVC)
    target_compile_options(omm-lib PRIVATE /W3 /WX-)
    target_compile_options(omm-gpu-nvrhi PRIVATE /W3 /WX-)
    if (TARGET ShaderMakeBlob)
        target_compile_options(ShaderMakeBlob PRIVATE /W3 /WX-)
    endif()
endif()

