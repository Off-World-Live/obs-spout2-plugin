if (NOT WIN32)
	return()
endif()
cmake_minimum_required(VERSION 3.5)
project(win-spout)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/plugins/win-spout/external")

set(LIBOBS_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/libobs")
set(LIBOBS_LIB "${CMAKE_BINARY_DIR}/libobs/$<CONFIGURATION>/obs.lib")
set(OBS_FRONTEND_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/UI/obs-frontend-api")
set(OBS_FRONTEND_LIB "${CMAKE_BINARY_DIR}/UI/obs-frontend-api/$<CONFIGURATION>/obs-frontend-api.lib")
set(PTHREAD_LIB "${CMAKE_BINARY_DIR}/deps/w32-pthreads/$<CONFIGURATION>/w32-pthreads.lib")

find_qt(COMPONENTS Core Widgets)

if(MSVC)
	include_directories(deps/Spout2/SPOUTSDK/SpoutLibrary)
	include_directories(deps/Spout2/SPOUTSDK/SpoutDirectX/SpoutDX)
	link_directories(deps/Spout2/BUILD/Binaries/x64)
endif()

set(win-spout_HEADERS
	source/win-spout.h
	source/win-spout-config.h
	source/ui/win-spout-output-settings.h)

set(win-spout_SOURCES
	source/win-spout.cpp
	source/win-spout-source.cpp
	source/win-spout-output.cpp
	source/win-spout-filter.cpp
	source/win-spout-config.cpp
	source/ui/win-spout-output-settings.cpp)

add_library(win-spout MODULE
	${win-spout_SOURCES}
	${win-spout_HEADERS})


include_directories(
	${OBS_FRONTEND_INCLUDE_DIR}
	${LIBOBS_INCLUDE_DIR}
	${Qt5Core_INCLUDES}
	${Qt5Widgets_INCLUDES}
	)

set(SPOUTDX_LIB "${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/x64/SpoutDX.lib")

target_link_libraries(win-spout
	${OBS_FRONTEND_LIB}
	${PTHREAD_LIB}
	${LIBOBS_LIB}
	${SPOUTDX_LIB}
	OBS::w32-pthreads
	Qt::Core
	Qt::Widgets
	)

function(copy_spout_file)
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(_src_suffix "x64")
	else()
		set(_src_suffix "win32")
	endif()
	add_custom_command(TARGET win-spout POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
			"${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/${_src_suffix}/Spout.dll"
			"${OBS_OUTPUT_DIR}/$<CONFIGURATION>/${OBS_PLUGIN_DESTINATION}/Spout.dll"
		COMMAND ${CMAKE_COMMAND} -E copy
			"${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/${_src_suffix}/SpoutLibrary.dll"
			"${OBS_OUTPUT_DIR}/$<CONFIGURATION>/${OBS_PLUGIN_DESTINATION}/SpoutLibrary.dll"
		COMMAND ${CMAKE_COMMAND} -E copy
			"${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/${_src_suffix}/SpoutDX.dll"
			"${OBS_OUTPUT_DIR}/$<CONFIGURATION>/${OBS_PLUGIN_DESTINATION}/SpoutDX.dll"
	)

	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/${_src_suffix}/Spout.pdb")
		add_custom_command(TARGET win-spout POST_BUILD
			COMMAND if $<CONFIG:Debug>==1(
				"${CMAKE_COMMAND}" -E copy
				"${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/${_src_suffix}/Spout.pdb"
				"${OBS_OUTPUT_DIR}/$<CONFIGURATION>/${OBS_PLUGIN_DESTINATION}/Spout.pdb")
			COMMAND if $<CONFIG:RelWithDebInfo>==1(
				"${CMAKE_COMMAND}" -E copy
				"${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/${_src_suffix}/Spout.pdb"
				"${OBS_OUTPUT_DIR}/$<CONFIGURATION>/${OBS_PLUGIN_DESTINATION}/Spout.pdb")
		)
	endif()

	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/${_src_suffix}/SpoutLibrary.pdb")
		add_custom_command(TARGET win-spout POST_BUILD
			COMMAND if $<CONFIG:Debug>==1(
				"${CMAKE_COMMAND}" -E copy
				"${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/${_src_suffix}/SpoutLibrary.pdb"
				"${OBS_OUTPUT_DIR}/$<CONFIGURATION>/${OBS_PLUGIN_DESTINATION}/SpoutLibrary.pdb")
			COMMAND if $<CONFIG:RelWithDebInfo>==1(
				"${CMAKE_COMMAND}" -E copy
				"${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/${_src_suffix}/SpoutLibrary.pdb"
				"${OBS_OUTPUT_DIR}/$<CONFIGURATION>/${OBS_PLUGIN_DESTINATION}/SpoutLibrary.pdb")
		)
	endif()

	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/${_src_suffix}/SpoutDX.pdb")
		add_custom_command(TARGET win-spout POST_BUILD
			COMMAND if $<CONFIG:Debug>==1(
				"${CMAKE_COMMAND}" -E copy
				"${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/${_src_suffix}/SpoutDX.pdb"
				"${OBS_OUTPUT_DIR}/$<CONFIGURATION>/${OBS_PLUGIN_DESTINATION}/SpoutDX.pdb")
			COMMAND if $<CONFIG:RelWithDebInfo>==1(
				"${CMAKE_COMMAND}" -E copy
				"${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/${_src_suffix}/SpoutDX.pdb"
				"${OBS_OUTPUT_DIR}/$<CONFIGURATION>/${OBS_PLUGIN_DESTINATION}/SpoutDX.pdb")
		)
	endif()
endfunction()

setup_plugin_target(win-spout)
copy_spout_file()

