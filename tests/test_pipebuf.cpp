#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_TREAT_CHAR_STAR_AS_STRING
#include <doctest/doctest.h>
#include <doctest/trompeloeil.hpp>

#include <pipestream/pipestream.hpp>

namespace
{
	constexpr pipestream::fd_type DUMMY_FD = 999;
	constexpr pipestream::fd_type INVALID_FD = -999;

	template<typename CharT, unsigned int N>
	constexpr std::basic_string<CharT> make_string(const char (&c_str)[N])
	{
		// Using if-constexpr statement instead of template specialization
		// because partial template function specialization is not allowed,
		// only overloading or full template function specialization are valid.
		// However I cannot fully specialize the template function (due to N).
		// I could have created functor struct but that seemed to be overkill
		if constexpr (not std::is_same_v<std::string, std::basic_string<CharT> >) {
			std::basic_string<CharT> str{};
			for (unsigned int i = 0; i < N-1; i++) {
				str.push_back(static_cast<CharT>(c_str[i]));
			}
			return str;
		} else {
			return std::string(c_str, N-1);
		}
	}

	template<typename CharT>
	int strlen(const CharT *const str)
	{
		int count{};
		if (str not_eq nullptr) {
			const CharT* pos = str;
			while (*(pos++) not_eq 0) {
				count++;
			}
		}
		return count;
	}

	template<>
	int strlen<char>(const char *const str)
	{
		return std::strlen(str);
	}

	template<typename CharT>
	int strcmp(const CharT *const str1, const CharT *const str2)
	{
		const int str1len = strlen<CharT>(str1);
		const int str2len = strlen<CharT>(str2);
		if (str1len not_eq str2len) {
			return str1len-str2len;
		}
		return std::memcmp(str1, str2, std::min(str1len, str2len)*sizeof(CharT));
	}

	template<> // this is a full template function specialization
	int strcmp<char>(const char *const str1, const char *const str2)
	{
		return std::strcmp(str1, str2);
	}

	template<typename C>
	void read_str_from_fd(pipestream::fd_type read_fd, const std::basic_string<C> str)
	{
		pipestream::basic_pipebuf<C> buf{pipestream::fd(read_fd)};
		C arr[str.size()+1];
		const std::streamsize count = buf.sgetn(arr, str.size());
		arr[count] = 0x0; // terminate bytes read from stream
		CHECK_FALSE(strcmp<C>(arr, str.c_str()));
	}

	template<typename C>
	void write_str_to_fd(pipestream::fd_type write_fd, const std::basic_string<C> str)
	{
		pipestream::basic_pipebuf<C> buf{pipestream::fd(write_fd)};
		CHECK_EQ(buf.sputn(str.c_str(), str.size()), str.size());
	}

	template<typename C>
	C make_test_char(int offset = 0)
	{
		C c{};
		for (int i = 0; i < sizeof(C); i++) {
			*(reinterpret_cast<char*>(&c)+i) = 'a'+i+offset*sizeof(C);
		}
		return c;
	}

	class mocked_fd : public pipestream::fd {
	public:
		explicit mocked_fd(const pipestream::fd_type basic_fd) : pipestream::fd(basic_fd) {};
		explicit mocked_fd() : pipestream::fd(DUMMY_FD) {};
		static constexpr bool trompeloeil_movable_mock = true;
		MAKE_MOCK0(close, bool(void), override);
		MAKE_CONST_MOCK2(read, std::streamsize(void*, const std::streamsize), override);
		MAKE_CONST_MOCK2(write, std::streamsize(const void*, const std::streamsize), override);

		template<typename CharT = char, typename Buffer>
		std::streamsize read(Buffer buf)
		{
			return pipestream::fd::read<CharT>(std::forward<Buffer>(buf));
		}

		template<typename CharT = char, typename BufferView>
		std::streamsize write(BufferView bufv)
		{
			return pipestream::fd::write<CharT>(std::forward<BufferView>(bufv));
		}
	};

	template<typename T>
	void flush_mocked(pipestream::basic_pipebuf<T, std::char_traits<T>, mocked_fd>& buf)
	{
		// This function is required, in the case when mocked read is forbidden,
		// before deconstructor run to flush/empty the pipebuf buffer,
		// then flushing does not need to be done in the deconstructor,
		// that would trigger forbidden read. Thus we temporarily allow reading here
		auto m = NAMED_ALLOW_CALL(buf.rdfd(), write(trompeloeil::_, trompeloeil::_))
				.RETURN(_2);
		buf.pubsync();
	}
}

