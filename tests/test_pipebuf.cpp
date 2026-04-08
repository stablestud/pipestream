#include <iostream>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <pipestream/pipestream.hpp>

TEST_SUITE("basic_pipebuf") {
	TEST_CASE("should be constructible by its own") {
		pipestream::basic_pipebuf buf(pipestream::unique_fd(1));
	}
}
