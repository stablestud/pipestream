#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <pipestream/fd.hpp>

#include "testutils.hpp"

TEST_SUITE("fd")
{
	TEST_CASE("should move correctly on move constructor")
       	{
		pipestream::fd fd{pipestream::testutils::DUMMY_FD};
		pipestream::fd new_fd{std::move(fd)};
		CHECK_EQ(fd.get(), pipestream::fd::none);
		CHECK_EQ(new_fd.get(), pipestream::testutils::DUMMY_FD);
	}

	TEST_CASE("should move correctly on move assignment operator")
	{
		pipestream::fd fd{pipestream::testutils::DUMMY_FD};
		pipestream::fd new_fd{pipestream::fd::none};
		new_fd = std::move(fd);
		CHECK_EQ(fd.get(), pipestream::fd::none);
		CHECK_EQ(new_fd.get(), pipestream::testutils::DUMMY_FD);
	}

	TEST_CASE("should not move on itself on move assignment operator")
	{
		pipestream::fd fd{pipestream::testutils::DUMMY_FD};
		fd = std::move(fd);
		CHECK(fd.get() == pipestream::testutils::DUMMY_FD);
	}

	TEST_CASE("should return false on has_valid_fd() if fd is invalid")
	{
		pipestream::fd fd{pipestream::fd::none};
		CHECK(not fd.has_valid_fd());
	}

	TEST_CASE("should return true on has_valid_fd() if fd is valid")
	{
		pipestream::fd fd{pipestream::testutils::DUMMY_FD};
		CHECK(fd.has_valid_fd());
	}

	TEST_CASE("should return given fd on get()")
	{
		pipestream::fd fd{pipestream::testutils::DUMMY_FD};
		CHECK_EQ(fd.get(), pipestream::testutils::DUMMY_FD);
	}

	TEST_CASE("should return fd even if fd invalid on get()")
	{
		pipestream::fd fd{pipestream::fd::none};
		CHECK_EQ(fd.get(), pipestream::fd::none);
	}

	TEST_CASE("should return given fd and invalidate internal fd on release()")
	{
		pipestream::fd fd{pipestream::testutils::DUMMY_FD};
		CHECK_EQ(fd.release(), pipestream::testutils::DUMMY_FD);
		CHECK(not fd.has_valid_fd());
	}

	TEST_CASE("should return fd even if fd invalid on release()")
	{
		pipestream::fd fd{pipestream::fd::none};
		CHECK_EQ(fd.release(), pipestream::fd::none);
		CHECK(not fd.has_valid_fd());
	}

	TEST_CASE("should return true on close() with valid fd")
	{
		int pipe_fd[2];
		CHECK_EQ(::pipe(pipe_fd), 0);
		pipestream::fd fd{pipe_fd[0]};
		CHECK(fd.close());
		::close(pipe_fd[1]);
	}

	TEST_CASE("should return true on close() with already closed fd")
	{
		pipestream::fd fd{pipestream::fd::none};
		CHECK(fd.close());
	}

	TEST_CASE("should return false on close() with invalid fd")
	{
		pipestream::fd fd{pipestream::testutils::INVALID_FD};
		CHECK_FALSE(fd.close());
	}

	TEST_CASE("should close fd on close()")
	{
		pipestream::fd_type pipe_fd[2];
		CHECK_FALSE(::pipe(pipe_fd));
		pipestream::fd fd0(pipe_fd[0]);
		pipestream::fd fd1(pipe_fd[1]);
		CHECK(fd0.close());
		CHECK(fd1.close());
		CHECK_EQ(::close(pipe_fd[0]), -1);
		CHECK_EQ(::close(pipe_fd[0]), -1);
	}

	TEST_CASE("should return false if not equal on comparison operator ==")
	{
		pipestream::fd fd1{pipestream::testutils::DUMMY_FD};
		pipestream::fd fd2{pipestream::testutils::INVALID_FD};
		CHECK_FALSE((fd1 == fd2));
	}

	TEST_CASE("should return true if equal on comparison operator ==")
	{
		pipestream::fd fd1{pipestream::testutils::DUMMY_FD};
		pipestream::fd fd2{pipestream::testutils::DUMMY_FD};
		CHECK((fd1 == fd2));
	}

	TEST_CASE("should return false if equal on comparison operator !=")
	{
		pipestream::fd fd1{pipestream::testutils::DUMMY_FD};
		pipestream::fd fd2{pipestream::testutils::DUMMY_FD};
		CHECK_FALSE((fd1 != fd2));
	}

	TEST_CASE("should return true if not equal on comparison operator !=")
	{
		pipestream::fd fd1{pipestream::testutils::DUMMY_FD};
		pipestream::fd fd2{pipestream::testutils::INVALID_FD};
		CHECK((fd1 != fd2));
	}

	TEST_CASE("should return -1 on invalid fd on read()")
	{
		pipestream::fd fd{pipestream::testutils::INVALID_FD};
		char array[12];
		CHECK_EQ(fd.read(array, 12), -1);
	}

	TEST_CASE("should return -1 on invalid fd on write()")
	{
		pipestream::fd fd{pipestream::testutils::INVALID_FD};
		const char array[12]{};
		CHECK_EQ(fd.write(array, 12), -1);
	}

	TEST_CASE("should read data from fd and return count bytes read on read()")
	{
		pipestream::fd_type pipe_fd[2];
		CHECK_FALSE(::pipe(pipe_fd));
		const std::string str{"Hello World!"};
		char array[str.size()+1]{};
		pipestream::fd fd{pipe_fd[0]};
		CHECK_EQ(::write(pipe_fd[1], str.c_str(), str.size()), str.size());
		CHECK_EQ(fd.read(array, str.size()), str.size());
		CHECK_EQ(std::strcmp(array, str.c_str()), 0);
	}

	TEST_CASE("should read N bytes from fd and return N bytes read on read()")
	{
		pipestream::fd_type pipe_fd[2];
		CHECK_FALSE(::pipe(pipe_fd));
		const std::string str1{"Hello"};
		const std::string str2{" World!"};
		const std::string combined{str1+str2};
		char array[str1.size()+1]{};
		pipestream::fd fd{pipe_fd[0]};
		CHECK_EQ(::write(pipe_fd[1], combined.c_str(), combined.size()), combined.size());
		CHECK_EQ(fd.read(array, str1.size()), str1.size());
		CHECK_EQ(std::strcmp(array, str1.c_str()), 0);
	}

	TEST_CASE("should read bytes available from fd even if more was requested and return n bytes read on read()")
	{
		pipestream::fd_type pipe_fd[2];
		CHECK_FALSE(::pipe(pipe_fd));
		const std::string str{"Hello World!"};
		char array[str.size()*2+1]{};
		pipestream::fd fd{pipe_fd[0]};
		CHECK_EQ(::write(pipe_fd[1], str.c_str(), str.size()), str.size());
		CHECK_EQ(fd.read(array, str.size()*2), str.size());
		CHECK_EQ(std::strcmp(array, str.c_str()), 0);
	}

	TEST_CASE("should write data to fd and return count bytes written on write()")
	{
		pipestream::fd_type pipe_fd[2];
		CHECK_FALSE(::pipe(pipe_fd));
		const std::string str{"Hello World!"};
		char array[str.size()+1]{};
		pipestream::fd fd{pipe_fd[1]};
		CHECK_EQ(fd.write(str.c_str(), str.size()), str.size());
		CHECK_EQ(::read(pipe_fd[0], array, str.size()), str.size());
		CHECK_EQ(std::strcmp(array, str.c_str()), 0);
	}

	/* Below are compile time tests only, thats why they are skipped in doctest */

	TEST_CASE_TEMPLATE("should take std::basic_string as read() argument" * doctest::skip(), T, char, wchar_t)
	{
		std::basic_string<T> str{pipestream::testutils::make_string<T>("Hello World!")};
		pipestream::fd fd{pipestream::testutils::DUMMY_FD};
		fd.read(str);
	}

	TEST_CASE_TEMPLATE("should take pipestream::buffer_view as read() argument" * doctest::skip(), T, char, wchar_t)
	{
		std::basic_string<T> str{pipestream::testutils::make_string<T>("Hello World!")};
		pipestream::buffer_view<T> bufview{str};
		pipestream::fd fd{pipestream::testutils::DUMMY_FD};
		fd.read(bufview);
	}

	TEST_CASE_TEMPLATE("should take std::basic_string as write() argument" * doctest::skip(), T, char, wchar_t)
	{
		const std::basic_string<T> str{pipestream::testutils::make_string<T>("Hello World!")};
		pipestream::fd fd{pipestream::testutils::DUMMY_FD};
		fd.write(str);
	}

	TEST_CASE_TEMPLATE("should take std::basic_string_view as write() argument" * doctest::skip(), T, char, wchar_t)
	{
		const std::basic_string<T> str{pipestream::testutils::make_string<T>("Hello World!")};
		const std::basic_string_view<T> strview{str};
		pipestream::fd fd{pipestream::testutils::DUMMY_FD};
		fd.write(strview);
	}

	TEST_CASE_TEMPLATE("should take pipestream::buffer_view as write() argument" * doctest::skip(), T, char, wchar_t)
	{
		std::basic_string<T> str{pipestream::testutils::make_string<T>("Hello World!")};
		pipestream::buffer_view<T> bufview{str};
		pipestream::fd fd{pipestream::testutils::DUMMY_FD};
		fd.write(bufview);
	}
}
