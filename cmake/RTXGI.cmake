set(RTXGI_STATIC_LIB ON CACHE BOOL "" FORCE)
set(RTXGI_COORDINATE_SYSTEM "Right Hand, Z-Up" CACHE STRING "" FORCE)
set(RTXGI_API_D3D12_ENABLE ON CACHE BOOL "" FORCE)
set(RTXGI_API_VULKAN_ENABLE OFF CACHE BOOL "" FORCE)

add_subdirectory(extern/RTXGI-DDGI/rtxgi-sdk)

target_compile_options(RTXGI-D3D12 PRIVATE /W3 /WX-)

target_link_libraries(${PROJECT_NAME} PRIVATE RTXGI-D3D12)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/extern/RTXGI-DDGI/rtxgi-sdk/include
)

if(AUTO_DEPLOYMENT)
    # SDK shader includes use ../../../include/rtxgi paths from Shaders/RTXGI/ddgi.
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/extern/RTXGI-DDGI/rtxgi-sdk/shaders/ DESTINATION ${DEPLOY_DIR}/Shaders/RTXGI)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/extern/RTXGI-DDGI/rtxgi-sdk/include/ DESTINATION ${DEPLOY_DIR}/Shaders/include)
endif()