TEST_SUITE("class operators")
{
	TEST_CASE_TEMPLATE("should move on move ctor", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf1 = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		const std::basic_string<T> combined{str1+str2};
		REQUIRE_CALL(buf1.rdfd(), read(trompeloeil::_, trompeloeil::ge(str1.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str1.c_str(), str1.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str1.size()*sizeof(T));
		CHECK_EQ(buf1.sbumpc(), str1.at(0)); // populate buf from pipe
		pipestream::basic_pipebuf<T, std::char_traits<T>, mocked_fd> buf2{std::move(buf1)};
		CHECK_EQ(buf1.sgetc(), pipestream::basic_pipebuf<T>::traits_type::eof());
		CHECK_EQ(buf1.sungetc(), pipestream::basic_pipebuf<T>::traits_type::eof());
		T aux[combined.size()+1]{};
		CHECK_EQ(buf2.sungetc(), str1.at(0));
		REQUIRE_CALL(buf2.rdfd(), read(trompeloeil::_, trompeloeil::ge(str2.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str2.c_str(), str2.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str2.size()*sizeof(T));
		CHECK_EQ(buf2.sgetn(aux, combined.size()), combined.size());
		CHECK_EQ(strcmp<T>(aux, combined.c_str()), 0);
		CHECK_EQ(buf1.rdfd().get(), pipestream::fd::none);
		CHECK_EQ(buf2.rdfd().get(), DUMMY_FD);
	}

	TEST_CASE_TEMPLATE("should move on move assign", T, char, wchar_t, char16_t, char32_t)
	{
		// Cannot use mocked_fd as it does not support move assign operator,
		// using real read/write operations instead
		pipestream::fd_type pipe_fd[2];
		CHECK_FALSE(::pipe(pipe_fd));
		pipestream::basic_pipebuf<T> buf1{pipestream::fd(pipe_fd[0])};
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		const std::basic_string<T> combined{str1+str2};
		T array[str1.size()+1]{};
		buf1.pubsetbuf(array, str1.size());
		std::thread t{write_str_to_fd<T>, pipe_fd[1], combined};
		CHECK_EQ(buf1.sbumpc(), str1.at(0));
		CHECK_EQ(strcmp<T>(str1.c_str(), array), 0);
		pipestream::basic_pipebuf<T> buf2{pipestream::fd(DUMMY_FD)};
		buf2 = std::move(buf1);
		CHECK_EQ(buf1.sgetc(), pipestream::basic_pipebuf<T>::traits_type::eof());
		CHECK_EQ(buf1.sungetc(), pipestream::basic_pipebuf<T>::traits_type::eof());
		T aux[combined.size()+1]{};
		CHECK_EQ(buf2.sungetc(), str1.at(0));
		CHECK_EQ(buf2.sgetn(aux, combined.size()), combined.size());
		CHECK_EQ(strcmp<T>(combined.c_str(), aux), 0);
		CHECK_EQ(buf1.rdfd().get(), pipestream::fd::none);
		CHECK_EQ(buf2.rdfd().get(), pipe_fd[0]);
		t.join();
	}

	TEST_CASE("should not move assign on itself")
	{
		// Not using mocked_fd as it cannot move assign
		pipestream::pipebuf buf{pipestream::fd(DUMMY_FD)};
		buf = std::move(buf);
		CHECK_EQ(buf.rdfd(), pipestream::fd(DUMMY_FD));
		const std::string str{"Hello"};
		CHECK_EQ(buf.sputn(str.c_str(), str.size()), str.size());
	}

	TEST_CASE_TEMPLATE("should swap on swap()", T, char, wchar_t, char16_t, char32_t)
	{
		// Not using mocked_fd as it cannot move assign which is used in swap()
		pipestream::fd_type pipe_fd[2];
		CHECK_FALSE(::pipe(pipe_fd));
		pipestream::basic_pipebuf<T> buf_read{pipestream::fd(pipe_fd[0])};
		pipestream::basic_pipebuf<T> buf_write{pipestream::fd(pipe_fd[1])};
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		std::basic_string<T> combined{str1+str2};
		buf_write.sputn(combined.c_str(), combined.size());
		CHECK_EQ(buf_write.pubsync(), 0);
		buf_write.swap(buf_read);
		CHECK_EQ(buf_read.sgetc(), pipestream::basic_pipebuf<T>::traits_type::eof());
		CHECK_EQ(buf_read.sputc(str2.at(1)), str2.at(1));
		CHECK_EQ(buf_read.pubsync(), 0);
		T aux[combined.size()+2]{};
		CHECK_EQ(buf_write.sgetn(aux, combined.size()+1), combined.size()+1);
		CHECK_EQ(strcmp<T>(aux, combined.append(1, str2.at(1)).c_str()), 0);
	}

	TEST_CASE("should not object slice on swap()")
	{
		// Cannot use mocked_fd to mock as it cannot be moved
		pipestream::fd_type pipe_fd[2];
		CHECK_FALSE(::pipe(pipe_fd));
		pipestream::pipebuf buf_write{pipestream::fd(pipe_fd[1])};
		pipestream::pipebuf buf_dummy{pipestream::fd(INVALID_FD)};
		const std::string str1{"Hello"};
		const std::string str2{" World!"};
		const std::string combined{str1+str2};
		std::thread read_thread{read_str_from_fd<char>, pipe_fd[0], combined};
		buf_write.sputn(str1.c_str(), str1.size());
		buf_write.swap(buf_dummy);
		buf_dummy.sputn(str2.c_str(), str2.size());
		buf_dummy.pubsync();
		buf_dummy.rdfd().close(); // Force close to prevent deadlock if object slicing is happening
		read_thread.join();
	}

	TEST_CASE_TEMPLATE("should provide non-member swap function for swap()", T, char, wchar_t)
	{
		pipestream::basic_pipebuf<T> buf_dummy{pipestream::fd(DUMMY_FD)};
		pipestream::basic_pipebuf<T> buf_invalid{pipestream::fd(INVALID_FD)};
		pipestream::swap(buf_dummy, buf_invalid);
		CHECK_EQ(buf_dummy.rdfd().get(), INVALID_FD);
		CHECK_EQ(buf_invalid.rdfd().get(), DUMMY_FD);
	}

	TEST_CASE_TEMPLATE("should swap on std::swap basic_pipebuf<T>", T, char, wchar_t)
	{
		// Cannot use mocked_fd to mock as it cannot be moved
		pipestream::fd_type pipe_fd[2];
		CHECK_FALSE(::pipe(pipe_fd));
		pipestream::basic_pipebuf<T> buf_write{pipestream::fd(pipe_fd[1])};
		pipestream::basic_pipebuf<T> buf_dummy{pipestream::fd(INVALID_FD)};
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		const std::basic_string<T> combined{str1+str2};
		std::thread read_thread{read_str_from_fd<T>, pipe_fd[0], combined};
		buf_write.sputn(str1.c_str(), str1.size());
		std::swap(buf_write, buf_dummy);
		buf_dummy.sputn(str2.c_str(), str2.size());
		buf_dummy.pubsync();
		buf_dummy.rdfd().close(); // Force close to prevent deadlock if object slicing is happening
		read_thread.join();
	}
}

TEST_SUITE("fd")
{
	TEST_CASE("should return given fd with rdfd()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		CHECK((buf.rdfd() == pipestream::fd(DUMMY_FD)));
	}

	TEST_CASE("should replace current fd with other fd on rdfd(other)")
	{
		pipestream::fd new_fd{INVALID_FD};
		// Not using mocked_fd as it cannot move assign
		pipestream::pipebuf buf{pipestream::fd(DUMMY_FD)};
		buf.rdfd(std::move(new_fd));
		CHECK(buf.rdfd().get() == INVALID_FD);
	}
}

TEST_SUITE("setbuf()")
{
	TEST_CASE("should replace internal buf with user provided buf on setbuf()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		std::string str{"Hello World"};
		char array[str.size() + 1]{};
		std::strcpy(array, str.c_str());
		buf.pubsetbuf(array, str.size());
		CHECK_EQ(std::strcmp(array, str.c_str()), 0);
		str = "Jqkkl World";
		CHECK_EQ(std::strcmp(array, "Hello World"), 0);
		CHECK_EQ(buf.sputn("Jqkkl", 5), 5);
		CHECK_EQ(std::strcmp(array, str.c_str()), 0);
		flush_mocked(buf);
	}

	TEST_CASE("should not delete user provided buf from setbuf() on deconstructor")
	{
		constexpr int arr_size = 4096;
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		pipestream::pipebuf::char_type arr1[arr_size];
		buf.pubsetbuf(arr1, sizeof(arr1));
		std::unique_ptr<char[]> arr2 = std::make_unique<char[]>(arr_size);
		// The following does not throw on errors, instead process will be terminated
		// Using CHECK_NOTHROW to emphasize that these should not cause any trouble/program state "exceptions"
		CHECK_NOTHROW(buf.pubsetbuf(arr2.get(), sizeof(arr1)));
		CHECK_NOTHROW(buf.pubsetbuf(arr1, sizeof(arr1)));
		CHECK_NOTHROW(std::memset(arr2.get(), 0xff, sizeof(arr1)));
	}

	TEST_CASE("should throw when giving invalid buf arguments on setbuf()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		CHECK_THROWS_WITH(buf.pubsetbuf(nullptr, 3), "invalid setbuf arguments");
		CHECK_THROWS_WITH(buf.pubsetbuf(reinterpret_cast<char*>(&buf), 0), "invalid setbuf arguments");
		CHECK_THROWS_WITH(buf.pubsetbuf(reinterpret_cast<char*>(&buf), 1), "invalid setbuf arguments");
		CHECK_THROWS_WITH(buf.pubsetbuf(reinterpret_cast<char*>(&buf), -99), "invalid setbuf arguments");
	}

	TEST_CASE("should return this and allow nullptr as buffer on setbuf()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		CHECK_EQ(buf.pubsetbuf(nullptr, 0), &buf);
	}

	TEST_CASE("should leave buffer in consistent state when setbuf() throws")
	{
		// This test relies on -fsanitize=undefined,addresses to catch out-of-bounds access if state is inconsistent
		const std::string str{"Hello World"};
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		try {
			buf.pubsetbuf(nullptr, 12);
		} catch(...) {}
		CHECK_EQ(buf.sputn(str.c_str(), str.size()), str.size());
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str.size()))
				.WITH(not std::memcmp(_1, str.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		CHECK_EQ(buf.pubsync(), 0);
	}

	TEST_CASE("should not write data to old buffer but to new buffer when new buffer is set with setbuf()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		const std::string str{"Hello World"};
		pipestream::pipebuf::char_type buf1[str.size()*2]{};
		pipestream::pipebuf::char_type buf2[str.size()*2]{};
		pipestream::pipebuf::char_type empty[str.size()*2]{};
		buf.pubsetbuf(buf1, str.size()*2);
		buf.pubsetbuf(buf2, str.size()*2);
		CHECK_EQ(buf.sputn(str.c_str(), str.size()), str.size());
		CHECK_EQ(std::memcmp(buf1, empty, str.size()+1), 0);
		CHECK_EQ(std::strcmp(str.c_str(), buf2), 0);
		flush_mocked(buf);
	}

	TEST_CASE("should discard data in buffer on setbuf()")
	{
		const std::string str1{"Hello World"};
		const std::string str2{"Super String World"};
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		char array1[str2.size()*2]{};
		buf.pubsetbuf(array1, str2.size()*2);
		CHECK_EQ(buf.sputn(str1.c_str(), str1.size()), str1.size());
		CHECK_EQ(std::strcmp(str1.c_str(), array1), 0);
		char array2[str2.size()*2]{};
		buf.pubsetbuf(array2, str2.size()*2);
		CHECK_EQ(buf.sputn(str2.c_str(), str2.size()), str2.size());
		CHECK_EQ(std::strcmp(str2.c_str(), array2), 0);
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str2.size()))
				.WITH(not std::memcmp(_1, str2.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		buf.pubsync();
	}
}

TEST_SUITE("sputn()")
{
	TEST_CASE_TEMPLATE("should write data directly to pipe when using null buffer on sputn()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		buf.pubsetbuf(nullptr, 0);
		std::basic_string<T> str{make_string<T>("Hello World")};
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str.size()*sizeof(T)))
				.WITH(not strcmp<T>(static_cast<const T*>(_1), str.c_str()))
				.TIMES(1)
				.RETURN(_2);
		CHECK_EQ(buf.sputn(str.c_str(), str.size()), str.size());
	}

	TEST_CASE("should return 0 on write attempt on closed/invalid fd with null buffer on sputn()")
	{
		auto buf = pipestream::make_pipebuf<char>(mocked_fd{});
		const std::string str{"Hello World"};
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str.size()))
				.TIMES(1)
				.RETURN(-1);
		buf.pubsetbuf(nullptr, 0);
		CHECK_EQ(buf.sputn(str.c_str(), str.size()), 0);
	}

	TEST_CASE("should throw on invalid arguments on sputn()")
	{
		auto buf = pipestream::make_pipebuf<char>(mocked_fd{});
		const std::string array{"Hello World!"};
		pipestream::pipebuf::char_type buf_arr[array.size()+1]{};
		buf.pubsetbuf(buf_arr, array.size()+1);
		CHECK_THROWS_WITH(buf.sputn(nullptr, 12), "invalid sputn arguments");
		CHECK_THROWS_WITH(buf.sputn(array.c_str(), -12), "invalid sputn arguments");
		CHECK_EQ(buf.sputn(array.c_str(),  0), 0);
		CHECK_EQ(std::strcmp(buf_arr, ""), 0);
	}

	TEST_CASE_TEMPLATE("should write string to pipe with buffer content when buffer is already populated (overflow) on sputn()", T, char, wchar_t, char16_t, char32_t)
	{
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		const auto combined = str1+str2;
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		T array[std::max(str1.size(), str2.size()) + 3]{};
		buf.pubsetbuf(array, std::max(str1.size(), str2.size()) + 2);
		CHECK_EQ(buf.sputn(str1.c_str(), str1.size()), str1.size());
		CHECK_FALSE(strcmp<T>(str1.c_str(), array));
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, combined.size()*sizeof(T)))
				.WITH(not std::memcmp(_1, combined.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		CHECK_EQ(buf.sputn(str2.c_str(), str2.size()), str2.size());
		CHECK_FALSE(strcmp<T>(str1.c_str(), array));
	}

	TEST_CASE_TEMPLATE("should write string to buffer when enough free buffer space is available", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		T array[(str1.size()+str2.size())*2]{};
		buf.pubsetbuf(array, (str1.size()+str2.size())*2);
		CHECK_EQ(buf.sputn(str1.c_str(), str1.size()), str1.size());
		CHECK_EQ(buf.sputn(str2.c_str(), str2.size()), str2.size());
		CHECK_EQ(strcmp<T>(array, (str1+str2).c_str()), 0);
		flush_mocked(buf);
	}

	TEST_CASE_TEMPLATE("should write string directly into pipe if its bigger than buffer and buffer is empty on sputn()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello World! This is a very long string, lets make it even longer, and even longer so we truly go over the buffers size")};
		T array[str.size() / 3]{};
		buf.pubsetbuf(array, str.size() / 3);
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str.size()*sizeof(T)))
				.WITH(not std::memcmp(_1, str.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		CHECK_EQ(buf.sputn(str.c_str(), str.size()), str.size());
		CHECK_FALSE(strcmp<T>(make_string<T>("").c_str(), array));
	}

	TEST_CASE_TEMPLATE("should write populated buffer and string into pipe if buffer has not enough space on sputn()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello World!")};
		const std::basic_string<T> str2{make_string<T>("This is another long string!")};
		const std::basic_string<T> combined{str1+str2};
		T array[str1.size()+4]{};
		buf.pubsetbuf(array, str1.size()+4);
		CHECK_EQ(buf.sputn(str1.c_str(), str1.size()), str1.size());
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, combined.size()*sizeof(T)))
				.WITH(not std::memcmp(_1, combined.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		CHECK_EQ(buf.sputn(str2.c_str(), str2.size()), str2.size());
	}

	TEST_CASE_TEMPLATE("should return n amount of characters written on partial string write to pipe on sputn()" * doctest::skip(), T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello World!")};
		T array[str.size()/2]{};
		buf.pubsetbuf(array, str.size()/2);
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str.size()/2*sizeof(T));
		CHECK_EQ(buf.sputn(str.c_str(), str.size()), str.size()/2);
	}

	TEST_CASE_TEMPLATE("should leave buffer in consistent state if write fails on sputn()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello World!")};
		const std::basic_string<T> str2{make_string<T>("This is another long string!")};
		const std::basic_string<T> combined{str1+str2};
		T array[str1.size()+4]{};
		buf.pubsetbuf(array, str1.size()+4);
		CHECK_EQ(buf.sputn(str1.c_str(), str1.size()), str1.size()); // populate buffer
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, trompeloeil::_))
				.TIMES(1)
				.RETURN(0);
		CHECK_EQ(buf.sputn(str2.c_str(), str2.size()), 0); // attempt write into pipe; will fail
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, combined.size()*sizeof(T)))
				.WITH(not std::memcmp(_1, combined.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		// Check if on another write attempt buffer content is also being written to pipe
		CHECK_EQ(buf.sputn(str2.c_str(), str2.size()), str2.size());
	}

	TEST_CASE_TEMPLATE("should leave buffer in consistent state if write partially fails on sputn()" * doctest::skip(), T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello World!")};
		const std::basic_string<T> str2{make_string<T>("This is another long string!")};
		const std::basic_string<T> combined{str1+str2};
		T array[str1.size()+4]{};
		buf.pubsetbuf(array, str1.size()+4);
		CHECK_EQ(buf.sputn(str1.c_str(), str1.size()), str1.size()); // populate buffer
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, trompeloeil::_))
				.TIMES(1)
				.RETURN(0);
		CHECK_EQ(buf.sputn(str2.c_str(), str2.size()), 0); // attempt write into pipe; will fail
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, combined.size()*sizeof(T)))
				.WITH(not std::memcmp(_1, combined.c_str(), _2))
				.TIMES(1)
				.RETURN(str1.size()/2*sizeof(T));
		CHECK_EQ(buf.sputn(str2.c_str(), str2.size()), 0);
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, (combined.size()-str1.size()/2)*sizeof(T)))
				.WITH(not std::memcmp(_1, combined.c_str()+str1.size()/2, _2))
				.TIMES(1)
				.RETURN((combined.size()-str1.size()/2)*sizeof(T));
		CHECK_EQ(buf.sputn(str2.c_str(), str2.size()), str2.size());
	}

	TEST_CASE_TEMPLATE("should return characters written on partial character write on sputn()" * doctest::skip(), T, wchar_t, char16_t, char32_t)
	{
		static_assert(pipestream::is_multibyte_v<T>);
		const std::basic_string<T> str{make_string<T>("Hello")};
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		buf.pubsetbuf(nullptr, 0);
		const int write_bytes = str.size()/2*sizeof(T)+sizeof(T)/2; // write half string and half character
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(write_bytes);
		CHECK_EQ(buf.sputn(str.c_str(), str.size()), write_bytes/sizeof(T)+1);
	}

	TEST_CASE_TEMPLATE("should continue writing character+string after partial character write on sputn()" * doctest::skip(), T, wchar_t, char16_t, char32_t)
	{
		// TODO
		static_assert(pipestream::is_multibyte_v<T>);
		const std::basic_string<T> str{make_string<T>("Hello World!")};
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const int write1bytes = str.size()/2*sizeof(T)+sizeof(T)/2; // Write half of str and half a character
		const int write2bytes = str.size()*sizeof(T)-write1bytes; // Write other half of character and the rest of the string
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, trompeloeil::gt(write1bytes)))
				.TIMES(1)
				.RETURN(write1bytes);
		CHECK_EQ(buf.sputn(str.c_str(), str.size()), write1bytes/sizeof(T));
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, write2bytes))
				.TIMES(1)
				.RETURN(write2bytes);
		CHECK_EQ(buf.sputn(str.c_str()+write1bytes*sizeof(T), write2bytes/sizeof(T)), write2bytes/sizeof(T)); // TODO +1 because partial character is now fulfilled?
	}
}

