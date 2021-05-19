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

find_package(Qt5Core REQUIRED)
find_package(Qt5Widgets REQUIRED)

if(MSVC)
	include_directories(deps/spout/SPOUT_LIBRARY/Include)
	include_directories(deps/spout)
	link_directories(deps/spout/SPOUT_LIBRARY/Binaries/x64)
endif()

set(win-spout_HEADERS
	source/win-spout.h
	source/win-spout-config.h
	source/ui/win-spout-output-settings.h)

set(win-spout_SOURCES
	source/win-spout.cpp
	source/win-spout-source.cpp
	source/win-spout-output.cpp
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


target_link_libraries(win-spout
	${OBS_FRONTEND_LIB}
	${PTHREAD_LIB}
	${LIBOBS_LIB}
	Qt5::Core
	Qt5::Widgets)
	
function(copy_spout_file)
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(_src_suffix "x64")
	else()
		set(_src_suffix "win32")
	endif()
	add_custom_command(
		TARGET win-spout POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
			"${CMAKE_CURRENT_SOURCE_DIR}/deps/spout/VS2012/Binaries/${_src_suffix}/Spout.dll"
			"${OBS_OUTPUT_DIR}/$<CONFIGURATION>/${OBS_PLUGIN_DESTINATION}/Spout.dll")
	add_custom_command(
		TARGET win-spout POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
			"${CMAKE_CURRENT_SOURCE_DIR}/deps/spout/SPOUT_LIBRARY/Binaries/${_src_suffix}/SpoutLibrary.dll"
			"${OBS_OUTPUT_DIR}/$<CONFIGURATION>/${OBS_PLUGIN_DESTINATION}/SpoutLibrary.dll")
endfunction()

install_obs_plugin_with_data(win-spout data)
copy_spout_file()
