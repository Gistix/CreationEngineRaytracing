set(NRD_USE_DX12 ON CACHE BOOL "" FORCE)
set(NRD_STATIC_LIBRARY ON CACHE BOOL "" FORCE)

set(NRD_EMBEDS_SPIRV_SHADERS OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "" FORCE)

add_subdirectory(extern/NRD)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        "extern/NRD/Include"
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        NRD
)