TEST_SUITE("sputc()")
{
	TEST_CASE_TEMPLATE("should write data directly to pipe when using null buffer on sputc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		buf.pubsetbuf(nullptr, 0);
		const T c{'E'};
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, sizeof(T)))
				.WITH(*static_cast<const T*>(_1) == c)
				.TIMES(1)
				.RETURN(_2);
		CHECK_EQ(buf.sputc(c), c);
	}

	TEST_CASE_TEMPLATE("should write data to buffer on sputc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		T array[4]{};
		buf.pubsetbuf(array, 4);
		const T c{(T)'E'};
		CHECK_EQ(buf.sputc(c), c);
		CHECK_EQ(array[0], c);
		const T d{(T)'Y'};
		CHECK_EQ(buf.sputc(d), d);
		CHECK_EQ(array[0], c);
		CHECK_EQ(array[1], d);
		flush_mocked(buf);
	}

	TEST_CASE_TEMPLATE("should return EOF on failure on sputc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		buf.pubsetbuf(nullptr, 0);
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, sizeof(T)))
				.TIMES(1)
				.RETURN(-1);
		CHECK_EQ(buf.sputc((T)'E'), pipestream::basic_pipebuf<T>::traits_type::eof());
	}

	TEST_CASE_TEMPLATE("should flush buffer if buffer is full on sputc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello")};
		T array[str.size()]{};
		buf.pubsetbuf(array, str.size());
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str.size()*sizeof(T)))
				.WITH(not std::memcmp(_1, str.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		for (const T c : str) {
			buf.sputc(c);
		}
	}

	TEST_CASE_TEMPLATE("should return EOF on partial character write on sputc()" * doctest::skip(), T, wchar_t, char16_t, char32_t)
	{
		static_assert(pipestream::is_multibyte_v<T>);
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		buf.pubsetbuf(nullptr, 0);
		const T c = make_test_char<T>();
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, trompeloeil::ge(sizeof(T))))
				.TIMES(1)
				.RETURN(sizeof(T)/2);
		CHECK_EQ(buf.sputc(c), pipestream::basic_pipebuf<T>::traits_type::eof());
	}

	TEST_CASE_TEMPLATE("should continue writing character after partial character write on sputc()" * doctest::skip(), T, wchar_t, char16_t, char32_t)
	{
		static_assert(pipestream::is_multibyte_v<T>);
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		buf.pubsetbuf(nullptr, 0);
		T c[2];
		c[0] = make_test_char<T>(0);
		c[1] = make_test_char<T>(1);
		const T* c_ptr = c;
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, trompeloeil::ge(sizeof(T))))
				.TIMES(1)
				.RETURN(sizeof(T)/2);
		// On partial write sputc() returns EOF however on a subsequent write the partial write will be completed
		// This could cause confusion, as it the user would expect the character was not written, but on another write it will be written, even though its not intended.
		// We could also pretend the write is successful when its actually not. The user would then expect the the character to be in the pipe, however it will only be on next write
		CHECK_EQ(buf.sputc(c[0]), pipestream::basic_pipebuf<T>::traits_type::eof());
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, trompeloeil::ge(sizeof(T)+sizeof(T)/2)))
				.WITH(not std::memcmp(_1, reinterpret_cast<const char*>(c_ptr)+sizeof(T)/2, sizeof(T)+sizeof(T)/2))
				.TIMES(1)
				.RETURN(sizeof(T)+sizeof(T)/2);
		CHECK_EQ(buf.sputc(c[1]), c[1]);
	}

	TEST_CASE_TEMPLATE("should not cause buffer overflow on failed writes with multiple sputc()", T, char, wchar_t, char16_t, char32_t)
	{
		constexpr int len = 32;
		constexpr int buf_len = len/2;
		constexpr int expected_write = buf_len-1;
		T filler = make_test_char<T>();
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		T array[len*sizeof(T)]{};
		buf.pubsetbuf(array, buf_len);
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, buf_len*sizeof(T)))
				.TIMES(AT_LEAST(1))
				.RETURN(0);
		for (int i = 0; i < buf_len-1; i++) {
			buf.sputc(filler); // Fill buffer
		}
		CHECK_EQ(array[expected_write-1], filler); // Second-to-last buffer space should have been written in buffer
		CHECK_EQ(buf.sputc(filler), pipestream::basic_pipebuf<T>::traits_type::eof()); // Trigger overflow
		CHECK_EQ(array[expected_write], filler);
		CHECK_EQ(buf.sputc(0x1a), pipestream::basic_pipebuf<T>::traits_type::eof()); // Retry with different value
		CHECK_EQ(array[expected_write], 0x1a); // Last buffer space should be overwritten as internal buffer ptr should not been advanced
		CHECK_EQ(array[expected_write+1], 0); // Buffer should not been written further than allowed
		CHECK_EQ(buf.sputc(0x79), pipestream::basic_pipebuf<T>::traits_type::eof()); // Retry with different value
		CHECK_EQ(array[expected_write], 0x79); // Last buffer space should be overwritten as internal buffer ptr should not been advanced
		CHECK_EQ(array[expected_write+1], 0); // Buffer should not been written further than allowed
		flush_mocked(buf);
	}
}

