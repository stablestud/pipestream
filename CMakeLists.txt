cmake_minimum_required(VERSION 3.12)
project(pipestream VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_library(pipestream INTERFACE)

target_include_directories(pipestream
	INTERFACE
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
)

option(BUILD_TESTS "run tests" OFF)

if (${BUILD_TESTS})
	enable_testing()
	add_subdirectory(tests)
endif()
