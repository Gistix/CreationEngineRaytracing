	# Hardcoded as release for now
	set(XMAKE_MODE "release")

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


	# CommonLibF4 was built with xmake's spdlog, match its headers/lib
	file(GLOB SPDLOG_XMAKE_DIRS "$ENV{LOCALAPPDATA}/.xmake/packages/s/spdlog/v1.16.*/*")
	if(SPDLOG_XMAKE_DIRS)
		list(GET SPDLOG_XMAKE_DIRS 0 SPDLOG_XMAKE)
		file(COPY "${SPDLOG_XMAKE}/include/spdlog" DESTINATION "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows-static-md/include")
		file(COPY "${SPDLOG_XMAKE}/lib/spdlog.lib" DESTINATION "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows-static-md/lib")
	endif()