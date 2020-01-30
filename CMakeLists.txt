if (NOT WIN32)
	return()
endif()

project(win-spout)

if(MSVC)
	include_directories(../../deps/spout)
	link_directories(../../deps/spout)
endif()

set(win-spout_SOURCES
	win-spout.cpp)

add_library(win-spout MODULE
	${win-spout_SOURCES})
target_link_libraries(win-spout
	libobs)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	install(FILES ../../deps/spout/Binaries/x64/SpoutLibrary.dll DESTINATION "${OBS_PLUGIN_DESTINATION}")
else()
	install(FILES ../../deps/spout/Binaries/Win32/SpoutLibrary.dll DESTINATION "${OBS_PLUGIN_DESTINATION}")
endif()

install_obs_plugin_with_data(win-spout data)
