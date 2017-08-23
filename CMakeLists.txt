cmake_minimum_required (VERSION 2.8)

project(socks4a)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall")

set(SUBDIRS src)
 
foreach(dir ${SUBDIRS})
    add_subdirectory(${dir})
endforeach()

