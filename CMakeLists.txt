if (NOT WIN32)
	return()
endif()
cmake_minimum_required(VERSION 3.5)
project(win-spout)

set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/plugins/win-spout/external")

if(MSVC)
	include_directories(deps/Spout2/SPOUTSDK/SpoutLibrary)
	include_directories(deps/Spout2/SPOUTSDK/SpoutDirectX/SpoutDX)
	link_directories(deps/Spout2/BUILD/Binaries/x64)
endif()

add_library(win-spout MODULE)
add_library(OBS::win-spout ALIAS win-spout)

target_sources(
	win-spout
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

target_include_directories(win-spout
	PUBLIC 
		obs::frontend-api
		obs::libobs
)

set(SPOUTDX_LIB "${CMAKE_CURRENT_SOURCE_DIR}/deps/Spout2/BUILD/Binaries/x64/SpoutDX.lib")

target_link_libraries(win-spout
	PRIVATE 
		OBS::frontend-api
		OBS::libobs
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

set_target_properties_obs(win-spout PROPERTIES FOLDER plugins/win-spout PREFIX "")

copy_spout_file()

