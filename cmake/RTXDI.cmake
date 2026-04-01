add_subdirectory(extern/RTXDI-Library)

target_compile_options(Rtxdi PRIVATE /W3 /WX-)

target_link_libraries(${PROJECT_NAME} PRIVATE Rtxdi)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/extern/RTXDI-Library/Include
)

if(AUTO_DEPLOYMENT)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/extern/RTXDI-Library/Include/Rtxdi DESTINATION ${DEPLOY_DIR}/Shaders)
endif()