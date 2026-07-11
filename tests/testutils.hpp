#ifndef PIPESTREAM_TESTS_TESTUTILS_HPP
#define PIPESTREAM_TESTS_TESTUTILS_HPP

#include <cstring>
#include <string>
#include <type_traits>

#include <pipestream/pipestream.hpp>

namespace pipestream::testutils
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
	std::size_t strlen(const CharT *const str)
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
	std::size_t strlen<char>(const char *const str)
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
		for (unsigned i = 0; i < sizeof(C); i++) {
			*(reinterpret_cast<char*>(&c)+i) = 'a'+i+offset*sizeof(C);
		}
		return c;
	}
}

#endif /* PIPESTREAM_TESTS_TESTUTILS_HPP */