TEST_SUITE("sgetc()")
{
	TEST_CASE_TEMPLATE("should get character from buffer if buffer is already populated on sgetc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello")};
		T array[str.size()+1]{};
		buf.pubsetbuf(array, str.size()+1);
		T tmp;
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(str.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str.c_str(), str.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str.size()*sizeof(T));
		CHECK_EQ(buf.sgetn(&tmp, 1), 1);
		CHECK_EQ(strcmp<T>(array, str.c_str()), 0);
		for (int i = 1; i <= str.size(); i++) {
			// sgetc() should not increases gptr, thus returned always the same data
			CHECK_EQ(buf.sgetc(), str.at(1));
		}
	}

	TEST_CASE_TEMPLATE("should populate buffer on sgetc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello")};
		T array[str.size()+1]{};
		buf.pubsetbuf(array, str.size()+1);
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(str.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str.c_str(), str.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str.size()*sizeof(T));
		CHECK_EQ(buf.sgetc(), str.at(0));
		CHECK_EQ(strcmp<T>(str.c_str(), array), 0);
	}

	TEST_CASE_TEMPLATE("should read from pipe directly if buffer is nullptr on sgetc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		buf.pubsetbuf(nullptr, 0);
		const std::basic_string<T> str{make_string<T>("Hello")};
		int n = 0;
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, sizeof(T)))
				.LR_SIDE_EFFECT(*static_cast<T*>(_1) = str.at(n++))
				.TIMES(5)
				.RETURN(sizeof(T));
		for (int i = 0; i < str.size(); i++) {
			// On nullptr buf getc advances the stream unlike its default behavior
			CHECK_EQ(buf.sgetc(), str.at(i));
		}
	}

	TEST_CASE_TEMPLATE("should return EOF on read failure on sgetc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{INVALID_FD});
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::_))
				.TIMES(1)
				.RETURN(-1);
		CHECK_EQ(buf.sgetc(), pipestream::basic_pipebuf<T>::traits_type::eof());
	}

	TEST_CASE_TEMPLATE("should leave buffer in consistent state when pipe returns EOF on underflow on sgetc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("This is the 1st string")};
		const std::basic_string<T> str2{make_string<T>("Hello World!")};
		const std::basic_string<T> combined{str1+str2};
		T array[combined.size()+1]{};
		buf.pubsetbuf(array, str1.size());
		T dst[combined.size()+1]{};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(combined.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, combined.c_str(), combined.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(combined.size()*sizeof(T));
		CHECK_EQ(buf.sgetn(dst, str1.size()), str1.size()); // populate buffer
		CHECK_EQ(buf.sgetn(dst, str2.size()), str2.size()); // consume buffer
		const std::basic_string<T> buf_str{str1.substr(str1.size()-1, 1)+str2};
		CHECK_EQ(strcmp<T>(array, buf_str.c_str()), 0);
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::_))
				.TIMES(1)
				.RETURN(0);
		CHECK_EQ(buf.sgetc(), pipestream::basic_pipebuf<T>::traits_type::eof());
		CHECK_EQ(strcmp<T>(array, buf_str.c_str()), 0);
	}

	TEST_CASE_TEMPLATE("should return EOF on partial character read on sgetc()" * doctest::skip(), T, wchar_t, char16_t, char32_t)
	{
		static_assert(pipestream::is_multibyte_v<T>);
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(sizeof(T))))
				.TIMES(1)
				.RETURN(sizeof(T)/2);
		CHECK_EQ(buf.sgetc(), pipestream::basic_pipebuf<T>::traits_type::eof());
	}

	TEST_CASE_TEMPLATE("should return character after partial character read on sgetc()" * doctest::skip(), T, wchar_t, char16_t, char32_t)
	{
		static_assert(pipestream::is_multibyte_v<T>);
		using int_type = typename pipestream::basic_pipebuf<T>::int_type;
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const T c = make_test_char<T>();
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, &c, sizeof(T)/2))
				.TIMES(1)
				.RETURN(sizeof(T)/2);
		CHECK_EQ(buf.sgetc(), pipestream::basic_pipebuf<T>::traits_type::eof());
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, reinterpret_cast<const char*>(&c)+sizeof(T)/2, sizeof(T)/2))
				.TIMES(1)
				.RETURN(sizeof(T)/2);
		CHECK_EQ(buf.sgetc(), c);
	}
}

