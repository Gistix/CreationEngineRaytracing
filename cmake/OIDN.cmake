# cmake/OIDN.cmake - Intel Open Image Denoise (OIDN) integration

if(EXISTS "${CMAKE_SOURCE_DIR}/extern/OIDN/CMakeLists.txt")
    message(STATUS "Configuring OIDN from extern/OIDN...")

    set(OIDN_APPS OFF CACHE BOOL "" FORCE)
    set(OIDN_FILTER_RTLIGHTMAP OFF CACHE BOOL "" FORCE)
    set(OIDN_FILTER_RT ON CACHE BOOL "" FORCE)
    set(OIDN_DEVICE_CPU OFF CACHE BOOL "" FORCE)
    set(OIDN_DEVICE_CUDA ON CACHE BOOL "" FORCE)
    set(OIDN_DEVICE_SYCL OFF CACHE BOOL "" FORCE)
    set(OIDN_DEVICE_HIP OFF CACHE BOOL "" FORCE)
    set(OIDN_WARN_AS_ERRORS OFF CACHE BOOL "" FORCE)

    if(CMAKE_TOOLCHAIN_FILE)
        file(TO_CMAKE_PATH "${CMAKE_TOOLCHAIN_FILE}" _normalized_toolchain)
        set(CMAKE_TOOLCHAIN_FILE "${_normalized_toolchain}" CACHE FILEPATH "" FORCE)
    endif()

    # Save CMAKE_INTERPROCEDURAL_OPTIMIZATION and disable it for OIDN:
    # WINDOWS_EXPORT_ALL_SYMBOLS (__create_def) cannot parse MSVC /GL (LTO/IPO) object files.
    set(_SAVED_CMAKE_IPO "${CMAKE_INTERPROCEDURAL_OPTIMIZATION}")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

    # Disable /WX for OIDN subproject
    set(_SAVED_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/WX([ ]|$)" "/WX- " CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /WX- /wd4201 /wd4458")

    add_subdirectory(extern/OIDN EXCLUDE_FROM_ALL)

    set(CMAKE_CXX_FLAGS "${_SAVED_CMAKE_CXX_FLAGS}")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION "${_SAVED_CMAKE_IPO}")

    foreach(_oidn_target OpenImageDenoise_common OpenImageDenoise_weights OpenImageDenoise_core OpenImageDenoise)
        if(TARGET ${_oidn_target})
            target_compile_options(${_oidn_target} PRIVATE /WX- /wd4201 /wd4458)
        endif()
    endforeach()

    target_link_libraries(
        ${PROJECT_NAME}
        PRIVATE
            OpenImageDenoise
            delayimp.lib
    )

    if(MSVC)
        target_link_options(
            ${PROJECT_NAME}
            PRIVATE
                "/DELAYLOAD:OpenImageDenoise.dll"
        )
    endif()

    if(TARGET OpenImageDenoise_device_cuda)
        add_dependencies(${PROJECT_NAME} OpenImageDenoise_device_cuda)
    endif()

    target_include_directories(
        ${PROJECT_NAME}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/extern/OIDN/include
    )

    target_compile_definitions(
        ${PROJECT_NAME}
        PRIVATE
            ENABLE_OIDN
    )

    if(AUTO_DEPLOYMENT)
        install(TARGETS OpenImageDenoise OpenImageDenoise_core RUNTIME DESTINATION ${DEPLOY_DIR}/${ScriptExtenderName}/Plugins OPTIONAL)
        install(DIRECTORY "${CMAKE_BINARY_DIR}/extern/OIDN/devices/cuda/preinstall/bin/" DESTINATION ${DEPLOY_DIR}/${ScriptExtenderName}/Plugins OPTIONAL)
        install(FILES $<TARGET_FILE_DIR:OpenImageDenoise>/OpenImageDenoise_device_cuda.dll DESTINATION ${DEPLOY_DIR}/${ScriptExtenderName}/Plugins OPTIONAL)
    endif()
endif()
