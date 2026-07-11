function(enable_sanitizers target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /fsanitize=address)
		target_link_options(${target} PRIVATE /fsanitize=address)
	else()
		target_compile_options(${target} PRIVATE
			-fsanitize=undefined,address,leak
			-fno-omit-frame-pointer)
		target_link_options(${target} PRIVATE
			-fsanitize=undefined,address,leak
			-fno-omit-frame-pointer)
	endif()
endfunction()

option(ENABLE_ASAN "enable address sanitizers" OFF)

include(FetchContent)
FetchContent_Declare(doctest
		GIT_REPOSITORY https://github.com/doctest/doctest.git
		GIT_TAG v2.5.1
		GIT_SHALLOW True)
FetchContent_MakeAvailable(doctest)
include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
FetchContent_Declare(trompeloeil
		GIT_REPOSITORY https://github.com/rollbear/trompeloeil.git
		GIT_TAG v49
		GIT_SHALLOW True)
FetchContent_MakeAvailable(trompeloeil)

set(TEST_CXX_OPTIONS
	#-Wall
	#-Wextra
	#-Wpedantic
	-Wconversion
	#-Wsign-conversion
	-Wshadow
)

add_executable(test_pipebuf test_pipebuf.cpp)
target_link_libraries(test_pipebuf PUBLIC pipestream doctest::doctest trompeloeil::trompeloeil)
target_compile_options(test_pipebuf PRIVATE ${TEST_CXX_OPTIONS})
doctest_discover_tests(test_pipebuf TEST_PREFIX "basic_pipebuf/")

add_executable(test_fd test_fd.cpp)
target_link_libraries(test_fd PUBLIC pipestream doctest::doctest)
target_compile_options(test_fd PRIVATE ${TEST_CXX_OPTIONS})
doctest_discover_tests(test_fd TEST_PREFIX "fd/")

add_executable(test_buffer_view test_buffer_view.cpp)
target_link_libraries(test_buffer_view PUBLIC pipestream doctest::doctest)
target_compile_options(test_buffer_view PRIVATE ${TEST_CXX_OPTIONS})
doctest_discover_tests(test_buffer_view TEST_PREFIX "buffer_view/")

if(${ENABLE_ASAN})
	enable_sanitizers(test_pipebuf)
	enable_sanitizers(test_fd)
	enable_sanitizers(test_buffer_view)
endif()

add_custom_target(tests)
add_dependencies(tests
	test_pipebuf
	test_fd
	test_buffer_view
)
