cmake_minimum_required(VERSION 3.3)
project(osfiles)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
    LibDisk.c
    LibDisk.h
    LibFS.c
    LibFS.h
    main.c)

add_executable(osfiles ${SOURCE_FILES})