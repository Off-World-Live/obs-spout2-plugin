cmake_minimum_required(VERSION 3.28...3.30)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

if (NOT WIN32)
	return()
endif()

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

if(MSVC)
	include_directories(deps/Spout2/SPOUTSDK/SpoutLibrary)
	include_directories(deps/Spout2/SPOUTSDK/SpoutDirectX/SpoutDX)
	link_directories(deps/Spout2/BUILD/Binaries/x64)
endif()

target_sources(
	${CMAKE_PROJECT_NAME}
	PRIVATE 
		source/win-spout.h
		source/win-spout-config.h
		source/ui/win-spout-output-settings.h
		source/win-spout.cpp
		source/win-spout-source.cpp
		source/win-spout-output.cpp
		source/win-spout-filter.cpp
		source/win-spout-config.cpp
		source/ui/win-spout-output-settings.cpp)

set(SPOUT_BINARIES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/x64")
set(SPOUTDX_LIB "${SPOUT_BINARIES_DIR}/SpoutDX.lib")
set(SPOUTLIBRARY_LIB "${SPOUT_BINARIES_DIR}/SpoutLibrary.lib")

target_link_libraries(
	${CMAKE_PROJECT_NAME}
	PRIVATE 
		${SPOUTLIBRARY_LIB}
		${SPOUTDX_LIB}
		OBS::w32-pthreads
	)

set(INSTALLED_PLUGIN_BIN_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_NAME}/${CMAKE_INSTALL_BINDIR}/64bit")
function(copy_spout_file)
	add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
			"${SPOUT_BINARIES_DIR}/Spout.dll"
			"${INSTALLED_PLUGIN_BIN_DIR}/Spout.dll"
		COMMAND ${CMAKE_COMMAND} -E copy
			"${SPOUT_BINARIES_DIR}/SpoutLibrary.dll"
			"${INSTALLED_PLUGIN_BIN_DIR}/SpoutLibrary.dll"
		COMMAND ${CMAKE_COMMAND} -E copy
			"${SPOUT_BINARIES_DIR}/SpoutDX.dll"
			"${INSTALLED_PLUGIN_BIN_DIR}/SpoutDX.dll"
	)

	if(EXISTS "${SPOUT_BINARIES_DIR}/Spout.pdb")
		add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
			COMMAND if $<CONFIG:Debug>==1(
				"${CMAKE_COMMAND}" -E copy
				"${SPOUT_BINARIES_DIR}/Spout.pdb"
				"${INSTALLED_PLUGIN_BIN_DIR}/Spout.pdb")
			COMMAND if $<CONFIG:RelWithDebInfo>==1(
				"${CMAKE_COMMAND}" -E copy
				"${SPOUT_BINARIES_DIR}/Spout.pdb"
				"${INSTALLED_PLUGIN_BIN_DIR}/Spout.pdb")
		)
	endif()

	if(EXISTS "${SPOUT_BINARIES_DIR}/SpoutLibrary.pdb")
		add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
			COMMAND if $<CONFIG:Debug>==1(
				"${CMAKE_COMMAND}" -E copy
				"${SPOUT_BINARIES_DIR}/SpoutLibrary.pdb"
				"${INSTALLED_PLUGIN_BIN_DIR}/SpoutLibrary.pdb")
			COMMAND if $<CONFIG:RelWithDebInfo>==1(
				"${CMAKE_COMMAND}" -E copy
				"${SPOUT_BINARIES_DIR}/SpoutLibrary.pdb"
				"${INSTALLED_PLUGIN_BIN_DIR}/SpoutLibrary.pdb")
		)
	endif()

	if(EXISTS "${SPOUT_BINARIES_DIR}/SpoutDX.pdb")
		add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
			COMMAND if $<CONFIG:Debug>==1(
				"${CMAKE_COMMAND}" -E copy
				"${SPOUT_BINARIES_DIR}/SpoutDX.pdb"
				"${INSTALLED_PLUGIN_BIN_DIR}/SpoutDX.pdb")
			COMMAND if $<CONFIG:RelWithDebInfo>==1(
				"${CMAKE_COMMAND}" -E copy
				"${SPOUT_BINARIES_DIR}/SpoutDX.pdb"
				"${INSTALLED_PLUGIN_BIN_DIR}/SpoutDX.pdb")
		)
	endif()
endfunction()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

copy_spout_file()