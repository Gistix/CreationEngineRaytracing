set(OMM_PROJECT_FOLDER "OMM-Library")
set(OMM_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(OMM_BUILD_VIEWER OFF CACHE BOOL "" FORCE)
set(OMM_BUILD_OMM_GPU_NVRHI ON CACHE BOOL "" FORCE)
set(OMM_INTEGRATION_LAYER_NVRHI ON CACHE BOOL "" FORCE)
set(OMM_LIB_INSTALL OFF CACHE BOOL "" FORCE)
set(OMM_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(OMM_OUTPUT_BIN_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
add_subdirectory(extern/OMM)

target_compile_options(omm-lib PRIVATE
    /WX-
    /Zi
)

target_link_options(omm-lib PRIVATE /DEBUG)
target_link_libraries(${PROJECT_NAME} PRIVATE omm-lib)

if (TARGET omm-gpu-nvrhi)
    target_compile_options(omm-gpu-nvrhi PRIVATE
        /WX-
        /Zi
    )

    target_link_options(omm-gpu-nvrhi PRIVATE /DEBUG)
    target_link_libraries(${PROJECT_NAME} PRIVATE omm-gpu-nvrhi)
endif()

