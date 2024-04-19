###########################################################################################
#
# This file is PART of autoiptv project
#
# author hongjun.liao <docici@126.com>
# date 2024/4/10

###########################################################################################
cmake_minimum_required(VERSION 2.8.12)
include(deps/libhp/cmakes/libhp.cmake) # hp_cmake_find_deps

###########################################################################################
# autoiptv 
project(autoiptv)

###########################################################################################
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Options are: None, Debug, Release, RelWithDebInfo, MinSizeRel." FORCE)
endif()
# -DCMAKE_EXPORT_COMPILE_COMMANDS is for lsp/clangd, see
# https://clangd.llvm.org/installation.html
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

###########################################################################################
include(CheckIncludeFile)
check_include_file(regex.h HAVE_REGEX_H)
check_include_file(sys/epoll.h HAVE_SYS_EPOLL_H)
check_include_file(sys/timerfd.h HAVE_SYS_TIMERFD_H)
check_include_file(search.h HAVE_SEARCH_H)

###########################################################################################

# sources and deps

if(WIN32)
	# getopt
	set(getopt_hdr .nullfilesrc.h)
	set(getopt_lib "deps/getopt/*.c")
else()
	set(getopt_hdr getopt.h)
	set(getopt_lib c)
endif()

set(g_withs 1 1 1 1)
set(g_hdrs .nullfilesub.h .nullfilesrc.h ${getopt_hdr} curl/curl.h)
set(g_incs "deps/libhp/include/ deps/libhp/deps/" "${PROJECT_BINARY_DIR}" deps/getopt/ deps/curl)
set(g_deps libhp .src getopt curl)
set(g_libs libhp "src/*" ${getopt_lib} curl)

###########################################################################################

# for libhp
set(LIBHP_WITH_CURL 1 CACHE INTERNAL "LIBHP_WITH_CURL")

hp_cmake_find_deps(SRCS AUTOIPTV_WITH_NULL_ deps cmakes g_withs g_hdrs g_incs g_deps g_libs)
if(MSVC)
	file(GLOB SRCS ${SRCS} deps/libc/*.c)
endif()

#message("autoiptv: SRCS=${SRCS}")
add_executable(autoiptv ${SRCS} )
###########################################################################################

foreach(dep IN LISTS g_deps  )
	hp_log("autoiptv: dep='${dep}', ${dep}_INCLUDE_DIRS='${${dep}_INCLUDE_DIRS}', ${dep}_LIBRARIES='${${dep}_LIBRARIES}'")
	target_include_directories(autoiptv PRIVATE ${${dep}_INCLUDE_DIRS} )
	target_link_libraries(autoiptv ${${dep}_LIBRARIES} ) 
endforeach() 

#target_include_directories(autoiptv PRIVATE "${PROJECT_BINARY_DIR}" src/ deps/libhp/include/ 
#	deps/libhp/ deps/libhp/deps/ deps/)
	
#if(MSVC)
#target_include_directories(autoiptv PRIVATE deps/libhp/include/win/)
#endif()

target_compile_options(autoiptv PUBLIC
    -DHAVE_CONFIG_H -D_GNU_SOURCE -DCMAKE_EXPORT_COMPILE_COMMANDS
    # for mongoose
    -DMG_ENABLE_LINES -DMG_ENABLE_DIRECTORY_LISTING
    # for redis
    -DDICT_BENCHMARK_MAIN
)
if(MSVC)
	target_compile_options(autoiptv PUBLIC -D_CRT_SECURE_NO_WARNINGS)
#	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
#		target_compile_options(autoiptv PUBLIC /MTd)
#	else()
#		target_compile_options(autoiptv PUBLIC /MT)
#	endif()
endif()

#target_link_directories(autoiptv PRIVATE lib/ deps/libhp/lib/)
###########################################################################################
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_ID
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND git symbolic-ref --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
###########################################################################################
configure_file("config.h.in" "config.h")
