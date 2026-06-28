#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <pipestream/buffer_view.hpp>

TEST_SUITE("buffer_view")
{
	TEST_CASE("should return given data")
	{
		std::string str{"Hello World!"};
		const pipestream::buffer_view<char> view{str.data(), str.size()};
		CHECK_EQ(view.size(), str.size());
		CHECK_EQ(view.data(), str.data());
	}

	TEST_CASE("should be able to convert from std::string")
	{
		std::string str{"Hello World!"};
		const pipestream::buffer_view<char> view{str};
		CHECK_EQ(view.size(), str.size());
		CHECK_EQ(view.data(), str.data());
	}
}
