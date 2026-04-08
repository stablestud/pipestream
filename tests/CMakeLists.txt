include(FetchContent)
FetchContent_Declare(doctest
		GIT_REPOSITORY https://github.com/doctest/doctest.git
		GIT_TAG v2.5.1
		GIT_SHALLOW True)
FetchContent_MakeAvailable(doctest)
include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")

add_executable(test_pipebuf test_pipebuf.cpp)
target_link_libraries(test_pipebuf PUBLIC pipestream doctest::doctest)
doctest_discover_tests(test_pipebuf TEST_PREFIX "basic_pipebuf/")

add_executable(test_unique_fd test_unique_fd.cpp)
target_link_libraries(test_unique_fd PUBLIC pipestream doctest::doctest)
doctest_discover_tests(test_unique_fd TEST_PREFIX "unique_fd/")