TEST_SUITE("sbumpc()")
{
	TEST_CASE_TEMPLATE("should get new character from buffer if buffer is populated on sbumpc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello")};
		T array[str.size()+1]{};
		buf.pubsetbuf(array, str.size()+1);
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(str.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str.c_str(), str.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str.size()*sizeof(T));
		for (int i = 0; i < str.size(); i++) {
			CHECK_EQ(buf.sbumpc(), str.at(i));
		}
	}

	TEST_CASE_TEMPLATE("should populate buffer on sbumpc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello")};
		T array[str.size()+1]{};
		buf.pubsetbuf(array, str.size()+1);
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(str.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str.c_str(), str.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str.size()*sizeof(T));
		CHECK_EQ(buf.sbumpc(), str.at(0));
		CHECK_EQ(strcmp<T>(array, str.c_str()), 0);
	}

	TEST_CASE_TEMPLATE("should read from pipe directly if buffer is nullptr on sbumpc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		buf.pubsetbuf(nullptr, 0);
		const std::basic_string<T> str{make_string<T>("Hello")};
		int n = 0;
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, sizeof(T)))
				.LR_SIDE_EFFECT(*static_cast<T*>(_1) = str.at(n++))
				.TIMES(5)
				.RETURN(sizeof(T));
		for (int i = 0; i < str.size(); i++) {
			CHECK_EQ(buf.sbumpc(), str.at(i));
		}
	}

	TEST_CASE("should return EOF on read failure on sbumpc()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{INVALID_FD});
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::_))
				.TIMES(1)
				.RETURN(-1);
		CHECK_EQ(buf.sbumpc(), pipestream::pipebuf::traits_type::eof());
	}
}

TEST_SUITE("snextc()")
{
	TEST_CASE_TEMPLATE("should get next character from buffer if buffer is populated on snextc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello")};
		T array[str.size()+1]{};
		buf.pubsetbuf(array, str.size()+1);
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(str.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str.c_str(), str.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str.size()*sizeof(T));
		for (int i = 1; i < str.size(); i++) {
			CHECK_EQ(buf.snextc(), str.at(i));
		}
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::_))
				.TIMES(1)
				.RETURN(0); // imitating that pipe has been closed
		CHECK_EQ(buf.snextc(), pipestream::basic_pipebuf<T>::traits_type::eof());
	}

	TEST_CASE_TEMPLATE("should populate buffer on snextc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello")};
		T array[str.size()+1]{};
		buf.pubsetbuf(array, str.size()+1);
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(str.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str.c_str(), str.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str.size()*sizeof(T));
		CHECK_EQ(buf.snextc(), str.at(1));
		CHECK_EQ(strcmp<T>(array, str.c_str()), 0);
	}

	TEST_CASE_TEMPLATE("should read directly from pipe if buffer is nullptr on snextc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		buf.pubsetbuf(nullptr, 0);
		const std::basic_string<T> str{make_string<T>("Hello")};
		int n = 0;
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, sizeof(T)))
				.LR_SIDE_EFFECT(std::memcpy(_1, str.c_str()+n++, sizeof(T)))
				.TIMES(4)
				.RETURN(_2);
		for (int i = 1; i < str.size(); i += 2) {
			// if buffer is nullptr then snextc behaves very differently,
			// it reads directly from the twice pipe a single characters and always discards the first one
			CHECK_EQ(buf.snextc(), str.at(i));
		}
	}

	TEST_CASE("should return EOF on read failure on snextc()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{INVALID_FD});
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::_))
				.TIMES(1)
				.RETURN(-1);
		CHECK_EQ(buf.snextc(), pipestream::pipebuf::traits_type::eof());
	}
}

