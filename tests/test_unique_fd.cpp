#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <pipestream/unique_fd.hpp>

namespace
{
	constexpr fd_type valid_fd = 999;
}

TEST_SUITE("unique_fd") {
	TEST_CASE("should move correctly on move constructor")
       	{
		pipestream::unique_fd fd(valid_fd);
		pipestream::unique_fd new_fd(std::move(fd));
		CHECK_EQ(fd.get(), pipestream::unique_fd::NONE);
		CHECK_EQ(new_fd.get(), valid_fd);
	}

	TEST_CASE("should move correctly on move assignment operator")
	{
		pipestream::unique_fd fd(valid_fd);
		pipestream::unique_fd new_fd(pipestream::unique_fd::NONE);
		new_fd = std::move(fd);
		CHECK_EQ(fd.get(), pipestream::unique_fd::NONE);
		CHECK_EQ(new_fd.get(), valid_fd);
	}

	TEST_CASE("should return false on has_fd() if fd is invalid")
	{
		pipestream::unique_fd fd(pipestream::unique_fd::NONE);
		CHECK(not fd.has_fd());
	}

	TEST_CASE("should return true on has_fd() if fd is valid")
	{
		pipestream::unique_fd fd(valid_fd);
		CHECK(fd.has_fd());
	}

	TEST_CASE("should return given fd on get()")
	{
		pipestream::unique_fd fd(valid_fd);
		CHECK_EQ(fd.get(), valid_fd);
	}

	TEST_CASE("should return fd even if fd invalid on get()")
	{
		pipestream::unique_fd fd(pipestream::unique_fd::NONE);
		CHECK_EQ(fd.get(), pipestream::unique_fd::NONE);
	}

	TEST_CASE("should return given fd and invalidate internal fd on release()")
	{
		pipestream::unique_fd fd(valid_fd);
		CHECK_EQ(fd.release(), valid_fd);
		CHECK(not fd.has_fd());
	}

	TEST_CASE("should return fd even if fd invalid on release()")
	{
		pipestream::unique_fd fd(pipestream::unique_fd::NONE);
		CHECK_EQ(fd.release(), pipestream::unique_fd::NONE);
		CHECK(not fd.has_fd());
	}

	TEST_CASE("should return false on close() with valid fd")
	{
		int pipe_fd[2];
		CHECK_EQ(::pipe(pipe_fd), 0);
		pipestream::unique_fd fd(pipe_fd[0]);
		CHECK_FALSE(fd.close());
	}

	TEST_CASE("should return true on close() with invalid fd")
	{
		pipestream::unique_fd fd(pipestream::unique_fd::NONE);
		CHECK(fd.close());
	}
}
