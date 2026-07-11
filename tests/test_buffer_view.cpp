#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <pipestream/buffer_view.hpp>

#include "testutils.hpp"

namespace
{
	struct char_view_like {
		using value_type = char;

		std::size_t size() { return 0; }
		char*       data() { return nullptr; }
	};
}

TEST_SUITE("buffer_view")
{
	TEST_CASE("should return given data")
	{
		std::string str{"Hello World!"};
		const pipestream::buffer_view<char> view{str.data(), str.size()};
		CHECK_EQ(view.size(), str.size());
		CHECK_EQ(view.data(), str.data());
	}

	TEST_CASE_TEMPLATE("should accept std::basic_string<T> in ctor", T, char, wchar_t)
	{
		std::basic_string<T> str{pipestream::testutils::make_string<T>("Hello World!")};
		const pipestream::buffer_view<T> view{str};
		CHECK_EQ(view.size(), str.size());
		CHECK_EQ(view.data(), str.data());
	}

	/* Below are compile time tests only, thats why they are skipped in doctest */

	TEST_CASE("should accept custom char container" * doctest::skip())
	{
		char_view_like custom{};
		const pipestream::buffer_view<char_view_like::value_type> view{custom};
	}
}