TEST_SUITE("get area putback")
{
	TEST_CASE("should not putback character on empty buffer on sputbackc()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{DUMMY_FD});
		const std::string str{"Hello World"};
		char array[str.size()]{};
		auto* array_ptr = array;
		buf.pubsetbuf(array, str.size());
		CHECK_EQ(buf.sputbackc(str.at(0)), pipestream::pipebuf::traits_type::eof());
		CHECK_EQ(array_ptr, "");
	}

	TEST_CASE("should not unget character on empty buffer on sungetc()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		const std::string str{"Hello World"};
		char array[str.size()+1]{};
		buf.pubsetbuf(array, str.size()+1);
		CHECK_EQ(buf.sungetc(), pipestream::pipebuf::traits_type::eof());
		CHECK_EQ(std::strcmp(array, ""), 0);
	}

	TEST_CASE_TEMPLATE("should putback character on populated buffer on sputbackc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello World!")};
		T aux[str.size()+1]{};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(str.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str.c_str(), str.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str.size()*sizeof(T));
		CHECK_EQ(buf.sgetn(aux, str.size()), str.size());
		CHECK_EQ(buf.sputbackc(*str.rbegin()), *str.rbegin());
		CHECK_EQ(buf.sgetc(), *str.rbegin());
	}

	TEST_CASE_TEMPLATE("should putback new character on populated buffer on sputbackc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello World!")};
		T aux[str.size()+1]{};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(str.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str.c_str(), str.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str.size()*sizeof(T));
		CHECK_EQ(buf.sgetn(aux, str.size()), str.size());
		CHECK_EQ(buf.sputbackc(*str.begin()), *str.begin());
		CHECK_EQ(buf.sgetc(), *str.begin());
	}

	TEST_CASE_TEMPLATE("should unget character on populated buffer on sungetc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello World!")};
		T aux[str.size()+1]{};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(str.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str.c_str(), str.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str.size()*sizeof(T));
		CHECK_EQ(buf.sgetn(aux, str.size()), str.size());
		CHECK_EQ(buf.sungetc(), *str.rbegin());
		CHECK_EQ(buf.sgetc(), *str.rbegin());
	}

	TEST_CASE_TEMPLATE("should putback character on after larger than buffer read on sungetc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello World!")};
		const std::basic_string<T> str2{make_string<T>("This is the 2nd string")};
		const std::basic_string<T> combined{str1+str2};
		T array[combined.size()+1]{};
		buf.pubsetbuf(array, str1.size());
		T dst[combined.size()+1]{};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(combined.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, combined.c_str(), combined.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(combined.size()*sizeof(T));
		CHECK_EQ(buf.sgetn(dst, combined.size()), combined.size());
		CHECK_EQ(buf.sputbackc((T)'A'), (T)'A');
		CHECK_EQ(buf.sgetc(), (T)'A');
	}

	TEST_CASE_TEMPLATE("should putback character after read with returned extra data on sungetc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("This is the 1st string")};
		const std::basic_string<T> str2{make_string<T>("Hello World!")};
		const std::basic_string<T> combined{str1+str2};
		T array[combined.size()+1]{};
		buf.pubsetbuf(array, str1.size());
		T dst[combined.size()+1]{};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(combined.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, combined.c_str(), combined.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(combined.size()*sizeof(T));
		CHECK_EQ(buf.sgetn(dst, str1.size()), str1.size());
		CHECK_EQ(buf.sputbackc((T)'A'), (T)'A');
		CHECK_EQ(buf.sgetc(), (T)'A');
	}

	TEST_CASE_TEMPLATE("should unget character on after larger than buffer read on sungetc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello World!")};
		const std::basic_string<T> str2{make_string<T>("This is the 2nd string")};
		const std::basic_string<T> combined{str1+str2};
		T array[combined.size()+1]{};
		buf.pubsetbuf(array, str1.size());
		T dst[combined.size()+1]{};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(combined.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, combined.c_str(), combined.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(combined.size()*sizeof(T));
		CHECK_EQ(buf.sgetn(dst, combined.size()), combined.size());
		CHECK_EQ(buf.sungetc(), *combined.rbegin());
		CHECK_EQ(buf.sgetc(), *combined.rbegin());
	}

	TEST_CASE_TEMPLATE("should unget character after read with returned extra data on sungetc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("This is the 1st string")};
		const std::basic_string<T> str2{make_string<T>("Hello World!")};
		const std::basic_string<T> combined{str1+str2};
		T array[combined.size()+1]{};
		buf.pubsetbuf(array, str1.size());
		T dst[combined.size()+1]{};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(combined.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, combined.c_str(), combined.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(combined.size()*sizeof(T));
		CHECK_EQ(buf.sgetn(dst, str1.size()), str1.size());
		CHECK_EQ(buf.sungetc(), *str1.rbegin());
		CHECK_EQ(buf.sgetc(), *str1.rbegin());
	}

	TEST_CASE_TEMPLATE("should putback character after underflow on sungetc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("This is the 1st string")};
		const std::basic_string<T> str2{make_string<T>("Hello World!")};
		const std::basic_string<T> combined{str1+str2};
		T array[combined.size()+1]{};
		buf.pubsetbuf(array, str1.size());
		T dst[combined.size()+1]{};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(combined.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, combined.c_str(), combined.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(combined.size()*sizeof(T));
		CHECK_EQ(buf.sgetn(dst, str1.size()), str1.size());
		CHECK_EQ(buf.sgetn(dst, str2.size()), str2.size());
		const std::basic_string<T> new_str{make_string<T>("C++ goes forward")};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(new_str.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, new_str.c_str(), new_str.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(new_str.size()*sizeof(T));
		CHECK_EQ(buf.sgetc(), new_str.at(0));
		CHECK_EQ(buf.sungetc(), *combined.rbegin());
	}

	TEST_CASE_TEMPLATE("should print injected string on buffer with size 2 on sgetc() and sputbackc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello World!")};
		T array[2]{};
		buf.pubsetbuf(array, 2);
		std::basic_string<T> result{};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, 2*sizeof(T)))
				.SIDE_EFFECT(std::memcpy(_1, str.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		buf.sbumpc();
		for (auto it = str.begin(); it != str.end(); it++) {
			CHECK_EQ(buf.sputbackc(*it), *it);
			result.append(1, buf.sbumpc());
		}
		CHECK_EQ(str.compare(result), 0);
	}

	TEST_CASE_TEMPLATE("should allow putback on buffer begin after underflow() on sputbackc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		T array[str1.size()+1]{};
		buf.pubsetbuf(array, str1.size());
		T aux[str1.size()];
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(str1.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str1.c_str(), str1.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str1.size()*sizeof(T));
		buf.sgetn(aux, str1.size());
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, (str1.size()-1)*sizeof(T)))
				.SIDE_EFFECT(std::memcpy(_1, str2.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		buf.sgetc(); // cause underflow
		CHECK_EQ(buf.sputbackc(str1.at(str1.size()-1)), str1.at(str1.size()-1));
		CHECK_EQ(buf.sgetc(), str1.at(str1.size()-1));
	}

	TEST_CASE_TEMPLATE("should allow overwrite putback on buffer begin after underflow() on sputbackc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		T array[str1.size()+1]{};
		buf.pubsetbuf(array, str1.size());
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(str1.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str1.c_str(), str1.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str1.size()*sizeof(T));
		T aux[str1.size()];
		buf.sgetn(aux, str1.size());
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, (str1.size()-1)*sizeof(T)))
				.SIDE_EFFECT(std::memcpy(_1, str2.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		buf.sgetc(); // cause underflow
		CHECK_EQ(buf.sputbackc(str1.at(0)), str1.at(0));
		CHECK_EQ(buf.sgetc(), str1.at(0));
	}

	TEST_CASE_TEMPLATE("should allow ungetc on buffer begin after underflow() on sungetc()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		T array[str1.size()+1]{};
		buf.pubsetbuf(array, str1.size());
		T aux[str1.size()];
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(str1.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str1.c_str(), str1.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str1.size()*sizeof(T));
		buf.sgetn(aux, str1.size());
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, (str1.size()-1)*sizeof(T)))
				// only reading str2 partially because buffer-1 is too small
				.SIDE_EFFECT(std::memcpy(_1, str2.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		buf.sgetc(); // cause underflow
		CHECK_EQ(buf.sungetc(), str1.at(str1.size()-1));
		CHECK_EQ(buf.sgetc(), str1.at(str1.size()-1));
	}

	TEST_CASE("should return EOF on nullptr buffer on sputbackc()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		buf.pubsetbuf(nullptr, 0);
		CHECK_EQ(buf.sputbackc('H'), pipestream::pipebuf::traits_type::eof());
	}

	TEST_CASE("should return EOF on nullptr buffer on sungetc()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		buf.pubsetbuf(nullptr, 0);
		CHECK_EQ(buf.sungetc(), pipestream::pipebuf::traits_type::eof());
	}
}

