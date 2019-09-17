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

install_obs_plugin_with_data(win-spout data)
