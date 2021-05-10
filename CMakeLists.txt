if (NOT WIN32)
	return()
endif()

project(win-spout)

if(MSVC)
	include_directories(../../deps/spout)
	link_directories(../../deps/spout)
endif()

set(win-spout_HEADERS
	source/win-spout.h)

set(win-spout_SOURCES
	source/win-spout.cpp
	source/win-spout-source.cpp)

add_library(win-spout MODULE
	${win-spout_SOURCES}
	${win-spout_HEADERS})

target_link_libraries(win-spout
	libobs)
function(copy_spout_file targetfile)
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(_src_suffix "x64")
	else()
		set(_src_suffix "win32")
	endif()
	add_custom_command(
		TARGET win-spout POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
			"${CMAKE_SOURCE_DIR}/deps/spout/Binaries/${_src_suffix}/${targetfile}"
			"${OBS_OUTPUT_DIR}/$<CONFIGURATION>/${OBS_PLUGIN_DESTINATION}/${targetfile}")
endfunction()

install_obs_plugin_with_data(win-spout data)
copy_spout_file("SpoutLibrary.dll")