TEST_SUITE("sgetn()")
{
	TEST_CASE_TEMPLATE("should read data directly from pipe when using null buffer on sgetn()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		buf.pubsetbuf(nullptr, 0);
		std::basic_string<T> str{make_string<T>("Hello World")};
		T dst[str.size() + 1]{};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, str.size()*sizeof(T)))
				.SIDE_EFFECT(std::memcpy(_1, str.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		CHECK_EQ(buf.sgetn(dst, str.size()), str.size());
		CHECK_FALSE(strcmp<T>(dst, str.c_str()));
	}

	TEST_CASE_TEMPLATE("should return 0 on read attempt on closed/invalid fd with null buffer on sgetn()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{INVALID_FD});
		buf.pubsetbuf(nullptr, 0);
		T array[32]{};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, 32*sizeof(T)))
				.TIMES(1)
				.RETURN(-1);
		CHECK_EQ(buf.sgetn(array, 32), 0);
	}

	TEST_CASE("should throw on invalid arguments on sgetn()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		char array[32]{};
		CHECK_THROWS_WITH(buf.sgetn(nullptr, 12), "invalid sgetn arguments");
		CHECK_THROWS_WITH(buf.sgetn(array, -12), "invalid sgetn arguments");
		CHECK_EQ(buf.sgetn(array, 0), 0);
		CHECK_EQ(std::strcmp(array, ""), 0);
	}

	TEST_CASE("should work with uncommen arguments on sgetn()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		char array[32]{};
		SUBCASE("should not read from pipe on 0 data request") {
			CHECK_EQ(buf.sgetn(array, 0), 0);
			CHECK_EQ(std::strcmp(array, ""), 0);
		}
		SUBCASE("should return single character on 1 data request") {
			const std::string str{"Hello World!"};
			REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(1)))
					.SIDE_EFFECT(std::memcpy(_1, str.c_str(), str.size()))
					.TIMES(1)
					.RETURN(str.size());
			CHECK_EQ(buf.sgetn(array, 1), 1);
			CHECK_EQ(array, str.substr(0, 1).c_str());
		}
	}

	TEST_CASE_TEMPLATE("should populate and read data from buffer on sgetn()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		const std::basic_string<T> combined{str1+str2};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(combined.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, combined.c_str(), combined.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(combined.size()*sizeof(T));
		T dst[combined.size()+1]{};
		CHECK_EQ(buf.sgetn(dst, str1.size()), str1.size());
		CHECK_EQ(strcmp<T>(dst, str1.c_str()), 0);
		CHECK_EQ(buf.sgetn(dst, str2.size()), str2.size());
		CHECK_EQ(strcmp<T>(dst, str2.c_str()), 0);
	}

	TEST_CASE_TEMPLATE("should read data with single read even if its bigger than buffer on sgetn()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		const std::basic_string<T> str3{make_string<T>(" This is the third line!")};
		const std::basic_string<T> combined{str1+str2+str3};
		T array[str1.size()]{};
		buf.pubsetbuf(array, str1.size());
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(combined.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, combined.c_str(), combined.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(combined.size()*sizeof(T));
		T dst[combined.size()+1]{};
		CHECK_EQ(buf.sgetn(dst, combined.size()), combined.size());
		CHECK_EQ(strcmp<T>(dst, combined.c_str()), 0);
	}

	TEST_CASE_TEMPLATE("should read data on populated buffer with single read even if its bigger than buffer on sgetn()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		const std::basic_string<T> str3{make_string<T>(" This is the third line!")};
		const std::basic_string<T> combined{str1+str2+str3};
		T array[str1.size()*2]{};
		buf.pubsetbuf(array, str1.size()*2);
		T dst[combined.size()+1]{};
		const int read1len = (str1.size()+3)*sizeof(T);
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(read1len)))
				.SIDE_EFFECT(std::memcpy(_1, (str1+combined).c_str(), read1len))
				.TIMES(1)
				.RETURN(read1len);
		CHECK_EQ(buf.sgetn(dst, str1.size()), str1.size());
		CHECK_EQ(strcmp<T>(dst, str1.c_str()), 0);
		const int read2len = (combined.size()-3)*sizeof(T);
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(read2len)))
				.SIDE_EFFECT(std::memcpy(_1, combined.c_str()+3, read2len))
				.TIMES(1)
				.RETURN(read2len);
		CHECK_EQ(buf.sgetn(dst, combined.size()), combined.size());
		CHECK_EQ(strcmp<T>(dst, combined.c_str()), 0);
	}

	TEST_CASE_TEMPLATE("should retry reading if on pipe-read not enough was returned on sgetn()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		const std::basic_string<T> str3{make_string<T>(" This is the third line!")};
		const std::basic_string<T> combined{str1+str2+str3};
		T dst[combined.size()+1]{};
		trompeloeil::sequence seq;
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(combined.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str1.c_str(), str1.size()*sizeof(T)))
				.IN_SEQUENCE(seq)
				.TIMES(1)
				.RETURN(str1.size()*sizeof(T));
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(combined.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str2.c_str(), str2.size()*sizeof(T)))
				.IN_SEQUENCE(seq)
				.TIMES(1)
				.RETURN(str2.size()*sizeof(T));
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(combined.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str3.c_str(), str3.size()*sizeof(T)))
				.IN_SEQUENCE(seq)
				.TIMES(1)
				.RETURN(str3.size()*sizeof(T));
		CHECK_EQ(buf.sgetn(dst, combined.size()), combined.size());
		CHECK_EQ(strcmp<T>(dst, combined.c_str()), 0);
	}

	TEST_CASE_TEMPLATE("should return N < count if less than count characters have been read on sgetn()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		const std::basic_string<T> str3{make_string<T>(" This is the third line!")};
		const std::basic_string<T> combined{str1+str2+str3};
		T dst[combined.size()+1]{};
		trompeloeil::sequence seq;
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::ge(combined.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str1.c_str(), str1.size()*sizeof(T)))
				.IN_SEQUENCE(seq)
				.TIMES(1)
				.RETURN(str1.size()*sizeof(T));
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::_))
				.IN_SEQUENCE(seq)
				.TIMES(1)
				.RETURN(0);
		CHECK_EQ(buf.sgetn(dst, combined.size()), str1.size());
		CHECK_EQ(strcmp<T>(dst, str1.c_str()), 0);
	}

	TEST_CASE_TEMPLATE("should return characters read on partial character read on sgetn()" * doctest::skip(), T, wchar_t, char16_t, char32_t)
	{
		static_assert(pipestream::is_multibyte_v<T>);
		const std::basic_string<T> str{make_string<T>("Hello")};
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		T dst[str.size()]{};
		const int read_bytes = str.size()/2*sizeof(T)+sizeof(T)/2; // read half string and half character
		trompeloeil::sequence seq;
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::gt(read_bytes)))
				.SIDE_EFFECT(std::memcpy(_1, str.c_str(), read_bytes))
				.IN_SEQUENCE(seq)
				.TIMES(1)
				.RETURN(read_bytes);
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::_))
				.IN_SEQUENCE(seq)
				.TIMES(1)
				.RETURN(0);
		CHECK_EQ(buf.sgetn(dst, str.size()), read_bytes/sizeof(T));
	}

	TEST_CASE_TEMPLATE("should continue reading character+string after partial character read on sgetn()" * doctest::skip(), T, wchar_t, char16_t, char32_t)
	{
		static_assert(pipestream::is_multibyte_v<T>);
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>(" World!")};
		const std::basic_string<T> combined{str1+str2};
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		T dst[combined.size()]{};
		const int read1bytes = combined.size()/4*sizeof(T)+sizeof(T)/2; // Read quarter of combined and half a character
		const int read2bytes = combined.size()/2*sizeof(T)+sizeof(T)/2; // Read half of combined and half a character
		trompeloeil::sequence seq;
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::gt(read1bytes)))
				.SIDE_EFFECT(std::memcpy(_1, combined.c_str(), read1bytes))
				.IN_SEQUENCE(seq)
				.TIMES(1)
				.RETURN(read1bytes);
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::gt(read2bytes)))
				.SIDE_EFFECT(std::memcpy(_1, reinterpret_cast<const char*>(combined.c_str())+read1bytes, read2bytes))
				.IN_SEQUENCE(seq)
				.TIMES(1)
				.RETURN(read2bytes);
		CHECK_EQ(buf.sgetn(dst, str1.size()), str1.size()); // 1st read with partial character without buffer
		CHECK_EQ(std::memcmp(dst, combined.c_str(), str1.size()*sizeof(T)), 0);
		const int read3bytes = combined.size()*sizeof(T)-read1bytes-read2bytes; // Read rest of partial charater and string
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::gt(read3bytes)))
				.SIDE_EFFECT(std::memcpy(_1, reinterpret_cast<const char*>(combined.c_str())+read1bytes+read2bytes, read3bytes))
				.TIMES(1)
				.RETURN(read3bytes);
		CHECK_EQ(buf.sgetn(dst+str1.size(), str2.size()), read3bytes/sizeof(T)+1); // 3rd read with partial character and rest of the string
		CHECK_EQ(std::memcmp(dst, combined.c_str(), combined.size()), 0);
	}
}

// TODO should allow unget after partial character read on ungetc() (without and with buffer)
// TODO should allow unget after partial string read on ungetc() (without buffer)
// TODO should clear partial shift buffer on rdfd()
// TODO should read partial character from pipe on sync()
// TODO should put leftover string into buffer on sputn() if partial write and rest fits into buffer
//
// how handle on partial write and next write takes up the whole pipe_write provided buffer, where to fit the other partial write?

