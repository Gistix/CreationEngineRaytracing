add_library("${PROJECT_NAME}" SHARED)

target_compile_features(
	"${PROJECT_NAME}"
	PRIVATE
	cxx_std_23
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(AddCXXFiles)
add_cxx_files("${PROJECT_NAME}")

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Plugin.h.in
	${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h
	@ONLY
)

target_precompile_headers(
	"${PROJECT_NAME}"
	PRIVATE
	include/PCH.h
)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)

set(BUILD_TESTS OFF)

if(WIN32)
	add_compile_definitions(_WINDOWS)
endif()

add_compile_definitions(_AMD64_)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
	add_compile_definitions(_UNICODE)

	target_compile_definitions(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:DEBUG>")

	set(SC_DEBUG_OPTS "/fp:strict;/ZI;/Od;/Gy")
	set(SC_RELEASE_OPTS "/Zi;/fp:fast;/GL;/Gy-;/Gm-;/Gw;/sdl-;/GS-;/guard:cf-;/O2;/Ob2;/Oi;/Ot;/Oy;/fp:except-")

	target_compile_options(
		"${PROJECT_NAME}"
		PRIVATE
		/MP
		/W4
		/WX
		/permissive-
		/Zc:alignedNew
		/Zc:auto
		/Zc:__cplusplus
		/Zc:externC
		/Zc:externConstexpr
		/Zc:forScope
		/Zc:hiddenFriend
		/Zc:implicitNoexcept
		/Zc:lambda
		/Zc:noexceptTypes
		/Zc:preprocessor
		/Zc:referenceBinding
		/Zc:rvalueCast
		/Zc:sizedDealloc
		/Zc:strictStrings
		/Zc:ternary
		/Zc:threadSafeInit
		/Zc:trigraphs
		/Zc:wchar_t
		/wd4200 # nonstandard extension used : zero-sized array in struct/union
		/arch:AVX
	)

	target_compile_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:DEBUG>:${SC_DEBUG_OPTS}>")
	target_compile_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:RELEASE>:${SC_RELEASE_OPTS}>")

	target_link_options(
		${PROJECT_NAME}
		PRIVATE
		/WX
		"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
		"$<$<CONFIG:RELEASE>:/LTCG;/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif()

if(BUILD_SKYRIM)
	add_compile_definitions(SKYRIM)

	set(CommonLibName "CommonLibSSE")
	set(CommonLibPath "extern/CommonLibSSE-NG")
	set(CommonLibTarget CommonLibSSE::CommonLibSSE)

	add_subdirectory(${CommonLibPath} ${CommonLibName} EXCLUDE_FROM_ALL)
elseif(BUILD_FALLOUT4)
	add_compile_definitions(FALLOUT4)

	set(CommonLibFullPath "${CMAKE_SOURCE_DIR}/extern/commonlibf4")
	set(CommonLibName "CommonLibF4")
	set(CommonLibTarget CommonLibF4)

	set(CommonLibSharedPath "${CommonLibFullPath}/lib/commonlib-shared")
	set(CommonLibSharedName "CommonLibShared")
	set(CommonLibSharedTarget CommonLibShared)

	set(XMAKE_MODE
		$<$<CONFIG:Debug>:debug>
		$<$<CONFIG:Release>:release>
		$<$<CONFIG:RelWithDebInfo>:releasedbg>
	)

	set(CommonLibBuildPath "${CommonLibFullPath}/build/windows/x64/${XMAKE_MODE}")

	# Find xmake
	find_program(XMAKE_EXE 
		NAMES xmake xmake.exe
		HINTS "C:/Program Files/xmake/"
	)

	if(NOT XMAKE_EXE OR XMAKE_EXE STREQUAL "XMAKE_EXE-NOTFOUND")
		message(FATAL_ERROR "xmake.exe not found in PATH. Please install it or set XMAKE_EXE.")
	else()
		message(STATUS "Found xmake at: ${XMAKE_EXE}")
	endif()

	include(ExternalProject)

	ExternalProject_Add(
		CommonLibF4Build
		SOURCE_DIR ${CommonLibFullPath}
		CONFIGURE_COMMAND ""
		BUILD_COMMAND ${CMAKE_COMMAND} -E env
			XMAKE_COLORTERM=nocolor
			${XMAKE_EXE} -y
		BUILD_IN_SOURCE 1
		INSTALL_COMMAND ""
	)

	add_library(${CommonLibTarget} STATIC IMPORTED GLOBAL)
	add_library(${CommonLibSharedTarget} STATIC IMPORTED GLOBAL)

	set_target_properties(${CommonLibName} PROPERTIES
		IMPORTED_LOCATION
			"${CommonLibBuildPath}/commonlibf4.lib"
		INTERFACE_INCLUDE_DIRECTORIES
			"${CommonLibFullPath}/include"
	)

	set_target_properties(${CommonLibSharedName} PROPERTIES
		IMPORTED_LOCATION
			"${CommonLibBuildPath}/commonlib-shared.lib"
		INTERFACE_INCLUDE_DIRECTORIES
			"${CommonLibSharedPath}/include"
	)

	add_dependencies(${CommonLibTarget} CommonLibF4Build)
endif()

target_include_directories(
	${PROJECT_NAME}
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/interop
	PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}/cmake
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(BUILD_SKYRIM)
	target_link_libraries(
		${PROJECT_NAME}
		PUBLIC
		${CommonLibTarget}
	)
elseif(BUILD_FALLOUT4)
	target_link_libraries(
		${PROJECT_NAME}
		PUBLIC
		${CommonLibTarget}
		${CommonLibSharedTarget}
	)
endif()