TEST_SUITE("sync()")
{
	TEST_CASE("should return 0 when using null buffer with sync()")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		CHECK_EQ(buf.pubsetbuf(nullptr, 0), &buf);
		CHECK_EQ(buf.pubsync(), 0);
	}

	TEST_CASE_TEMPLATE("should return 0 when buffer empty on sync()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		CHECK_EQ(buf.pubsync(), 0);
	}

	TEST_CASE_TEMPLATE("should return -1 when buffer not empty but having invalid fd on sync()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{INVALID_FD});
		const std::basic_string<T> str{make_string<T>("Hello World")};
		buf.sputn(str.c_str(), str.size());
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, trompeloeil::_))
				.TIMES(1)
				.RETURN(-1);
		CHECK_EQ(buf.pubsync(), -1);
		flush_mocked(buf);
	}

	TEST_CASE("should return 0 when sync() is successful")
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		const std::string str{"Hello World"};
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str.size()))
				.WITH(not std::strcmp(static_cast<const char*>(_1), str.c_str()))
				.TIMES(1)
				.RETURN(str.size());
		CHECK_EQ(buf.sputn(str.c_str(), str.size()), str.size());
		CHECK_EQ(buf.pubsync(), 0);
	}

	TEST_CASE("should return -1 on partial write on sync()" * doctest::skip())
	{
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		const std::string str{"Hello World!"};
		buf.sputn(str.c_str(), str.size());
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, trompeloeil::eq(str.size())))
				.TIMES(1)
				.RETURN(str.size()/2);
		CHECK_EQ(buf.pubsync(), -1);
		flush_mocked(buf);
	}

	TEST_CASE_TEMPLATE("should leave buffer in consistent state on partial write on sync()" * doctest::skip(), T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str{make_string<T>("Hello World!")};
		buf.sputn(str.c_str(), str.size());
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, trompeloeil::eq(str.size()*sizeof(T))))
				.TIMES(1)
				.RETURN(str.size()/2*sizeof(T));
		CHECK_EQ(buf.pubsync(), -1);
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, trompeloeil::eq(str.size()/2*sizeof(T))))
				.TIMES(1)
				.RETURN(str.size()/2*sizeof(T));
		CHECK_EQ(buf.pubsync(), 0);
	}

	TEST_CASE_TEMPLATE("should write buffer to pipe on sync()", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello ")};
		const std::basic_string<T> str2{make_string<T>("World")};
		CHECK_EQ(buf.sputn(str1.c_str(), str1.size()), str1.size());
		CHECK_EQ(buf.sputn(str2.c_str(), str2.size()), str2.size());
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, (str1.size()+str2.size())*sizeof(T)))
				.WITH(not std::memcmp(_1, (str1+str2).c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		CHECK_EQ(buf.pubsync(), 0);
	}

	TEST_CASE_TEMPLATE("should reuse buffer from start after sync()", T, char, wchar_t, char16_t, char32_t)
	{
		constexpr std::streamsize buf_size = 32;
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		T array[buf_size]{};
		buf.pubsetbuf(array, buf_size);
		const std::basic_string<T> str1{make_string<T>("Hello World")};
		const std::basic_string<T> str2{make_string<T>("This is the second str")};
		CHECK_EQ(buf.sputn(str1.c_str(), str1.size()), str1.size());
		CHECK_EQ(strcmp<T>(array, str1.c_str()), 0);
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str1.size()*sizeof(T)))
				.WITH(not std::memcmp(_1, str1.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		CHECK_EQ(buf.pubsync(), 0);
		CHECK_EQ(buf.sputn(str2.c_str(), str2.size()), str2.size());
		CHECK_EQ(strcmp<T>(array, str2.c_str()), 0);
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str2.size()*sizeof(T)))
				.WITH(not std::memcmp(_1, str2.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		CHECK_EQ(buf.pubsync(), 0);
	}

	TEST_CASE_TEMPLATE("should leave buffer in consistent state when sync() fails", T, char, wchar_t, char16_t, char32_t)
	{
		const std::basic_string<T> str1{make_string<T>("Hello World")};
		const std::basic_string<T> str2{make_string<T>("This is the second str")};
		const std::basic_string<T> combined{str1+str2};
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		T array[combined.size()+1]{};
		buf.pubsetbuf(array, combined.size()+1);
		CHECK_EQ(buf.sputn(str1.c_str(), str1.size()), str1.size());
		CHECK_EQ(strcmp<T>(array, str1.c_str()), 0);
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str1.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(-1);
		CHECK_NE(buf.pubsync(), 0);
		CHECK_EQ(buf.sputn(str2.c_str(), str2.size()), str2.size());
		CHECK_EQ(strcmp<T>(array, (str1+str2).c_str()), 0);
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, combined.size()*sizeof(T)))
				.WITH(not std::memcmp(_1, combined.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		CHECK_EQ(buf.pubsync(), 0);
	}
}

TEST_SUITE("misc")
{
	TEST_CASE_TEMPLATE("should return number of character in get buffer on in_avail()", T, char, wchar_t, char16_t, char32_t)
	{
		const std::basic_string<T> str{make_string<T>("Hello World!")};
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		T array[str.size()+2]{};
		buf.pubsetbuf(array, str.size()+2);
		CHECK_EQ(buf.in_avail(), 0);
		T dst[str.size()+1]{};
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::gt(str.size()*sizeof(T))))
				.SIDE_EFFECT(std::memcpy(_1, str.c_str(), str.size()*sizeof(T)))
				.TIMES(1)
				.RETURN(str.size()*sizeof(T));
		CHECK_EQ(buf.sgetn(dst, 2), 2);
		CHECK_EQ(buf.in_avail(), str.size()-2);
	}

	TEST_CASE_TEMPLATE("should use buffer correctly after buffer write-out to pipe", T, char, wchar_t, char16_t, char32_t)
	{
		auto buf = pipestream::make_pipebuf<T>(mocked_fd{});
		const std::basic_string<T> str1{make_string<T>("Hello")};
		const std::basic_string<T> str2{make_string<T>("tYZkZ")};
		const std::basic_string<T> str3{make_string<T>("qdqCg")};
		T array[str1.size() + 1]{};
		buf.pubsetbuf(array, str1.size());
		auto check = [&array, &buf](const std::basic_string<T>& s) {
			for (const char& c : s) {
				CHECK_EQ(buf.sputc(c), c) ;
			}
			CHECK_EQ(strcmp<T>(array, s.c_str()), 0);
		};
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str1.size()*sizeof(T)))
				.WITH(not std::memcmp(_1, str1.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		check(str1);
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str2.size()*sizeof(T)))
				.WITH(not std::memcmp(_1, str2.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		check(str2);
		REQUIRE_CALL(buf.rdfd(), write(trompeloeil::_, str3.size()*sizeof(T)))
				.WITH(not std::memcmp(_1, str3.c_str(), _2))
				.TIMES(1)
				.RETURN(_2);
		check(str3);
	}
}

TEST_SUITE("position seeking")
{
	TEST_CASE("should ignore calls to pubseekpos()")
	{
		const std::string str1{"Hello"};
		const std::string str2{" World!"};
		const std::string combined{str1+str2};
		char array[combined.size()+1]{};
		auto* array_ptr = array;
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::gt(combined.size())))
				.TIMES(1)
				.SIDE_EFFECT(std::strcpy(static_cast<char*>(_1), combined.c_str()))
				.RETURN(combined.size());
		buf.sgetc(); // populate buffer
		CHECK_EQ(buf.sgetn(array, str1.size()), str1.size());
		CHECK_EQ(buf.pubseekpos(2), pipestream::pipebuf::pos_type(pipestream::pipebuf::off_type(-1)));
		CHECK_EQ(buf.sgetn(array+str1.size(), str2.size()), str2.size());
		CHECK_EQ(array_ptr, combined.c_str());
	}

	TEST_CASE("should ignore calls to pubseekoff()")
	{
		const std::string str1{"Hello"};
		const std::string str2{" World!"};
		const std::string combined{str1+str2};
		char array[combined.size()+1]{};
		auto* array_ptr = array;
		auto buf = pipestream::make_pipebuf(mocked_fd{});
		REQUIRE_CALL(buf.rdfd(), read(trompeloeil::_, trompeloeil::gt(combined.size())))
				.TIMES(1)
				.SIDE_EFFECT(std::strcpy(static_cast<char*>(_1), combined.c_str()))
				.RETURN(combined.size());
		buf.sgetc(); // populate buffer
		CHECK_EQ(buf.sgetn(array, str1.size()), str1.size());
		CHECK_EQ(buf.pubseekoff(2, std::ios_base::beg), pipestream::pipebuf::pos_type(pipestream::pipebuf::off_type(-1)));
		CHECK_EQ(buf.sgetn(array+str1.size(), str2.size()), str2.size());
		CHECK_EQ(array_ptr, combined.c_str());
	}
}
